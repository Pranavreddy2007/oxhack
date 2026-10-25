// embercast_replay: offline evaluation over a seed set.
// Runs every policy on the same truth realisation per seed (common random
// numbers) and writes one CSV row per (seed, policy).

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "episode.hpp"

using namespace embercast;

namespace {

std::vector<std::uint64_t> read_seed_set(const std::string& path, const std::string& set) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    std::exit(2);
  }
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string s = ss.str();
  const auto key = s.find("\"" + set + "\"");
  if (key == std::string::npos) {
    std::fprintf(stderr, "seed set %s not found\n", set.c_str());
    std::exit(2);
  }
  const auto a = s.find('[', key), b = s.find(']', a);
  std::vector<std::uint64_t> out;
  std::string num;
  for (auto i = a + 1; i <= b; ++i) {
    if (std::isdigit(static_cast<unsigned char>(s[i]))) num += s[i];
    else if (!num.empty()) {
      out.push_back(std::stoull(num));
      num.clear();
    }
  }
  return out;
}

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> v;
  std::stringstream ss(s);
  std::string x;
  while (std::getline(ss, x, ',')) if (!x.empty()) v.push_back(x);
  return v;
}

void usage() {
  std::puts(
      "embercast_replay [options]\n"
      "  --seeds-file PATH   (default laptop/tools/seeds.json)\n"
      "  --set tune|test     seed set (default tune)\n"
      "  --seeds a,b,c       explicit seeds (overrides --set)\n"
      "  --limit N           use the first N seeds\n"
      "  --policies LIST     ensemble,deterministic,front_nearest,oracle\n"
      "  --stress NAME       none|regime|dropout30|late_scans|stuck\n"
      "  --K N --H N --D N --N N --lambda X --alpha X --switch X --beta X\n"
      "  --no-filter         ensemble without weights/resampling\n"
      "  --backend NAME      cpu_naive|cpu_opt (default cpu_opt)\n"
      "  --engine-threads N  threads inside the backend (default 1)\n"
      "  --jobs N            parallel episodes (default hardware)\n"
      "  --tag TEXT          label written to the CSV\n"
      "  --out PATH          CSV output (default stdout)\n"
      "  --log PATH          decision log JSONL\n"
      "  --latency PATH      raw per-tick stage latency CSV");
}

}  // namespace

int main(int argc, char** argv) {
  std::string seeds_file = "laptop/tools/seeds.json", set = "tune", seeds_arg, out_path, log_path,
              lat_path, tag = "default";
  std::vector<std::string> policies = {"ensemble", "deterministic", "front_nearest", "oracle"};
  EpisodeOptions opt;
  opt.engine.backend = "cpu_opt";
  int limit = -1;
  unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", a.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--seeds-file") seeds_file = next();
    else if (a == "--set") set = next();
    else if (a == "--seeds") seeds_arg = next();
    else if (a == "--limit") limit = std::stoi(next());
    else if (a == "--policies") policies = split(next());
    else if (a == "--stress") {
      if (!parse_stress(next(), opt.scenario.stress)) { usage(); return 2; }
    }
    else if (a == "--K") opt.engine.K = std::stoi(next());
    else if (a == "--H") opt.engine.H = std::stoi(next());
    else if (a == "--D") opt.engine.D = std::stoi(next());
    else if (a == "--N") opt.scenario.n = std::stoi(next());
    else if (a == "--lambda") opt.engine.risk.lambda = std::stod(next());
    else if (a == "--alpha") opt.engine.risk.alpha = std::stod(next());
    else if (a == "--switch") opt.engine.risk.switch_cost = std::stod(next());
    else if (a == "--beta") opt.engine.filter.beta = std::stof(next());
    else if (a == "--no-filter") opt.engine.filter.weighting = false;
    else if (a == "--backend") opt.engine.backend = next();
    else if (a == "--engine-threads") opt.engine.threads = std::stoi(next());
    else if (a == "--jobs") jobs = unsigned(std::max(1, std::stoi(next())));
    else if (a == "--tag") tag = next();
    else if (a == "--out") out_path = next();
    else if (a == "--log") log_path = next();
    else if (a == "--latency") lat_path = next();
    else if (a == "--help" || a == "-h") { usage(); return 0; }
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
  }
  std::vector<std::uint64_t> seeds;
  if (!seeds_arg.empty()) for (auto& s : split(seeds_arg)) seeds.push_back(std::stoull(s));
  else seeds = read_seed_set(seeds_file, set);
  if (limit >= 0 && std::size_t(limit) < seeds.size()) seeds.resize(std::size_t(limit));

  struct Job { std::uint64_t seed; std::string policy; };
  std::vector<Job> work;
  for (auto s : seeds) for (auto& p : policies) work.push_back({s, p});
  std::vector<EpisodeResult> results(work.size());
  std::atomic<std::size_t> next_job{0};
  std::vector<std::thread> pool;
  for (unsigned w = 0; w < std::min<std::size_t>(jobs, work.size()); ++w)
    pool.emplace_back([&] {
      for (std::size_t j; (j = next_job++) < work.size();)
        results[j] = run_episode(work[j].seed, work[j].policy, opt);
    });
  for (auto& th : pool) th.join();

  std::FILE* out = out_path.empty() ? stdout : std::fopen(out_path.c_str(), "w");
  if (!out) { std::fprintf(stderr, "cannot write %s\n", out_path.c_str()); return 2; }
  std::fprintf(out, "tag,seed,stress,policy,backend,N,K,H,D,lambda,switch,beta,filter,loss,total_value,final_area,"
                    "pred_n,brier,logloss,crps_n,crps,var_sum,sqerr_sum,lost_n,lost_brier,resamples,"
                    "sensor_ok,sensor_late,sensor_oor,sensor_stuck,sensor_dup,scans_ok,scans_late");
  for (int b = 0; b < 10; ++b) std::fprintf(out, ",bin_p%d,bin_y%d,bin_n%d", b, b, b);
  std::fprintf(out, "\n");
  for (const auto& r : results) {
    const auto& pm = r.pm;
    const auto& c = r.counters;
    const bool is_ens = r.policy == "ensemble";
    std::fprintf(out, "%s,%llu,%s,%s,%s,%d,%d,%d,%d,%g,%g,%g,%d,%.6f,%.6f,%d,%ld,%.9g,%.9g,%ld,%.9g,%.9g,%.9g,%ld,%.9g,%d,%u,%u,%u,%u,%u,%u,%u",
                 tag.c_str(), static_cast<unsigned long long>(r.seed), r.stress.c_str(), r.policy.c_str(),
                 opt.engine.backend.c_str(), opt.scenario.n,
                 is_ens ? opt.engine.K : 1, opt.engine.H, opt.engine.D,
                 is_ens ? opt.engine.risk.lambda : 0.0, opt.engine.risk.switch_cost,
                 double(opt.engine.filter.beta), is_ens ? int(opt.engine.filter.weighting) : 0,
                 r.loss, r.total_value, r.final_area, pm.n, pm.brier, pm.logloss, pm.crps_n, pm.crps,
                 pm.var_sum, pm.sqerr_sum, pm.lost_n, pm.lost_brier, r.resamples, c.sensor_ok,
                 c.sensor_late, c.sensor_out_of_range, c.sensor_stuck, c.sensor_dup, c.scans_ok,
                 c.scans_late);
    for (int b = 0; b < 10; ++b) std::fprintf(out, ",%.9g,%.9g,%ld", pm.bin_p[b], pm.bin_y[b], pm.bin_n[b]);
    std::fprintf(out, "\n");
  }
  if (out != stdout) std::fclose(out);
  if (!log_path.empty()) {
    std::ofstream lf(log_path, std::ios::binary);
    for (const auto& r : results) lf << decision_log(r);
  }
  if (!lat_path.empty()) {
    std::ofstream lf(lat_path);
    lf << "seed,policy,stage,ns\n";
    for (const auto& r : results) {
      if (r.policy == "oracle") continue;
      lf << r.seed << ',' << r.policy << ",cold_decision," << r.first_decision_ns << '\n';
      for (auto v : r.filter_ns) lf << r.seed << ',' << r.policy << ",filter," << v << '\n';
      for (auto v : r.rollout_ns) lf << r.seed << ',' << r.policy << ",rollout," << v << '\n';
      for (auto v : r.policy_ns) lf << r.seed << ',' << r.policy << ",policy," << v << '\n';
      for (auto v : r.decision_ns) lf << r.seed << ',' << r.policy << ",decision," << v << '\n';
    }
  }
  return 0;
}
