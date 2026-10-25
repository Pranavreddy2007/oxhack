// embercast_board_daemon: the engine process (UNO Q Linux side, or laptop).
//
// Input: TCP from sim_stream. Frames SENSOR and PERIMETER for tick t, then a
// HEARTBEAT with t_sim_tick = t. On that HEARTBEAT the engine advances to t.
// Output on the same connection: DECISION (decision ticks, echoes the
// heartbeat t_send_ns, carries board stage timings), FORECAST (P(burn) map),
// and a HEARTBEAT with the engine state.
//
// Link loss: no frame for --stale-ms -> the engine keeps the last plan and
// reports STALE. The daemon accepts a new connection at any time.
// The scenario static data comes from the seed (same generator as sim_stream).

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "embercast/clock.hpp"
#include "embercast/engine.hpp"
#include "embercast/log_writer.hpp"
#include "embercast/metrics.hpp"
#include "embercast/rng.hpp"
#include "embercast/transport.hpp"
#include "scenario.hpp"

using namespace embercast;

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }

class ConsoleStatus final : public IStatusSink {
 public:
  void status(HealthState s, std::uint8_t top) override {
    if (s == last_) return;
    last_ = s;
    static const char* names[] = {"OK", "STALE", "DEGRADED", "FAULT"};
    std::printf("[daemon] state %s (top risk cluster %u)\n", names[int(s)], unsigned(top));
    std::fflush(stdout);
  }

 private:
  HealthState last_ = HealthState::Fault;
};

void usage() {
  std::puts(
      "embercast_board_daemon [options]\n"
      "  --listen HOST (default 127.0.0.1; use 0.0.0.0 on the board)  --port N (default 7070)\n"
      "  --seed N (default 1000)  --N N  --ticks N  --K N --H N --D N\n"
      "  --backend cpu_naive|cpu_opt (default cpu_opt)  --threads N (0 = all cores)\n"
      "  --stale-ms N (default 1500)\n"
      "  --log PATH        decision log JSONL (written by a separate thread)\n"
      "  --mcu-uds PATH    Unix socket for bridge/mcu_sidecar.py (off by default)\n"
      "  --sensor-source laptop|mcu  engine weather input (default laptop)\n"
      "  --mcu-timeout-ms N  MCU silence that gives SENSOR_LOST (default 2000)\n"
      "  --bench-out PATH  raw stage latency CSV, written at exit");
}

}  // namespace

int main(int argc, char** argv) {
  std::string host = "127.0.0.1", log_path, bench_out, mcu_uds, sensor_source = "laptop";
  int mcu_timeout_ms = 2000;
  int port = 7070, stale_ms = 1500;
  ScenarioOptions so;
  std::uint64_t seed = 1000;
  EngineConfig cfg;
  cfg.backend = "cpu_opt";
  cfg.threads = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { usage(); std::exit(2); }
      return argv[++i];
    };
    if (a == "--listen") host = next();
    else if (a == "--port") port = std::stoi(next());
    else if (a == "--seed") seed = std::stoull(next());
    else if (a == "--N") so.n = std::stoi(next());
    else if (a == "--ticks") so.ticks = std::stoi(next());
    else if (a == "--K") cfg.K = std::stoi(next());
    else if (a == "--H") cfg.H = std::stoi(next());
    else if (a == "--D") cfg.D = std::stoi(next());
    else if (a == "--backend") cfg.backend = next();
    else if (a == "--threads") cfg.threads = std::stoi(next());
    else if (a == "--stale-ms") stale_ms = std::stoi(next());
    else if (a == "--log") log_path = next();
    else if (a == "--bench-out") bench_out = next();
    else if (a == "--mcu-uds") mcu_uds = next();
    else if (a == "--sensor-source") sensor_source = next();
    else if (a == "--mcu-timeout-ms") mcu_timeout_ms = std::stoi(next());
    else { usage(); return a == "--help" ? 0 : 2; }
  }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  const int lfd = tcp_listen(host, port);
  if (lfd < 0) {
    std::fprintf(stderr, "[daemon] cannot listen on %s:%d\n", host.c_str(), port);
    return 1;
  }
  std::printf("[daemon] %s  listening on %s:%d  backend %s  K=%d H=%d D=%d\n", safety_notice(),
              host.c_str(), port, cfg.backend.c_str(), cfg.K, cfg.H, cfg.D);
  std::fflush(stdout);

  const int mcu_lfd = mcu_uds.empty() ? -1 : uds_listen(mcu_uds);
  if (!mcu_uds.empty() && mcu_lfd < 0) {
    std::fprintf(stderr, "[daemon] cannot listen on %s\n", mcu_uds.c_str());
    return 1;
  }
  if (mcu_lfd >= 0) std::printf("[daemon] MCU sidecar socket %s, sensor source %s\n", mcu_uds.c_str(), sensor_source.c_str());
  std::unique_ptr<StreamTransport> mcu;
  std::uint64_t last_mcu = 0;
  bool mcu_lost = mcu_lfd >= 0;  // lost until the first MCU frame
  bool using_mcu = false;
  std::uint32_t mcu_seq = 0;

  LogWriter log(log_path);
  ConsoleStatus console;
  LatencyRecorder rec_filter(1 << 20), rec_rollout(1 << 18), rec_policy(1 << 18), rec_total(1 << 18);

  // Engine state for one scenario. A tick that goes back to 0 starts a new episode.
  std::unique_ptr<Scenario> sc;
  std::vector<std::uint8_t> init;
  std::unique_ptr<Engine> eng;
  std::int64_t last_tick = -1;
  auto reset = [&]() {
    sc = std::make_unique<Scenario>(make_scenario(seed, so));
    init.assign(sc->terrain.cells(), 0);
    initial_state(*sc, init.data());
    EngineStatic es{&sc->terrain, &sc->mc, ThetaPrior{}, sc->crews, sc->travel_ticks, init.data()};
    EngineConfig c = cfg;
    c.seed = splitmix64(seed ^ 0xB0A2Dull);
    eng = std::make_unique<Engine>(es, c);
    last_tick = -1;
  };
  reset();

  std::unique_ptr<StreamTransport> tr;
  auto rf = std::make_unique<RawFrame>();
  auto fm = std::make_unique<ForecastMsg>();
  auto pm = std::make_unique<PerimeterMsg>();
  std::vector<std::uint8_t> out(kMaxFrame);
  std::uint64_t last_rx = now_ns();
  std::uint32_t out_seq = 0;
  char line[384];

  while (!g_stop.load()) {
    if (!tr || !tr->connected()) {
      const int fd = accept_timeout(lfd, 100);
      if (fd >= 0) {
        tr = std::make_unique<StreamTransport>(fd);
        std::printf("[daemon] client connected\n");
        std::fflush(stdout);
        last_rx = now_ns();
      }
    }
    bool any = false;
    if (mcu_lfd >= 0) {
      if (!mcu || !mcu->connected()) {
        const int fd = accept_timeout(mcu_lfd, 0);
        if (fd >= 0) {
          mcu = std::make_unique<StreamTransport>(fd);
          std::printf("[daemon] MCU sidecar connected\n");
        }
      }
      while (mcu && mcu->pop(*rf)) {
        any = true;
        if (FrameType(rf->h.type) != FrameType::Sensor) continue;
        SensorMsg m;
        if (!decode(rf->h, rf->payload.data(), m)) continue;
        last_mcu = now_ns();
        if (mcu_lost) {
          mcu_lost = false;
          std::printf("[daemon] MCU sensor frames OK\n");
        }
        if (m.flags & 0x80000000u) {  // knob frame: relay to the laptop
          if (tr && tr->connected()) tr->send(out.data(), encode(m, out.data(), out.size()));
          m.flags &= ~0x80000000u;
        }
        if (sensor_source == "mcu") {
          if (!using_mcu) {
            eng->reset_sensor_seq();
            using_mcu = true;
          }
          m.tick = std::uint32_t(std::max<std::int64_t>(0, last_tick));
          eng->on_sensor(m);
        }
      }
      if (!mcu_lost && now_ns() - last_mcu > std::uint64_t(mcu_timeout_ms) * 1000000ull) {
        mcu_lost = true;
        std::printf("[daemon] SENSOR_LOST: no MCU frame for %d ms%s\n", mcu_timeout_ms,
                    sensor_source == "mcu" ? "; laptop sensor emulation takes over" : "");
      }
      if (mcu_lost && using_mcu) {
        eng->reset_sensor_seq();
        using_mcu = false;
      }
    }
    while (tr && tr->pop(*rf)) {
      any = true;
      last_rx = now_ns();
      eng->set_link_down(false);
      const auto type = FrameType(rf->h.type);
      if (type == FrameType::Sensor) {
        SensorMsg m;
        if (decode(rf->h, rf->payload.data(), m) && !using_mcu) eng->on_sensor(m);
      } else if (type == FrameType::Perimeter) {
        if (decode(rf->h, rf->payload.data(), *pm)) eng->on_perimeter(*pm);
      } else if (type == FrameType::Heartbeat) {
        HeartbeatMsg hb;
        if (!decode(rf->h, rf->payload.data(), hb)) continue;
        if (hb.tick == 0 && last_tick > 0) reset();
        // Advance through missed ticks so the members stay in step with the truth.
        Plan plan;
        bool decided = false;
        for (std::int64_t t = last_tick + 1; t <= std::int64_t(hb.tick); ++t) {
          decided = eng->tick(std::uint32_t(t), plan);
          const StageTimes& tm = eng->last_times();
          rec_filter.record(tm.filter_ns);
          if (decided) {
            rec_rollout.record(tm.rollout_ns);
            rec_policy.record(tm.policy_ns);
            rec_total.record(tm.total_ns);
          }
        }
        last_tick = std::max<std::int64_t>(last_tick, hb.tick);
        const StageTimes& tm = eng->last_times();
        if (decided) {
          DecisionMsg dm;
          dm.seq = out_seq++;
          dm.tick = hb.tick;
          dm.echo_t_send_ns = hb.t_send_ns;
          dm.plan = plan;
          dm.state = std::uint32_t(eng->health());
          dm.filter_ns = tm.filter_ns;
          dm.rollout_ns = tm.rollout_ns;
          dm.policy_ns = tm.policy_ns;
          dm.total_ns = tm.total_ns;
          tr->send(out.data(), encode(dm, out.data(), out.size()));
          if (eng->has_forecast()) {
            const ForecastOut& fc = eng->forecast();
            fm->seq = dm.seq;
            fm->tick = hb.tick;
            fm->n = std::uint16_t(sc->terrain.n);
            fm->ess = float(eng->ess());
            for (int c = 0; c < sc->terrain.cells(); ++c)
              fm->p_burn_u8[c] = std::uint8_t(std::lround(std::clamp(fc.p_burn[c], 0.f, 1.f) * 255.f));
            tr->send(out.data(), encode(*fm, out.data(), out.size()));
          }
          int n = std::snprintf(line, sizeof line,
                                "{\"tick\":%u,\"crews\":[%d,%d,%d,%d],\"eloss\":%.6g,\"cvar\":%.6g,"
                                "\"state\":%u,\"filter_ns\":%llu,\"rollout_ns\":%llu,\"policy_ns\":%llu}\n",
                                hb.tick, plan.crew_cluster[0], plan.crew_cluster[1], plan.crew_cluster[2],
                                plan.crew_cluster[3], double(plan.expected_loss), double(plan.cvar),
                                unsigned(eng->health()), (unsigned long long)tm.filter_ns,
                                (unsigned long long)tm.rollout_ns, (unsigned long long)tm.policy_ns);
          (void)n;
          log.push(line);
        }
        HeartbeatMsg rep;
        rep.seq = out_seq++;
        rep.tick = hb.tick;
        rep.t_send_ns = now_ns();
        rep.state = std::uint32_t(eng->health());
        if (rep.state == std::uint32_t(HealthState::Ok) && mcu_lfd >= 0 && mcu_lost)
          rep.state = std::uint32_t(HealthState::Degraded);
        tr->send(out.data(), encode(rep, out.data(), out.size()));
        if (mcu && mcu->connected()) {  // status for the MCU display
          rep.seq = mcu_seq++;
          mcu->send(out.data(), encode(rep, out.data(), out.size()));
          if (decided) {
            DecisionMsg dm;
            dm.tick = hb.tick;
            dm.plan = plan;
            dm.state = rep.state;
            mcu->send(out.data(), encode(dm, out.data(), out.size()));
          }
        }
        console.status(eng->health(), eng->plan().top_risk[0]);
      }
    }
    if (now_ns() - last_rx > std::uint64_t(stale_ms) * 1000000ull) {
      eng->set_link_down(true);
      console.status(HealthState::Stale, eng->plan().top_risk[0]);
    }
    if (!any) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  std::printf("[daemon] stopping\n");
  if (tr) {
    const auto& c = tr->counters();
    std::printf("[daemon] frames %llu bad_crc %llu bad_header %llu ring_full %llu\n",
                (unsigned long long)c.frames.load(), (unsigned long long)c.bad_crc.load(),
                (unsigned long long)c.bad_header.load(), (unsigned long long)c.ring_full.load());
  }
  if (!bench_out.empty()) {
    std::ofstream f(bench_out);
    f << "label,stage,ns\n";
    auto dump = [&](const char* s, const LatencyRecorder& r) {
      for (auto v : r.samples()) f << "daemon," << s << ',' << v << '\n';
    };
    dump("tick_filter", rec_filter);
    dump("decision_rollout", rec_rollout);
    dump("decision_policy", rec_policy);
    dump("decision_total", rec_total);
    std::printf("[daemon] wrote %s\n", bench_out.c_str());
  }
  close_fd(lfd);
  close_fd(mcu_lfd);
  return 0;
}
