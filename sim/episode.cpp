#include "episode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>

#include "embercast/clock.hpp"
#include "embercast/rng.hpp"
#include "truth.hpp"

namespace embercast {

bool parse_policy(const std::string& s, PolicyKind& k) {
  for (PolicyKind v : {PolicyKind::Ensemble, PolicyKind::Deterministic, PolicyKind::FrontNearest})
    if (s == policy_kind_name(v)) {
      k = v;
      return true;
    }
  return false;
}

namespace {

struct Snapshot {
  std::uint32_t t0 = 0;
  std::vector<float> p_burn;
  std::vector<std::uint8_t> eval_mask;  // cells to evaluate
  std::vector<double> area_w;           // member weights
  std::vector<int> area;                // member areas
  std::array<float, kMaxClusters> p_lost{};
  std::array<double, kMaxClusters> closs0{};
};

// Cells unburned at t0 within `r` (Chebyshev) of the true footprint.
void eval_mask(const std::vector<std::uint8_t>& st, int n, int r, std::vector<std::uint8_t>& out) {
  std::vector<std::uint8_t> row(st.size(), 0);
  out.assign(st.size(), 0);
  for (int y = 0; y < n; ++y) {
    int last = -100000;
    for (int x = 0; x < n; ++x) {
      if (st[y * n + x] != kUnburned) last = x;
      if (x - last <= r) row[y * n + x] = 1;
    }
    last = 100000;
    for (int x = n - 1; x >= 0; --x) {
      if (st[y * n + x] != kUnburned) last = x;
      if (last - x <= r) row[y * n + x] = 1;
    }
  }
  for (int x = 0; x < n; ++x) {
    int last = -100000;
    for (int y = 0; y < n; ++y) {
      if (row[y * n + x]) last = y;
      if (y - last <= r) out[y * n + x] = 1;
    }
    last = 100000;
    for (int y = n - 1; y >= 0; --y) {
      if (row[y * n + x]) last = y;
      if (last - y <= r) out[y * n + x] = 1;
    }
  }
  for (std::size_t c = 0; c < st.size(); ++c)
    if (st[c] != kUnburned) out[c] = 0;
}

void score(const Snapshot& s, const TruthSim& truth, PredMetrics& pm) {
  const auto& st = truth.state();
  const Terrain& t = truth.scenario().terrain;
  for (std::size_t c = 0; c < st.size(); ++c) {
    if (!s.eval_mask[c]) continue;
    const double y = st[c] != kUnburned ? 1.0 : 0.0;
    const double p = std::clamp(double(s.p_burn[c]), 0.0, 1.0);
    const double pc = std::clamp(p, 1e-4, 1 - 1e-4);
    pm.brier += (p - y) * (p - y);
    pm.logloss += -(y * std::log(pc) + (1 - y) * std::log(1 - pc));
    const int b = std::min(9, int(p * 10));
    pm.bin_p[b] += p;
    pm.bin_y[b] += y;
    ++pm.bin_n[b];
    ++pm.n;
  }
  // Burned-area CRPS and spread-skill.
  const double ya = truth.burned_area();
  const int K = int(s.area.size());
  double a1 = 0, a2 = 0, mean = 0;
  for (int i = 0; i < K; ++i) {
    a1 += s.area_w[i] * std::abs(s.area[i] - ya);
    mean += s.area_w[i] * s.area[i];
    for (int j = 0; j < K; ++j) a2 += s.area_w[i] * s.area_w[j] * std::abs(s.area[i] - s.area[j]);
  }
  double var = 0;
  for (int i = 0; i < K; ++i) var += s.area_w[i] * (s.area[i] - mean) * (s.area[i] - mean);
  pm.crps += a1 - 0.5 * a2;
  ++pm.crps_n;
  pm.var_sum += var;
  pm.sqerr_sum += (mean - ya) * (mean - ya);
  for (int j = 0; j < t.num_clusters; ++j) {
    const double lost = truth.cluster_loss()[j] - s.closs0[j];
    const double y = lost >= kLostFraction * t.cluster_value[j] && lost > 0 ? 1.0 : 0.0;
    pm.lost_brier += (s.p_lost[j] - y) * (s.p_lost[j] - y);
    ++pm.lost_n;
  }
}

Plan oracle_plan(const TruthSim& truth, const Plan& prev, const EngineConfig& cfg) {
  const Scenario& sc = truth.scenario();
  TruthSim fut = truth;
  fut.set_emit_obs(false);
  fut.clear_crews();
  const auto c0 = fut.cluster_loss();
  for (int h = 0; h < cfg.H; ++h) fut.step();
  std::array<float, kMaxClusters> l{};
  for (int j = 0; j < sc.terrain.num_clusters; ++j) l[j] = float(fut.cluster_loss()[j] - c0[j]);
  const double w = 1.0;
  RiskParams rp = cfg.risk;
  rp.lambda = 0.0;
  const GreedyResult g = greedy_allocate(l.data(), &w, 1, sc.terrain.num_clusters, sc.crews,
                                         sc.mc, rp, counts_from_plan(prev));
  Plan p = counts_to_plan(g.counts, prev, sc.crews, sc.terrain.num_clusters, truth.tick());
  p.expected_loss = float(g.eloss);
  p.cvar = float(g.cvar);
  return p;
}

}  // namespace

EpisodeResult run_episode(std::uint64_t seed, const std::string& policy,
                          const EpisodeOptions& opt) {
  const Scenario sc = make_scenario(seed, opt.scenario);
  EpisodeResult r;
  r.seed = seed;
  r.policy = policy;
  r.stress = stress_name(sc.stress);
  for (int j = 0; j < sc.terrain.num_clusters; ++j) r.total_value += sc.terrain.cluster_value[j];

  TruthSim truth(sc);
  std::vector<std::uint8_t> init(sc.terrain.cells());
  initial_state(sc, init.data());

  const bool oracle = policy == "oracle";
  std::unique_ptr<Engine> eng;
  if (!oracle) {
    PolicyKind k;
    if (!parse_policy(policy, k)) throw std::invalid_argument("unknown policy: " + policy);
    EngineConfig cfg = engine_config_for(k, opt.engine);
    cfg.seed = splitmix64(seed ^ 0xE3BE7CA57ull);
    EngineStatic es;
    es.terrain = &sc.terrain;
    es.mc = &sc.mc;
    es.prior = ThetaPrior{};
    es.crews = sc.crews;
    es.travel_ticks = sc.travel_ticks;
    es.init_state = init.data();
    eng = std::make_unique<Engine>(es, cfg);
  }
  const EngineConfig& cfg = eng ? eng->config() : opt.engine;
  r.plans.reserve(std::size_t(sc.ticks / std::max(1, cfg.D) + 2));
  r.filter_ns.reserve(std::size_t(sc.ticks));
  r.rollout_ns.reserve(std::size_t(sc.ticks));
  r.policy_ns.reserve(std::size_t(sc.ticks));
  r.decision_ns.reserve(std::size_t(sc.ticks));

  std::deque<Snapshot> pending;
  std::vector<SensorMsg> sens;
  std::vector<PerimeterMsg> scans;
  Plan prev;
  prev.num_crews = sc.crews;
  prev.crew_cluster.fill(kNoCluster);

  for (int t = 0; t < sc.ticks; ++t) {
    const std::uint32_t ut = std::uint32_t(t);
    sens.clear();
    scans.clear();
    truth.collect(ut, sens, scans);
    Plan p;
    bool decided = false;
    if (eng) {
      for (const auto& m : sens) eng->on_sensor(m);
      for (const auto& m : scans) eng->on_perimeter(m);
      decided = eng->tick(ut, p);
      const StageTimes& tm = eng->last_times();
      r.filter_ns.push_back(tm.filter_ns);
      if (decided) {
        if (t == 0) r.first_decision_ns = tm.total_ns;
        else {
          r.rollout_ns.push_back(tm.rollout_ns);
          r.policy_ns.push_back(tm.policy_ns);
          r.decision_ns.push_back(tm.total_ns);
        }
      }
    } else if (t % cfg.D == 0) {
      p = oracle_plan(truth, prev, cfg);
      decided = true;
    }
    if (decided) {
      truth.set_plan(p);
      r.plans.push_back(p);
      prev = p;
    }
    if (opt.collect_pred && eng && decided && eng->has_forecast() && t + cfg.H <= sc.ticks) {
      const ForecastOut& fc = eng->forecast();
      Snapshot s;
      s.t0 = ut;
      s.p_burn = fc.p_burn;
      eval_mask(truth.state(), sc.terrain.n, opt.eval_radius, s.eval_mask);
      s.area.assign(fc.area.begin(), fc.area.begin() + fc.K);
      s.area_w = eng->weights();
      s.p_lost = {};
      std::copy(fc.p_lost.begin(), fc.p_lost.begin() + kMaxClusters, s.p_lost.begin());
      s.closs0 = truth.cluster_loss();
      pending.push_back(std::move(s));
    }
    truth.step();
    while (!pending.empty() && pending.front().t0 + std::uint32_t(cfg.H) == truth.tick()) {
      score(pending.front(), truth, r.pm);
      pending.pop_front();
    }
  }
  r.loss = truth.total_loss();
  r.final_area = truth.burned_area();
  if (eng) {
    r.counters = eng->counters();
    r.resamples = eng->resamples();
  }
  return r;
}

std::string decision_log(const EpisodeResult& r) {
  std::string out;
  char buf[512];
  for (const Plan& p : r.plans) {
    int n = std::snprintf(buf, sizeof buf, "{\"seed\":%llu,\"policy\":\"%s\",\"tick\":%u,\"crews\":[",
                          static_cast<unsigned long long>(r.seed), r.policy.c_str(), p.tick);
    out.append(buf, std::size_t(n));
    for (int i = 0; i < p.num_crews; ++i) {
      n = std::snprintf(buf, sizeof buf, "%s%d", i ? "," : "", int(p.crew_cluster[i]));
      out.append(buf, std::size_t(n));
    }
    n = std::snprintf(buf, sizeof buf, "],\"eloss\":%.9g,\"cvar\":%.9g,\"moved\":%d}\n",
                      double(p.expected_loss), double(p.cvar), p.moved);
    out.append(buf, std::size_t(n));
  }
  return out;
}

}  // namespace embercast
