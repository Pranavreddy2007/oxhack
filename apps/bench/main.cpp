// embercast_bench: engine stage latency and throughput.
// The measured loop is the engine tick (filter + rollout + policy). The truth
// simulator runs between ticks and is not measured. Raw samples go to CSV.

#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>

#include "embercast/clock.hpp"
#include "embercast/engine.hpp"
#include "embercast/metrics.hpp"
#include "embercast/rng.hpp"
#include "scenario.hpp"
#include "truth.hpp"

using namespace embercast;

namespace {

long vm_hwm_kb() {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line))
    if (line.rfind("VmHWM:", 0) == 0) return std::strtol(line.c_str() + 6, nullptr, 10);
  return -1;
}

double cpu_seconds() {
  rusage ru{};
  getrusage(RUSAGE_SELF, &ru);
  return double(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
         1e-6 * double(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec);
}

void usage() {
  std::puts(
      "embercast_bench [options]\n"
      "  --backend cpu_naive|cpu_opt|opencl  --threads N  --K N --H N --D N --N N\n"
      "  --episodes N (default 10)  --ticks N (default 120)  --seed0 N (default 1000, TUNE)\n"
      "  --out-dir DIR (default results/raw/bench)  --label TEXT\n"
      "  --baseline FILE  --max-p99-growth PCT (default 50)  --save-baseline FILE");
}

}  // namespace

int main(int argc, char** argv) {
  EngineConfig cfg;
  cfg.backend = "cpu_opt";
  cfg.threads = 0;
  int N = 96, episodes = 10, ticks = 120;
  std::uint64_t seed0 = 1000;
  std::string out_dir = "results/raw/bench", label, baseline, save_baseline;
  double max_growth = 50.0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { usage(); std::exit(2); }
      return argv[++i];
    };
    if (a == "--backend") cfg.backend = next();
    else if (a == "--threads") cfg.threads = std::stoi(next());
    else if (a == "--K") cfg.K = std::stoi(next());
    else if (a == "--H") cfg.H = std::stoi(next());
    else if (a == "--D") cfg.D = std::stoi(next());
    else if (a == "--N") N = std::stoi(next());
    else if (a == "--episodes") episodes = std::stoi(next());
    else if (a == "--ticks") ticks = std::stoi(next());
    else if (a == "--seed0") seed0 = std::stoull(next());
    else if (a == "--out-dir") out_dir = next();
    else if (a == "--label") label = next();
    else if (a == "--baseline") baseline = next();
    else if (a == "--max-p99-growth") max_growth = std::stod(next());
    else if (a == "--save-baseline") save_baseline = next();
    else { usage(); return a == "--help" ? 0 : 2; }
  }
  if (label.empty()) {
    std::ostringstream os;
    os << cfg.backend << "_t" << cfg.threads << "_N" << N << "_K" << cfg.K << "_H" << cfg.H;
    label = os.str();
  }
  const std::size_t cap = std::size_t(episodes) * std::size_t(ticks) + 16;
  std::map<std::string, LatencyRecorder> rec;
  for (const char* s : {"tick_filter", "decision_rollout", "decision_policy", "decision_total",
                        "decision_cold_process", "decision_cold_episode"})
    rec[s].reserve(cap);

  ScenarioOptions so;
  so.n = N;
  so.ticks = ticks;
  const auto wall0 = now_ns();
  const double cpu0 = cpu_seconds();
  int threads_used = 0;
  std::vector<SensorMsg> sens;
  std::vector<PerimeterMsg> scans;
  sens.reserve(64);
  scans.reserve(16);
  for (int e = 0; e < episodes; ++e) {
    const Scenario sc = make_scenario(seed0 + std::uint64_t(e), so);
    TruthSim truth(sc);
    std::vector<std::uint8_t> init(sc.terrain.cells());
    initial_state(sc, init.data());
    EngineStatic es{&sc.terrain, &sc.mc, ThetaPrior{}, sc.crews, sc.travel_ticks, init.data()};
    EngineConfig c = cfg;
    c.seed = splitmix64(sc.seed);
    Engine eng(es, c);
    threads_used = c.threads;
    for (int t = 0; t < ticks; ++t) {
      sens.clear();
      scans.clear();
      truth.collect(std::uint32_t(t), sens, scans);
      for (auto& m : sens) eng.on_sensor(m);
      for (auto& m : scans) eng.on_perimeter(m);
      Plan p;
      const bool decided = eng.tick(std::uint32_t(t), p);  // measured inside the engine
      const StageTimes& tm = eng.last_times();
      if (t == 0) {
        rec[e == 0 ? "decision_cold_process" : "decision_cold_episode"].record(tm.total_ns);
      } else {
        rec["tick_filter"].record(tm.filter_ns);
        if (decided) {
          rec["decision_rollout"].record(tm.rollout_ns);
          rec["decision_policy"].record(tm.policy_ns);
          rec["decision_total"].record(tm.total_ns);
        }
      }
      if (decided) truth.set_plan(p);
      truth.step();
    }
  }
  const double wall_s = double(now_ns() - wall0) * 1e-9;
  const double cpu_s = cpu_seconds() - cpu0;

  std::filesystem::create_directories(out_dir);
  const std::string raw = out_dir + "/" + label + "_raw.csv";
  {
    std::ofstream f(raw);
    f << "label,stage,ns\n";
    for (auto& [stage, r] : rec)
      for (auto v : r.samples()) f << label << ',' << stage << ',' << v << '\n';
  }
  const int cells = N * N;
  const auto rs = rec["decision_rollout"].stats();
  const double member_ticks = double(cfg.K) * cfg.H;
  {
    std::ofstream f(out_dir + "/" + label + "_meta.json");
    f << "{\n \"label\": \"" << label << "\",\n \"backend\": \"" << cfg.backend
      << "\",\n \"threads_requested\": " << cfg.threads << ",\n \"hardware_threads\": "
      << sysconf(_SC_NPROCESSORS_ONLN) << ",\n \"N\": " << N << ",\n \"K\": " << cfg.K
      << ",\n \"H\": " << cfg.H << ",\n \"D\": " << cfg.D << ",\n \"episodes\": " << episodes
      << ",\n \"ticks\": " << ticks << ",\n \"wall_s\": " << wall_s << ",\n \"cpu_s\": " << cpu_s
      << ",\n \"cpu_util_cores\": " << (wall_s > 0 ? cpu_s / wall_s : 0)
      << ",\n \"vm_hwm_kb\": " << vm_hwm_kb()
      << ",\n \"rollout_member_ticks_per_s\": " << (rs.mean > 0 ? member_ticks / (rs.mean * 1e-9) : 0)
      << ",\n \"rollout_cell_updates_per_s\": "
      << (rs.mean > 0 ? member_ticks * cells / (rs.mean * 1e-9) : 0)
      << ",\n \"note\": \"cell updates are nominal (K*H*N*N per rollout); thread pinning: none\"\n}\n";
  }
  (void)threads_used;
  std::printf("label %s  (raw: %s)\n", label.c_str(), raw.c_str());
  std::printf("%-22s %8s %10s %10s %10s %10s %10s %10s  (microseconds)\n", "stage", "count", "mean",
              "p50", "p95", "p99", "p99.9", "max");
  std::map<std::string, double> p99s;
  for (auto& [stage, r] : rec) {
    const auto s = r.stats();
    p99s[stage] = s.p99;
    char p999[32];
    if (s.p999 >= 0) std::snprintf(p999, sizeof p999, "%10.1f", s.p999 / 1e3);
    else std::snprintf(p999, sizeof p999, "%10s", "n/a");
    std::printf("%-22s %8zu %10.1f %10.1f %10.1f %10.1f %s %10.1f\n", stage.c_str(), s.count,
                s.mean / 1e3, s.p50 / 1e3, s.p95 / 1e3, s.p99 / 1e3, p999, s.max / 1e3);
  }
  std::printf("decision throughput %.1f decisions/s, rollout %.3g member-ticks/s, %.3g cell-updates/s\n",
              rec["decision_total"].stats().mean > 0 ? 1e9 / rec["decision_total"].stats().mean : 0.0,
              rs.mean > 0 ? member_ticks / (rs.mean * 1e-9) : 0.0,
              rs.mean > 0 ? member_ticks * cells / (rs.mean * 1e-9) : 0.0);
  std::printf("cpu util %.2f cores, peak RSS %ld kB\n", wall_s > 0 ? cpu_s / wall_s : 0.0, vm_hwm_kb());

  if (!save_baseline.empty()) {
    std::ofstream f(save_baseline);
    for (auto& [stage, v] : p99s) f << stage << ' ' << v << '\n';
    std::printf("saved baseline %s\n", save_baseline.c_str());
  }
  if (!baseline.empty()) {
    std::ifstream f(baseline);
    if (!f) { std::fprintf(stderr, "cannot read baseline %s\n", baseline.c_str()); return 2; }
    std::string stage;
    double v;
    int fail = 0;
    while (f >> stage >> v) {
      if (stage.find("cold") != std::string::npos || !p99s.count(stage) || v <= 0) continue;
      const double growth = 100.0 * (p99s[stage] - v) / v;
      std::printf("regression check %-18s p99 %.1f us vs %.1f us (%+.1f %%)\n", stage.c_str(),
                  p99s[stage] / 1e3, v / 1e3, growth);
      if (growth > max_growth) ++fail;
    }
    if (fail) {
      std::printf("FAIL: %d stage(s) p99 grew more than %.0f %%\n", fail, max_growth);
      return 1;
    }
    std::printf("PASS: p99 growth within %.0f %%\n", max_growth);
  }
  return 0;
}
