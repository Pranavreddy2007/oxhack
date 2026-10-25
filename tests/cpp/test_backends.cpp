#include <cstring>
#include <vector>

#include "doctest.h"
#include "embercast/filter.hpp"
#include "embercast/forecast.hpp"
#include "embercast/thread_pool.hpp"
#include "episode.hpp"
#include "scenario.hpp"

using namespace embercast;

namespace {

struct Run {
  std::vector<std::uint8_t> states;
  std::vector<float> p_burn, loss, p_lost;
  std::vector<int> area;
};

Run run_backend(const char* name, int threads, std::uint64_t seed) {
  ScenarioOptions so;
  so.n = 64;
  const Scenario sc = make_scenario(seed, so);
  std::vector<std::uint8_t> init(sc.terrain.cells());
  initial_state(sc, init.data());
  auto be = make_backend(name, threads);
  const int K = 24;
  be->prepare(K, sc.terrain.cells());
  ParticleFilter pf;
  pf.init(sc.terrain, sc.mc, ThetaPrior{}, FilterConfig{}, K, seed, init.data(), false);
  CrewCounts crews{};
  crews[0] = 2;
  crews[1] = 1;
  ForecastOut out;
  out.allocate(K, sc.terrain.cells());
  Run r;
  for (std::uint32_t t = 1; t <= 30; ++t) {
    pf.propagate(*be, sc.truth.weather0, crews, t, false);
    if (t % 4 == 0) {
      PerimeterMsg m;
      m.n = std::uint16_t(sc.terrain.n);
      for (int c = 0; c < sc.terrain.cells(); c += 2) bit_set(m.observed, c);
      bit_set(m.fire, sc.ignition_cell);
      pf.update(m, t);
    }
    if (t % 10 == 0) {
      StepArgs s;
      s.terrain = &sc.terrain;
      s.mc = &sc.mc;
      s.crews = crews;
      s.seed = seed + 1;
      s.tick = t + 1;
      pf.build_tables(sc.truth.weather0);
      be->rollout(pf.ensemble(), s, 15, out);
      r.p_burn.insert(r.p_burn.end(), out.p_burn.begin(), out.p_burn.end());
      r.loss.insert(r.loss.end(), out.loss.begin(), out.loss.end());
      r.p_lost.insert(r.p_lost.end(), out.p_lost.begin(), out.p_lost.end());
      r.area.insert(r.area.end(), out.area.begin(), out.area.end());
    }
  }
  r.states = pf.ensemble().state;
  return r;
}

bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

}  // namespace

TEST_CASE("AC2: cpu_naive and cpu_opt give bit-identical outputs") {
  for (std::uint64_t seed : {101ull, 202ull, 303ull}) {
    const Run a = run_backend("cpu_naive", 1, seed);
    for (int threads : {1, 3}) {
      const Run b = run_backend("cpu_opt", threads, seed);
      CHECK(a.states == b.states);
      CHECK(same_bits(a.p_burn, b.p_burn));
      CHECK(same_bits(a.loss, b.loss));
      CHECK(same_bits(a.p_lost, b.p_lost));
      CHECK(a.area == b.area);
    }
  }
}

TEST_CASE("AC2: full episodes give identical decision logs on both backends") {
  EpisodeOptions o;
  o.scenario.n = 48;
  o.scenario.ticks = 40;
  o.engine.K = 16;
  o.engine.H = 8;
  o.engine.backend = "cpu_naive";
  const auto a = decision_log(run_episode(41, "ensemble", o));
  o.engine.backend = "cpu_opt";
  o.engine.threads = 2;
  const auto b = decision_log(run_episode(41, "ensemble", o));
  CHECK(a == b);
}

#ifdef EMBERCAST_HAVE_OPENCL
TEST_CASE("AC2: opencl gives bit-identical outputs to cpu_naive (skipped without a device)") {
  try {
    make_backend("opencl");
  } catch (const std::exception& e) {
    MESSAGE("opencl skipped: " << e.what());
    return;
  }
  for (std::uint64_t seed : {101ull, 202ull, 303ull}) {
    const Run a = run_backend("cpu_naive", 1, seed);
    const Run b = run_backend("opencl", 1, seed);
    CHECK(a.states == b.states);
    CHECK(same_bits(a.p_burn, b.p_burn));
    CHECK(same_bits(a.loss, b.loss));
    CHECK(same_bits(a.p_lost, b.p_lost));
    CHECK(a.area == b.area);
  }
  EpisodeOptions o;
  o.scenario.n = 48;
  o.scenario.ticks = 40;
  o.engine.K = 16;
  o.engine.H = 8;
  o.engine.backend = "cpu_naive";
  const auto ra = decision_log(run_episode(41, "ensemble", o));
  o.engine.backend = "opencl";
  const auto rb = decision_log(run_episode(41, "ensemble", o));
  CHECK(ra == rb);
}
#endif

TEST_CASE("thread pool runs every index exactly once, many times") {
  ThreadPool pool(4);
  std::vector<int> hits(1000, 0);
  for (int rep = 0; rep < 200; ++rep) {
    std::fill(hits.begin(), hits.end(), 0);
    auto fn = [&](int i, int) { ++hits[std::size_t(i)]; };
    pool.run(int(hits.size()), fn);
    for (int h : hits) REQUIRE(h == 1);
  }
}
