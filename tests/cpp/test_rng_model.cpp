#include <cmath>
#include <vector>

#include "doctest.h"
#include "embercast/model.hpp"
#include "embercast/rng.hpp"
#include "scenario.hpp"

using namespace embercast;

TEST_CASE("philox4x32-10 known-answer vectors (Random123 kat_vectors)") {
  auto r = philox4x32_10({0, 0, 0, 0}, 0, 0);
  CHECK(r[0] == 0x6627e8d5u);
  CHECK(r[1] == 0xe169c58du);
  CHECK(r[2] == 0xbc57ac4cu);
  CHECK(r[3] == 0x9b00dbd8u);
  r = philox4x32_10({0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}, 0xffffffffu, 0xffffffffu);
  CHECK(r[0] == 0x408f276du);
  CHECK(r[1] == 0x41c83b0eu);
  CHECK(r[2] == 0xa20bc7c6u);
  CHECK(r[3] == 0x6d5451fdu);
  r = philox4x32_10({0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u}, 0xa4093822u, 0x299f31d0u);
  CHECK(r[0] == 0xd16cfe09u);
  CHECK(r[1] == 0x94fdccebu);
  CHECK(r[2] == 0x5001e420u);
  CHECK(r[3] == 0x24126ea1u);
}

TEST_CASE("rng is a pure function of its inputs") {
  CHECK(rng4(42, 1, 2, 3, 4) == rng4(42, 1, 2, 3, 4));
  CHECK(rng4(42, 1, 2, 3, 4) != rng4(43, 1, 2, 3, 4));
  CHECK(rng4(42, 1, 2, 3, 4) != rng4(42, 1, 2, 3, 5));
}

TEST_CASE("uniform draws have a plausible mean") {
  double s = 0;
  const int n = 100000;
  for (int i = 0; i < n; ++i) s += u01(rng4(7, std::uint32_t(i), 0, 0, 1)[0]);
  CHECK(std::abs(s / n - 0.5) < 0.01);
}

TEST_CASE("threshold conversion") {
  CHECK(prob_to_threshold(0.0) == 0u);
  CHECK(prob_to_threshold(-1.0) == 0u);
  CHECK(prob_to_threshold(1.0) == 0xFFFFFFFFu);
  CHECK(prob_to_threshold(0.5) == 0x80000000u);
}

TEST_CASE("ignition probability is in [0,1] and behaves") {
  ModelConst mc;
  Weather w;
  w.wind_speed = 10.f;
  w.wind_dir = 0.f;  // blows to +x
  Theta th;
  for (int f = 0; f < kFuelClasses; ++f)
    for (int d = 0; d < kDirs; ++d)
      for (int s = 0; s < kSlopeBins; ++s)
        for (int c = 0; c <= kMaxCrews; ++c) {
          const double p = ignition_prob(mc, f, d, s, c, w, th);
          CHECK(p >= 0.0);
          CHECK(p <= 1.0);
        }
  // Fuel 0 never burns.
  CHECK(ignition_prob(mc, 0, 0, 4, 0, w, th) == 0.0);
  // Neighbour d=2 is at -x, so spread goes to +x: downwind is faster than upwind (d=0).
  CHECK(ignition_prob(mc, 2, 2, 4, 0, w, th) > ignition_prob(mc, 2, 0, 4, 0, w, th));
  // Uphill is faster.
  CHECK(ignition_prob(mc, 2, 1, 8, 0, w, th) > ignition_prob(mc, 2, 1, 0, 0, w, th));
  // More crews never increase the probability; diminishing returns.
  for (int c = 0; c < kMaxCrews; ++c)
    CHECK(ignition_prob(mc, 2, 2, 4, c + 1, w, th) <= ignition_prob(mc, 2, 2, 4, c, w, th));
  CHECK(crew_efficacy(mc, 2) - crew_efficacy(mc, 1) < crew_efficacy(mc, 1) - crew_efficacy(mc, 0));
}

TEST_CASE("spread step: burnt cells never return, burning cells count down") {
  ScenarioOptions opt;
  opt.n = 48;
  const Scenario sc = make_scenario(11, opt);
  ThresholdTable tab;
  Weather w = sc.truth.weather0;
  build_thresholds(sc.mc, w, sc.truth.theta0, tab);
  std::vector<std::uint8_t> a(sc.terrain.cells(), kUnburned), b(a.size());
  a[sc.ignition_cell] = std::uint8_t(sc.mc.burn_ticks);
  CrewCounts crews{};
  int prev_fp = 1;
  for (std::uint32_t t = 1; t <= 40; ++t) {
    step_reference(sc.terrain, sc.mc, tab, crews, 5, 0, t, kStreamSpread, a.data(), b.data());
    int fp = 0;
    for (std::size_t c = 0; c < a.size(); ++c) {
      if (a[c] == kBurnt) CHECK(b[c] == kBurnt);
      if (is_burning(a[c])) CHECK((b[c] == kBurnt || b[c] == a[c] - 1));
      if (sc.terrain.fuel[c] == 0) CHECK(b[c] == kUnburned);
      fp += is_footprint(b[c]);
    }
    CHECK(fp >= prev_fp);
    prev_fp = fp;
    a.swap(b);
  }
  CHECK(prev_fp > 1);
}
