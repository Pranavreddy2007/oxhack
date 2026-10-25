// embercast_sim_stream: laptop side of the live demo.
//
// Runs three truth simulators with the same seed (common random numbers):
//   lane "ensemble": EmberCast, on board_daemon over TCP (or in-process with --board none)
//   lane "b1":       deterministic baseline, in-process
//   lane "b0":       front-nearest baseline, in-process
// Streams one JSON line per tick to dashboard clients (TCP, --dash-port) and
// reads control lines from them: wind_shift, cut_link, restore_link, reset.
// Round-trip time is measured on the laptop clock: send HEARTBEAT -> receive
// the DECISION that echoes its t_send_ns.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <poll.h>
#include <vector>

#include "embercast/clock.hpp"
#include "embercast/engine.hpp"
#include "embercast/metrics.hpp"
#include "embercast/rng.hpp"
#include "embercast/transport.hpp"
#include "scenario.hpp"
#include "truth.hpp"

using namespace embercast;

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }

struct Lane {
  std::string name;
  std::unique_ptr<TruthSim> truth;
  std::unique_ptr<Engine> engine;  // null for the remote lane
  Plan plan;
  HealthState state = HealthState::Ok;
  std::vector<std::uint8_t> p_burn;  // 0..255
  bool has_p = false;
};

struct DashClient {
  int fd = -1;
  std::string inbuf;
  bool need_init = true;
};

void usage() {
  std::puts(
      "embercast_sim_stream [options]\n"
      "  --board HOST:PORT | none   (default 127.0.0.1:7070; none = in-process engine)\n"
      "  --seed N (default 1000)  --N N  --ticks N (default 120)  --rate HZ (default 4)\n"
      "  --K N --H N --D N  --threads N\n"
      "  --timeout-ms N   wait for the board reply (default 600)\n"
      "  --dash-port N    dashboard stream port (default 7072, 0 = off)\n"
      "  --episodes N     stop after N episodes (default 0 = run forever)\n"
      "  --fast           do not sleep between ticks (tests)\n"
      "  --rtt-out PATH   raw round-trip CSV (laptop clock)\n"
      "  --script FILE    timed controls: lines 'tick command'");
}

std::string hex_grid(const std::vector<std::uint8_t>& v) {
  static const char* h = "0123456789abcdef";
  std::string s(v.size() * 2, '0');
  for (std::size_t i = 0; i < v.size(); ++i) {
    s[2 * i] = h[v[i] >> 4];
    s[2 * i + 1] = h[v[i] & 15];
  }
  return s;
}

std::string state_grid(const std::vector<std::uint8_t>& st) {
  std::string s(st.size(), '0');
  for (std::size_t i = 0; i < st.size(); ++i)
    s[i] = st[i] == kUnburned ? '0' : (st[i] == kBurnt ? '2' : '1');
  return s;
}

const char* state_name(HealthState s) {
  static const char* names[] = {"OK", "STALE", "DEGRADED", "FAULT"};
  return names[int(s)];
}

}  // namespace

int main(int argc, char** argv) {
  std::string board = "127.0.0.1:7070", rtt_out, script_path;
  std::uint64_t seed = 1000;
  ScenarioOptions so;
  EngineConfig cfg;
  cfg.backend = "cpu_opt";
  cfg.threads = 0;
  double rate = 4.0;
  int timeout_ms = 600, dash_port = 7072, episodes = 0;
  bool fast = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { usage(); std::exit(2); }
      return argv[++i];
    };
    if (a == "--board") board = next();
    else if (a == "--seed") seed = std::stoull(next());
    else if (a == "--N") so.n = std::stoi(next());
    else if (a == "--ticks") so.ticks = std::stoi(next());
    else if (a == "--rate") rate = std::stod(next());
    else if (a == "--K") cfg.K = std::stoi(next());
    else if (a == "--H") cfg.H = std::stoi(next());
    else if (a == "--D") cfg.D = std::stoi(next());
    else if (a == "--threads") cfg.threads = std::stoi(next());
    else if (a == "--timeout-ms") timeout_ms = std::stoi(next());
    else if (a == "--dash-port") dash_port = std::stoi(next());
    else if (a == "--episodes") episodes = std::stoi(next());
    else if (a == "--fast") fast = true;
    else if (a == "--rtt-out") rtt_out = next();
    else if (a == "--script") script_path = next();
    else { usage(); return a == "--help" ? 0 : 2; }
  }
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  std::signal(SIGPIPE, SIG_IGN);

  const bool remote = board != "none";
  std::string bhost;
  int bport = 0;
  if (remote) {
    const auto colon = board.rfind(':');
    if (colon == std::string::npos) { usage(); return 2; }
    bhost = board.substr(0, colon);
    bport = std::stoi(board.substr(colon + 1));
  }
  std::vector<std::pair<int, std::string>> script;
  if (!script_path.empty()) {
    std::ifstream f(script_path);
    int t;
    std::string cmd;
    while (f >> t >> cmd) script.push_back({t, cmd});
  }
  std::printf("[sim_stream] %s  board %s  seed %llu\n", safety_notice(), board.c_str(),
              (unsigned long long)seed);
  std::fflush(stdout);

  const int dash_lfd = dash_port > 0 ? tcp_listen("127.0.0.1", dash_port) : -1;
  if (dash_port > 0 && dash_lfd < 0) {
    std::fprintf(stderr, "[sim_stream] cannot listen on dashboard port %d\n", dash_port);
    return 1;
  }
  std::vector<DashClient> dash;
  std::vector<std::uint64_t> rtt_samples, board_samples;
  rtt_samples.reserve(1 << 16);
  board_samples.reserve(1 << 16);

  Scenario sc = make_scenario(seed, so);
  std::vector<std::uint8_t> init(sc.terrain.cells());
  std::vector<Lane> lanes(3);
  bool link_cut = false;
  std::unique_ptr<StreamTransport> tr;
  std::uint64_t next_connect = 0, backoff_ms = 250;
  std::uint32_t seq = 0;
  std::vector<std::uint8_t> buf(kMaxFrame);
  auto rf = std::make_unique<RawFrame>();
  auto fm = std::make_unique<ForecastMsg>();
  double last_rtt_ms = -1, last_board_ms = -1, last_rollout_ms = -1;
  float ess = 0;
  float knob = 0.f;
  bool have_knob = false;

  auto start_episode = [&]() {
    sc = make_scenario(seed, so);
    initial_state(sc, init.data());
    const char* names[3] = {"ensemble", "b1", "b0"};
    const PolicyKind kinds[3] = {PolicyKind::Ensemble, PolicyKind::Deterministic, PolicyKind::FrontNearest};
    for (int i = 0; i < 3; ++i) {
      Lane& L = lanes[i];
      L.name = names[i];
      L.truth = std::make_unique<TruthSim>(sc);
      L.plan = Plan{};
      L.plan.num_crews = sc.crews;
      L.plan.crew_cluster.fill(kNoCluster);
      L.p_burn.assign(sc.terrain.cells(), 0);
      L.has_p = false;
      L.engine.reset();
      if (i > 0 || !remote) {
        EngineStatic es{&sc.terrain, &sc.mc, ThetaPrior{}, sc.crews, sc.travel_ticks, init.data()};
        EngineConfig c = engine_config_for(kinds[i], cfg);
        c.seed = splitmix64(seed ^ (0xB0A2Dull + std::uint64_t(i)));
        if (i == 0) c.seed = splitmix64(seed ^ 0xB0A2Dull);  // same seed as board_daemon
        L.engine = std::make_unique<Engine>(es, c);
      }
    }
    for (auto& d : dash) d.need_init = true;
  };
  start_episode();

  auto send_dash = [&](const std::string& s) {
    for (auto& d : dash) {
      if (d.fd < 0) continue;
      if (::send(d.fd, s.data(), s.size(), MSG_NOSIGNAL) < 0) {
        close_fd(d.fd);
        d.fd = -1;
      }
    }
  };
  auto init_json = [&]() {
    std::string s = "{\"type\":\"init\",\"n\":" + std::to_string(sc.terrain.n) + ",\"seed\":" +
                     std::to_string(seed) + ",\"ticks\":" + std::to_string(sc.ticks) +
                     ",\"crews\":" + std::to_string(sc.crews) + ",\"remote\":" + (remote ? "true" : "false") +
                     ",\"fuel\":\"";
    for (int c = 0; c < sc.terrain.cells(); ++c) s += char('0' + sc.terrain.fuel[c]);
    s += "\",\"cluster\":\"";
    for (int c = 0; c < sc.terrain.cells(); ++c)
      s += sc.terrain.cluster[c] == kNoCluster ? '.' : char('a' + sc.terrain.cluster[c]);
    s += "\",\"clusters\":[";
    for (int j = 0; j < sc.terrain.num_clusters; ++j) {
      char b[160];
      std::snprintf(b, sizeof b, "%s{\"x\":%.2f,\"y\":%.2f,\"value\":%.1f}", j ? "," : "",
                    double(sc.terrain.cluster_cx[j]), double(sc.terrain.cluster_cy[j]),
                    double(sc.terrain.cluster_value[j]));
      s += b;
    }
    s += "]}\n";
    return s;
  };

  int episode = 0;
  std::uint32_t t = 0;
  auto next_tick_at = std::chrono::steady_clock::now();
  while (!g_stop.load()) {
    // Dashboard connections and controls.
    if (dash_lfd >= 0) {
      const int fd = accept_timeout(dash_lfd, 0);
      if (fd >= 0) dash.push_back({fd, "", true});
      for (auto& d : dash) {
        if (d.fd < 0) continue;
        pollfd p{d.fd, POLLIN, 0};
        while (::poll(&p, 1, 0) == 1) {
          char b[512];
          const ssize_t r = ::recv(d.fd, b, sizeof b, 0);
          if (r <= 0) {
            close_fd(d.fd);
            d.fd = -1;
            break;
          }
          d.inbuf.append(b, std::size_t(r));
        }
        std::size_t nl;
        while ((nl = d.inbuf.find('\n')) != std::string::npos) {
          const std::string cmd = d.inbuf.substr(0, nl);
          d.inbuf.erase(0, nl + 1);
          script.push_back({int(t), cmd});
        }
        if (d.need_init && d.fd >= 0) {
          const std::string s = init_json();
          if (::send(d.fd, s.data(), s.size(), MSG_NOSIGNAL) < 0) {
            close_fd(d.fd);
            d.fd = -1;
          }
          d.need_init = false;
        }
      }
      dash.erase(std::remove_if(dash.begin(), dash.end(), [](const DashClient& d) { return d.fd < 0; }),
                 dash.end());
    }
    // Controls due at this tick.
    for (auto it = script.begin(); it != script.end();) {
      if (it->first > int(t)) {
        ++it;
        continue;
      }
      const std::string& c = it->second;
      if (c == "wind_shift")
        for (auto& L : lanes) L.truth->add_wind_shift(1.5707963f);
      else if (c == "cut_link") {
        link_cut = true;
        if (tr) tr->close();
      } else if (c == "restore_link") link_cut = false;
      else if (c == "reset") t = std::uint32_t(sc.ticks);  // ends the episode now
      std::printf("[sim_stream] tick %u control %s\n", t, c.c_str());
      std::fflush(stdout);
      it = script.erase(it);
    }
    if (t >= std::uint32_t(sc.ticks)) {
      std::printf("[sim_stream] episode %d done: loss ensemble %.1f  b1 %.1f  b0 %.1f\n", episode,
                  lanes[0].truth->total_loss(), lanes[1].truth->total_loss(), lanes[2].truth->total_loss());
      std::fflush(stdout);
      ++episode;
      if (episodes > 0 && episode >= episodes) break;
      start_episode();
      t = 0;
    }

    // Remote link management with backoff.
    if (remote && !link_cut && (!tr || !tr->connected())) {
      const std::uint64_t now = now_ns();
      if (now >= next_connect) {
        tr.reset();
        const int fd = tcp_connect(bhost, bport, 100);
        if (fd >= 0) {
          tr = std::make_unique<StreamTransport>(fd);
          backoff_ms = 250;
          std::printf("[sim_stream] connected to board %s\n", board.c_str());
        } else {
          next_connect = now + backoff_ms * 1000000ull;
          backoff_ms = std::min<std::uint64_t>(backoff_ms * 2, 4000);
        }
        std::fflush(stdout);
      }
    }

    // One tick for each lane.
    std::vector<SensorMsg> sens;
    std::vector<PerimeterMsg> scans;
    for (int i = 0; i < 3; ++i) {
      Lane& L = lanes[i];
      sens.clear();
      scans.clear();
      L.truth->collect(t, sens, scans);
      Plan p;
      bool decided = false;
      if (L.engine) {
        for (auto& m : sens) L.engine->on_sensor(m);
        for (auto& m : scans) L.engine->on_perimeter(m);
        decided = L.engine->tick(t, p);
        L.state = L.engine->health();
        if (decided && L.engine->has_forecast()) {
          const auto& fc = L.engine->forecast();
          for (int c = 0; c < sc.terrain.cells(); ++c)
            L.p_burn[c] = std::uint8_t(std::lround(std::clamp(fc.p_burn[c], 0.f, 1.f) * 255.f));
          L.has_p = true;
          if (i == 0) {
            ess = float(L.engine->ess());
            last_board_ms = L.engine->last_times().total_ns / 1e6;
            last_rollout_ms = L.engine->last_times().rollout_ns / 1e6;
          }
        }
      } else {
        // Remote lane.
        bool got_reply = false;
        if (tr && tr->connected() && !link_cut) {
          for (auto& m : sens) {
            m.t_send_ns = now_ns();
            tr->send(buf.data(), encode(m, buf.data(), buf.size()));
          }
          for (auto& m : scans) {
            m.t_send_ns = now_ns();
            tr->send(buf.data(), encode(m, buf.data(), buf.size()));
          }
          HeartbeatMsg hb;
          hb.seq = seq++;
          hb.tick = t;
          hb.t_send_ns = now_ns();
          tr->send(buf.data(), encode(hb, buf.data(), buf.size()));
          const std::uint64_t deadline = now_ns() + std::uint64_t(timeout_ms) * 1000000ull;
          while (!got_reply && now_ns() < deadline && tr->connected()) {
            if (!tr->pop(*rf)) {
              std::this_thread::sleep_for(std::chrono::microseconds(50));
              continue;
            }
            const auto type = FrameType(rf->h.type);
            if (type == FrameType::Sensor) {  // MCU knob frame relayed by the daemon
              SensorMsg km;
              if (decode(rf->h, rf->payload.data(), km) && (km.flags & 0x80000000u)) {
                if (!have_knob) {
                  knob = km.wind_dir;
                  have_knob = true;
                } else if (std::abs(km.wind_dir - knob) > 0.35f) {
                  for (auto& LL : lanes) LL.truth->add_wind_shift(km.wind_dir - knob);
                  std::printf("[sim_stream] tick %u knob wind shift %.2f rad\n", t, double(km.wind_dir - knob));
                  knob = km.wind_dir;
                }
              }
              continue;
            }
            if (type == FrameType::Decision) {
              DecisionMsg dm;
              if (decode(rf->h, rf->payload.data(), dm) && dm.tick == t) {
                p = dm.plan;
                decided = true;
                const std::uint64_t rtt = rf->t_recv_ns - dm.echo_t_send_ns;
                rtt_samples.push_back(rtt);
                board_samples.push_back(dm.total_ns);
                last_rtt_ms = rtt / 1e6;
                last_board_ms = dm.total_ns / 1e6;
                last_rollout_ms = dm.rollout_ns / 1e6;
              }
            } else if (type == FrameType::Forecast) {
              if (decode(rf->h, rf->payload.data(), *fm) && fm->n == sc.terrain.n) {
                std::copy(fm->p_burn_u8.begin(), fm->p_burn_u8.begin() + sc.terrain.cells(), L.p_burn.begin());
                L.has_p = true;
                ess = fm->ess;
              }
            } else if (type == FrameType::Heartbeat) {
              HeartbeatMsg rep;
              if (decode(rf->h, rf->payload.data(), rep) && rep.tick == t) {
                got_reply = true;
                L.state = HealthState(rep.state);
              }
            }
          }
        }
        if (!got_reply) L.state = HealthState::Stale;  // keep the last plan
      }
      if (decided) {
        L.truth->set_plan(p);
        L.plan = p;
      }
    }

    // Dashboard frame (visualisation only; not part of any measurement).
    if (!dash.empty()) {
      std::string s = "{\"type\":\"tick\",\"tick\":" + std::to_string(t) + ",\"link_cut\":" +
                      (link_cut ? "true" : "false");
      char b[256];
      const Weather& w = lanes[0].truth->weather();
      std::snprintf(b, sizeof b, ",\"wind_dir\":%.3f,\"wind_speed\":%.2f,\"rtt_ms\":%.3f,\"board_ms\":%.3f,\"rollout_ms\":%.3f,\"ess\":%.2f",
                    double(w.wind_dir), double(w.wind_speed), last_rtt_ms, last_board_ms, last_rollout_ms, double(ess));
      s += b;
      s += ",\"lanes\":{";
      for (int i = 0; i < 3; ++i) {
        const Lane& L = lanes[i];
        std::snprintf(b, sizeof b, "%s\"%s\":{\"loss\":%.2f,\"state\":\"%s\",\"eloss\":%.2f,\"cvar\":%.2f,\"crews\":[",
                      i ? "," : "", L.name.c_str(), L.truth->total_loss(), state_name(L.state),
                      double(L.plan.expected_loss), double(L.plan.cvar));
        s += b;
        for (int c = 0; c < L.plan.num_crews; ++c) {
          s += c ? "," : "";
          s += std::to_string(int(L.plan.crew_cluster[c]));
        }
        s += "],\"fire\":\"" + state_grid(L.truth->state()) + "\"";
        if (L.has_p) s += ",\"p\":\"" + hex_grid(L.p_burn) + "\"";
        s += "}";
      }
      s += "}}\n";
      send_dash(s);
    }

    for (auto& L : lanes) L.truth->step();
    ++t;
    if (!fast) {
      next_tick_at += std::chrono::microseconds(std::int64_t(1e6 / rate));
      std::this_thread::sleep_until(next_tick_at);
      if (std::chrono::steady_clock::now() > next_tick_at + std::chrono::seconds(1))
        next_tick_at = std::chrono::steady_clock::now();
    }
  }
  if (!rtt_out.empty()) {
    std::ofstream f(rtt_out);
    f << "label,stage,ns\n";
    for (std::size_t i = 0; i < rtt_samples.size(); ++i) {
      f << "laptop,round_trip," << rtt_samples[i] << '\n';
      f << "laptop,board_engine," << board_samples[i] << '\n';
      f << "laptop,transport," << (rtt_samples[i] - std::min(rtt_samples[i], board_samples[i])) << '\n';
    }
    std::printf("[sim_stream] wrote %zu round trips to %s\n", rtt_samples.size(), rtt_out.c_str());
  }
  if (tr) {
    const auto& c = tr->counters();
    std::printf("[sim_stream] frames %llu bad_crc %llu bad_header %llu\n", (unsigned long long)c.frames.load(),
                (unsigned long long)c.bad_crc.load(), (unsigned long long)c.bad_header.load());
  }
  return 0;
}
