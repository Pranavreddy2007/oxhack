#pragma once
// Spread rule and probability -> uint32 threshold lookup table.
//
// For an UNBURNED cell c and each BURNING neighbour in direction d, the
// spread draw u (uint32) ignites c when u < T[fuel(c)][d][slope_bin(c,d)][crews(c)].
// The CPU builds T once per (member, tick) from theta and weather. Every
// backend gets the same table and compares integers only.

#include <cmath>
#include <cstdint>

#include "embercast/rng.hpp"
#include "embercast/types.hpp"

namespace embercast {

// Neighbour offsets. Neighbour d of cell (x, y) is (x + kDx[d], y + kDy[d]).
// Directions 0..3 come from RNG lane 0, directions 4..7 from lane 1.
inline constexpr int kDx[kDirs] = {1, 0, -1, 0, 1, -1, -1, 1};
inline constexpr int kDy[kDirs] = {0, 1, 0, -1, 1, 1, -1, -1};

struct ModelConst {
  float base_rate[kFuelClasses] = {0.f, 0.04f, 0.07f, 0.10f};  // per tick
  float k_wind = 0.12f;    // per (m/s)
  float k_slope = 0.04f;   // per m of rise between cells
  float k_moist = 3.0f;
  float h_ref = 0.3f;
  float e_max = 0.85f;     // crew efficacy limit
  float e_n0 = 1.0f;       // crew efficacy scale
  int burn_ticks = 6;      // B
  float slope_bin_m = 10.f;
};

// Crew efficacy e(n) with diminishing returns.
inline double crew_efficacy(const ModelConst& mc, int n) {
  if (n <= 0) return 0.0;
  return mc.e_max * (1.0 - std::exp(-double(n) / mc.e_n0));
}

inline int slope_bin_of(const ModelConst& mc, float dz) {
  int b = int(std::lround(dz / mc.slope_bin_m)) + kSlopeBins / 2;
  return b < 0 ? 0 : (b >= kSlopeBins ? kSlopeBins - 1 : b);
}

struct ThresholdTable {
  // [fuel][dir][slope_bin][crews at the target cell]
  std::uint32_t t[kFuelClasses][kDirs][kSlopeBins][kMaxCrews + 1];
};

inline std::uint32_t prob_to_threshold(double p) {
  if (!(p > 0.0)) return 0u;
  const double v = p * 4294967296.0;
  return v >= 4294967295.0 ? 0xFFFFFFFFu : std::uint32_t(v);
}

// Per-tick ignition probability from one burning neighbour in direction d.
// The spread vector is from the neighbour to the target: (-kDx[d], -kDy[d]).
// The factors are split so that build_thresholds() needs few exp() calls.
inline double wind_factor(const ModelConst& mc, int d, const Weather& w, const Theta& th) {
  const double sx = -kDx[d], sy = -kDy[d];
  const double len = std::sqrt(sx * sx + sy * sy);
  const double dir = double(w.wind_dir) + th.wind_bias;
  const double cos_t = (sx * std::cos(dir) + sy * std::sin(dir)) / len;
  double f = std::exp(mc.k_wind * w.wind_speed * cos_t);
  if (len > 1.5) f *= 0.70710678118654752;  // diagonal neighbour
  return f;
}
inline double slope_factor(const ModelConst& mc, int sbin) {
  return std::exp(mc.k_slope * (sbin - kSlopeBins / 2) * double(mc.slope_bin_m));
}
inline double moisture_factor(const ModelConst& mc, const Weather& w, const Theta& th) {
  double h = double(w.humidity) + th.moisture_bias;
  h = h < 0.0 ? 0.0 : (h > 1.0 ? 1.0 : h);
  return std::exp(-mc.k_moist * (h - mc.h_ref));
}

inline double ignition_prob(const ModelConst& mc, int fuel, int d, int sbin, int crews,
                            const Weather& w, const Theta& th) {
  const double r = mc.base_rate[fuel] * wind_factor(mc, d, w, th) * slope_factor(mc, sbin) *
                   moisture_factor(mc, w, th) * th.spread_scale;
  return (1.0 - std::exp(-r)) * (1.0 - crew_efficacy(mc, crews));
}

inline void build_thresholds(const ModelConst& mc, const Weather& w, const Theta& th,
                             ThresholdTable& out) {
  double fw[kDirs], fs[kSlopeBins], keep[kMaxCrews + 1];
  for (int d = 0; d < kDirs; ++d) fw[d] = wind_factor(mc, d, w, th);
  for (int s = 0; s < kSlopeBins; ++s) fs[s] = slope_factor(mc, s);
  for (int c = 0; c <= kMaxCrews; ++c) keep[c] = 1.0 - crew_efficacy(mc, c);
  const double fm = moisture_factor(mc, w, th) * th.spread_scale;
  for (int f = 0; f < kFuelClasses; ++f)
    for (int d = 0; d < kDirs; ++d)
      for (int s = 0; s < kSlopeBins; ++s) {
        const double r = mc.base_rate[f] * fw[d] * fs[s] * fm;
        const double p = 1.0 - std::exp(-r);
        for (int c = 0; c <= kMaxCrews; ++c) out.t[f][d][s][c] = prob_to_threshold(p * keep[c]);
      }
}

// Prior over the hidden parameters. The same for all seeds. The engine knows it.
struct ThetaPrior {
  float wind_bias_sd = 0.35f, wind_bias_max = 0.9f;
  float log_s_sd = 0.30f, s_min = 0.4f, s_max = 2.5f;
  float moist_sd = 0.08f, moist_max = 0.25f;
};

inline Theta clamp_theta(const ThetaPrior& p, Theta t) {
  auto cl = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
  t.wind_bias = cl(t.wind_bias, -p.wind_bias_max, p.wind_bias_max);
  t.spread_scale = cl(t.spread_scale, p.s_min, p.s_max);
  t.moisture_bias = cl(t.moisture_bias, -p.moist_max, p.moist_max);
  return t;
}

// Compute slope bins for all cells. Call once after the terrain is made.
void compute_slope_bins(const ModelConst& mc, Terrain& t);

// Spread draws for one cell: lane 0 -> dirs 0..3, lane 1 -> dirs 4..7.
inline u32x4 spread_draws(std::uint64_t seed, std::uint32_t member, std::uint32_t tick,
                          std::uint32_t cell, std::uint32_t stream_base, int lane) {
  return rng4(seed, cell, tick, member, stream_base + std::uint32_t(lane));
}

// Reference one-tick update of one grid. `in` and `out` must not overlap.
// `tick` is the tick that `out` represents.
void step_reference(const Terrain& t, const ModelConst& mc, const ThresholdTable& tab,
                    const CrewCounts& crews, std::uint64_t seed, std::uint32_t member,
                    std::uint32_t tick, std::uint32_t stream_base, const std::uint8_t* in,
                    std::uint8_t* out);

}  // namespace embercast
