#include <vector>

#include "doctest.h"
#include "scenario.hpp"
#include "truth.hpp"

using namespace embercast;

namespace {

// Run a truth simulation with a fixed crew plan and record everything
// observable into one byte string.
std::vector<std::uint8_t> run_truth(std::uint64_t seed, Stress stress) {
  ScenarioOptions opt;
  opt.n = 64;
  opt.ticks = 60;
  opt.stress = stress;
  const Scenario sc = make_scenario(seed, opt);
  TruthSim ts(sc);
  Plan p;
  p.num_crews = sc.crews;
  for (int i = 0; i < sc.crews && i < kMaxCrews; ++i)
    p.crew_cluster[i] = std::uint8_t(i % sc.terrain.num_clusters);
  ts.set_plan(p);
  std::vector<std::uint8_t> log;
  std::vector<SensorMsg> s;
  std::vector<PerimeterMsg> sc_msgs;
  std::vector<std::uint8_t> buf(kMaxFrame);
  for (int t = 0; t < sc.ticks; ++t) {
    ts.collect(ts.tick(), s, sc_msgs);
    for (auto& m : s) {
      const auto n = encode(m, buf.data(), buf.size());
      log.insert(log.end(), buf.begin(), buf.begin() + long(n));
    }
    for (auto& m : sc_msgs) {
      const auto n = encode(m, buf.data(), buf.size());
      log.insert(log.end(), buf.begin(), buf.begin() + long(n));
    }
    s.clear();
    sc_msgs.clear();
    ts.step();
  }
  log.insert(log.end(), ts.state().begin(), ts.state().end());
  return log;
}

}  // namespace

TEST_CASE("scenario generator is deterministic and valid") {
  const Scenario a = make_scenario(1234), b = make_scenario(1234), c = make_scenario(1235);
  CHECK(a.terrain.fuel == b.terrain.fuel);
  CHECK(a.terrain.elev == b.terrain.elev);
  CHECK(a.terrain.cluster == b.terrain.cluster);
  CHECK(a.ignition_cell == b.ignition_cell);
  CHECK(a.terrain.fuel != c.terrain.fuel);
  CHECK(a.terrain.num_clusters == 8);
  for (int j = 0; j < a.terrain.num_clusters; ++j) CHECK(a.terrain.cluster_value[j] > 0.f);
}

TEST_CASE("truth replay is byte-identical for the same seed") {
  for (Stress st : {Stress::None, Stress::RegimeShift, Stress::Dropout30, Stress::LateScans,
                    Stress::StuckSensor}) {
    const auto x = run_truth(77, st), y = run_truth(77, st);
    CHECK(x == y);
    CHECK(x != run_truth(78, st));
  }
}

TEST_CASE("truth: fire grows, loss is monotone, crews protect") {
  ScenarioOptions opt;
  opt.n = 64;
  const Scenario sc = make_scenario(3, opt);
  TruthSim free_run(sc), defended(sc);
  Plan p;
  p.num_crews = sc.crews;
  for (int i = 0; i < sc.crews && i < kMaxCrews; ++i) p.crew_cluster[i] = 0;
  defended.set_plan(p);
  double prev = 0;
  for (int t = 0; t < 80; ++t) {
    free_run.step();
    defended.step();
    CHECK(free_run.total_loss() >= prev);
    prev = free_run.total_loss();
  }
  CHECK(free_run.burned_area() > 1);
  CHECK(defended.effective_crews()[0] == sc.crews);
  // Same random draws, lower thresholds in cluster 0 -> never more loss there.
  CHECK(defended.cluster_loss()[0] <= free_run.cluster_loss()[0]);
}

TEST_CASE("truth: dropout stress gives fewer sensor frames") {
  ScenarioOptions a, b;
  a.n = b.n = 32;
  b.stress = Stress::Dropout30;
  const Scenario sa = make_scenario(9, a), sb = make_scenario(9, b);
  TruthSim ta(sa), tb(sb);
  std::vector<SensorMsg> s1, s2;
  std::vector<PerimeterMsg> p;
  for (int t = 0; t < 200; ++t) {
    ta.step();
    tb.step();
    ta.collect(ta.tick(), s1, p);
    tb.collect(tb.tick(), s2, p);
  }
  CHECK(s2.size() < s1.size());
  CHECK(double(s2.size()) / 200.0 < 0.8);
}
