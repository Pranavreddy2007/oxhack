#include "embercast/model.hpp"

namespace embercast {

void compute_slope_bins(const ModelConst& mc, Terrain& t) {
  const int n = t.n;
  t.slope_bin.assign(std::size_t(t.cells()) * kDirs, std::uint8_t(kSlopeBins / 2));
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const int c = y * n + x;
      for (int d = 0; d < kDirs; ++d) {
        const int nx = x + kDx[d], ny = y + kDy[d];
        if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
        const float dz = t.elev[c] - t.elev[ny * n + nx];  // rise from neighbour to target
        t.slope_bin[std::size_t(c) * kDirs + d] = std::uint8_t(slope_bin_of(mc, dz));
      }
    }
}

void step_reference(const Terrain& t, const ModelConst& mc, const ThresholdTable& tab,
                    const CrewCounts& crews, std::uint64_t seed, std::uint32_t member,
                    std::uint32_t tick, std::uint32_t stream_base, const std::uint8_t* in,
                    std::uint8_t* out) {
  const int n = t.n;
  const std::uint8_t B = std::uint8_t(mc.burn_ticks);
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      const int c = y * n + x;
      const std::uint8_t v = in[c];
      if (v == kBurnt) {
        out[c] = kBurnt;
        continue;
      }
      if (v != kUnburned) {
        out[c] = v == 1 ? kBurnt : std::uint8_t(v - 1);
        continue;
      }
      out[c] = kUnburned;
      const int fuel = t.fuel[c];
      if (fuel == 0) continue;
      const int ncrew = crews[t.cluster[c]];
      bool have_lane[2] = {false, false};
      u32x4 draws[2];
      for (int d = 0; d < kDirs; ++d) {
        const int nx = x + kDx[d], ny = y + kDy[d];
        if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
        if (!is_burning(in[ny * n + nx])) continue;
        const int lane = d >> 2;
        if (!have_lane[lane]) {
          draws[lane] = spread_draws(seed, member, tick, std::uint32_t(c), stream_base, lane);
          have_lane[lane] = true;
        }
        const std::uint32_t thr = tab.t[fuel][d][t.slope_bin[std::size_t(c) * kDirs + d]][ncrew];
        if (draws[lane][d & 3] < thr) {
          out[c] = B;
          break;
        }
      }
    }
  }
}

}  // namespace embercast
