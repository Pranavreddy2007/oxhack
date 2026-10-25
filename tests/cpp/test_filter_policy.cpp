#include <cmath>
#include <numeric>
#include <vector>

#include "doctest.h"
#include "embercast/engine.hpp"
#include "embercast/filter.hpp"
#include "embercast/policy.hpp"
#include "episode.hpp"
#include "scenario.hpp"

using namespace embercast;

TEST_CASE("systematic resampling keeps heavy members") {
  const double w[4] = {0.0, 0.7, 0.3, 0.0};
  int idx[4];
  systematic_resample(w, 4, 0.5, idx);
  int c1 = 0, c2 = 0;
  for (int i : idx) {
    CHECK((i == 1 || i == 2));
    c1 += i == 1;
    c2 += i == 2;
  }
  CHECK(c1 >= 2);
  CHECK(c2 >= 1);
}

TEST_CASE("weighted CVaR") {
  const double loss[4] = {1, 2, 3, 10};
  const double w[4] = {0.25, 0.25, 0.25, 0.25};
  int order[4];
  CHECK(weighted_cvar(loss, w, 4, 0.75, order) == doctest::Approx(10.0));
  CHECK(weighted_cvar(loss, w, 4, 0.5, order) == doctest::Approx(6.5));
  CHECK(weighted_cvar(loss, w, 4, 0.0, order) == doctest::Approx(4.0));  // mean
  const double w2[4] = {0.1, 0.1, 0.1, 0.7};
  CHECK(weighted_cvar(loss, w2, 4, 0.95, order) == doctest::Approx(10.0));
}

TEST_CASE("greedy allocation: more crews never increase modelled member loss") {
  ModelConst mc;
  std::vector<float> l(std::size_t(3) * kMaxClusters, 0.f);
  l[0 * kMaxClusters + 0] = 50;
  l[0 * kMaxClusters + 1] = 5;
  l[1 * kMaxClusters + 0] = 10;
  l[1 * kMaxClusters + 1] = 80;
  l[2 * kMaxClusters + 2] = 20;
  const double w[3] = {0.5, 0.3, 0.2};
  RiskParams rp;
  rp.switch_cost = 0;
  CrewCounts prev{};
  double last = 1e30;
  for (int C = 0; C <= 6; ++C) {
    const auto g = greedy_allocate(l.data(), w, 3, 3, C, mc, rp, prev);
    int total = 0;
    for (int j = 0; j < 3; ++j) total += g.counts[j];
    CHECK(total == C);
    CHECK(g.eloss <= last + 1e-9);
    last = g.eloss;
  }
  // Per member: adding a crew to any cluster never increases that member's loss.
  for (int n = 0; n < kMaxCrews; ++n)
    CHECK(1.0 - crew_efficacy(mc, n + 1) <= 1.0 - crew_efficacy(mc, n));
}

TEST_CASE("risk aversion moves crews to the tail") {
  ModelConst mc;
  // Member 0 (p=0.9): small loss at cluster 0. Member 1 (p=0.1): huge loss at cluster 1.
  std::vector<float> l(std::size_t(2) * kMaxClusters, 0.f);
  l[0 * kMaxClusters + 0] = 20;
  l[1 * kMaxClusters + 1] = 150;
  const double w[2] = {0.9, 0.1};
  RiskParams neutral, averse;
  neutral.lambda = 0;
  neutral.switch_cost = averse.switch_cost = 0;
  averse.lambda = 2.0;
  averse.alpha = 0.9;
  CrewCounts prev{};
  const auto a = greedy_allocate(l.data(), w, 2, 2, 1, mc, neutral, prev);
  const auto b = greedy_allocate(l.data(), w, 2, 2, 1, mc, averse, prev);
  CHECK(a.counts[0] == 1);
  CHECK(b.counts[1] == 1);
}

TEST_CASE("switching penalty keeps crews in place") {
  ModelConst mc;
  std::vector<float> l(kMaxClusters, 0.f);
  l[0] = 10;
  l[1] = 11;
  const double w = 1;
  RiskParams rp;
  rp.lambda = 0;
  rp.switch_cost = 5;
  CrewCounts prev{};
  prev[0] = 1;
  CHECK(greedy_allocate(l.data(), &w, 1, 2, 1, mc, rp, prev).counts[0] == 1);
  rp.switch_cost = 0;
  CHECK(greedy_allocate(l.data(), &w, 1, 2, 1, mc, rp, prev).counts[1] == 1);
}

TEST_CASE("counts_to_plan keeps crews where possible") {
  Plan prev;
  prev.num_crews = 3;
  prev.crew_cluster = {2, 1, kNoCluster};
  CrewCounts c{};
  c[1] = 2;
  c[3] = 1;
  const Plan p = counts_to_plan(c, prev, 3, 4, 10);
  CHECK(p.crew_cluster[1] == 1);
  CHECK(counts_from_plan(p)[1] == 2);
  CHECK(counts_from_plan(p)[3] == 1);
  CHECK(p.moved == 2);
}

TEST_CASE("filter: weights sum to 1, ESS in [1, K], probabilities in [0,1]") {
  ScenarioOptions so;
  so.n = 48;
  const Scenario sc = make_scenario(21, so);
  std::vector<std::uint8_t> init(sc.terrain.cells());
  initial_state(sc, init.data());
  auto be = make_backend("cpu_naive");
  be->prepare(32, sc.terrain.cells());
  ParticleFilter pf;
  pf.init(sc.terrain, sc.mc, ThetaPrior{}, FilterConfig{}, 32, 5, init.data(), false);
  PerimeterMsg m;
  m.n = std::uint16_t(sc.terrain.n);
  for (std::uint32_t t = 1; t <= 12; ++t) {
    pf.propagate(*be, sc.truth.weather0, CrewCounts{}, t, false);
    if (t % 4 == 0) {
      for (int c = 0; c < sc.terrain.cells(); c += 3) bit_set(m.observed, c);
      bit_set(m.fire, sc.ignition_cell);
      pf.update(m, t);
      const auto& w = pf.ensemble().weight;
      CHECK(std::accumulate(w.begin(), w.end(), 0.0) == doctest::Approx(1.0));
      CHECK(pf.ess() >= 1.0 - 1e-9);
      CHECK(pf.ess() <= 32.0 + 1e-9);
    }
  }
  ForecastOut out;
  out.allocate(32, sc.terrain.cells());
  StepArgs s;
  s.terrain = &sc.terrain;
  s.mc = &sc.mc;
  s.seed = 9;
  s.tick = 13;
  pf.build_tables(sc.truth.weather0);
  be->rollout(pf.ensemble(), s, 10, out);
  for (float p : out.p_burn) {
    CHECK(p >= 0.f);
    CHECK(p <= 1.0001f);
  }
  for (int j = 0; j < sc.terrain.num_clusters; ++j) {
    CHECK(out.p_lost[j] >= 0.f);
    CHECK(out.p_lost[j] <= 1.0001f);
  }
}

namespace {
struct EngineFixture {
  Scenario sc;
  std::vector<std::uint8_t> init;
  std::unique_ptr<Engine> eng;
  explicit EngineFixture(PolicyKind k = PolicyKind::Ensemble) {
    ScenarioOptions so;
    so.n = 32;
    sc = make_scenario(5, so);
    init.resize(sc.terrain.cells());
    initial_state(sc, init.data());
    EngineConfig cfg;
    cfg.K = 8;
    cfg.H = 5;
    EngineStatic es{&sc.terrain, &sc.mc, ThetaPrior{}, sc.crews, sc.travel_ticks, init.data()};
    eng = std::make_unique<Engine>(es, engine_config_for(k, cfg));
  }
  SensorMsg sensor(std::uint32_t seq, std::uint32_t tick) {
    SensorMsg m;
    m.seq = seq;
    m.tick = tick;
    m.wind_speed = 5.f + 0.1f * float(seq);
    m.wind_dir = 0.3f;
    m.humidity = 0.3f;
    m.temp = 20.f;
    return m;
  }
};
}  // namespace

TEST_CASE("engine: missing sensor data -> STALE, continues to plan") {
  EngineFixture f;
  Plan p;
  f.eng->on_sensor(f.sensor(0, 0));
  CHECK(f.eng->tick(0, p));
  CHECK(f.eng->health() == HealthState::Ok);
  for (std::uint32_t t = 1; t <= 10; ++t) f.eng->tick(t, p);  // no more sensor frames
  CHECK(f.eng->health() == HealthState::Stale);
  CHECK(f.eng->has_forecast());
  f.eng->on_sensor(f.sensor(1, 11));
  f.eng->tick(11, p);
  CHECK(f.eng->health() == HealthState::Ok);
}

TEST_CASE("engine: out-of-range, flagged, duplicate, late and stuck frames are dropped and counted") {
  EngineFixture f;
  Plan p;
  f.eng->on_sensor(f.sensor(0, 0));
  f.eng->tick(0, p);
  auto bad = f.sensor(1, 1);
  bad.humidity = 1.7f;
  f.eng->on_sensor(bad);
  auto flagged = f.sensor(2, 1);
  flagged.flags = kFlagOutOfRange;
  f.eng->on_sensor(flagged);
  f.eng->on_sensor(f.sensor(2, 1));  // a dropped frame does not consume its seq, so this one is accepted
  f.eng->on_sensor(f.sensor(2, 1));  // duplicate
  f.eng->tick(1, p);
  CHECK(f.eng->counters().sensor_out_of_range == 1);
  CHECK(f.eng->counters().sensor_flagged == 1);
  CHECK(f.eng->counters().sensor_dup == 1);
  // Late frame: older than max_late_ticks.
  f.eng->on_sensor(f.sensor(3, 1));
  f.eng->tick(20, p);
  CHECK(f.eng->counters().sensor_late == 1);
  // Stuck: identical values three times.
  for (std::uint32_t s = 4; s < 8; ++s) {
    auto m = f.sensor(4, 21);
    m.seq = s;
    f.eng->on_sensor(m);
  }
  f.eng->tick(21, p);
  CHECK(f.eng->counters().sensor_stuck == 2);
}

TEST_CASE("engine: late and duplicate perimeter scans") {
  EngineFixture f;
  Plan p;
  PerimeterMsg m;
  m.n = std::uint16_t(f.sc.terrain.n);
  m.seq = 0;
  m.tick = 0;
  bit_set(m.observed, 0);
  f.eng->tick(0, p);
  f.eng->on_perimeter(m);
  f.eng->on_perimeter(m);  // duplicate
  f.eng->tick(1, p);
  CHECK(f.eng->counters().scans_ok == 1);
  CHECK(f.eng->counters().scans_dup == 1);
  m.seq = 1;
  m.tick = 2;
  f.eng->on_perimeter(m);
  f.eng->tick(30, p);
  CHECK(f.eng->counters().scans_late == 1);
}

TEST_CASE("AC1: deterministic replay gives byte-identical decision logs") {
  EpisodeOptions o;
  o.scenario.n = 48;
  o.scenario.ticks = 40;
  o.engine.K = 16;
  o.engine.H = 8;
  for (const char* pol : {"ensemble", "deterministic", "front_nearest", "oracle"}) {
    const auto a = decision_log(run_episode(31, pol, o));
    const auto b = decision_log(run_episode(31, pol, o));
    CHECK(!a.empty());
    CHECK(a == b);
  }
}
