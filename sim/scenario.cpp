#include "scenario.hpp"

#include <algorithm>
#include <cmath>

#include "embercast/rng.hpp"

namespace embercast {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Draw helper: a sequence of uniforms from one key, by index.
struct Draw {
  std::uint64_t seed;
  std::uint32_t stream;
  std::uint32_t idx = 0;
  double u() { return u01(rng4(seed, idx++, 0, 0, stream)[0]); }
  double normal() {
    const auto r = rng4(seed, idx++, 0, 0, stream);
    return normal01(r[0], r[1]);
  }
  int uniform_int(int lo, int hi) {  // inclusive
    return lo + int(u() * (hi - lo + 1));
  }
};

}  // namespace

const char* stress_name(Stress s) {
  switch (s) {
    case Stress::None: return "none";
    case Stress::RegimeShift: return "regime";
    case Stress::Dropout30: return "dropout30";
    case Stress::LateScans: return "late_scans";
    case Stress::StuckSensor: return "stuck";
  }
  return "?";
}

bool parse_stress(const std::string& s, Stress& out) {
  for (Stress v : {Stress::None, Stress::RegimeShift, Stress::Dropout30, Stress::LateScans,
                   Stress::StuckSensor})
    if (s == stress_name(v)) {
      out = v;
      return true;
    }
  return false;
}

Theta clamp_theta(const ThetaPrior& p, Theta t) {
  t.wind_bias = std::clamp(t.wind_bias, -p.wind_bias_max, p.wind_bias_max);
  t.spread_scale = std::clamp(t.spread_scale, p.s_min, p.s_max);
  t.moisture_bias = std::clamp(t.moisture_bias, -p.moist_max, p.moist_max);
  return t;
}

void initial_state(const Scenario& sc, std::uint8_t* state) {
  const Terrain& t = sc.terrain;
  std::fill(state, state + t.cells(), kUnburned);
  const int x = sc.ignition_cell % t.n, y = sc.ignition_cell / t.n;
  const int dx[5] = {0, 1, -1, 0, 0}, dy[5] = {0, 0, 0, 1, -1};
  for (int i = 0; i < 5; ++i) {
    const int c = (y + dy[i]) * t.n + (x + dx[i]);
    if (t.fuel[c] != 0) state[c] = std::uint8_t(sc.mc.burn_ticks);
  }
}

Scenario make_scenario(std::uint64_t seed, const ScenarioOptions& opt) {
  Scenario sc;
  sc.seed = seed;
  sc.stress = opt.stress;
  sc.ticks = opt.ticks;
  sc.crews = std::min(opt.crews, kMaxCrews);
  Draw dr{seed, kStreamScenario};

  const int n = std::clamp(opt.n, 16, kMaxN);
  Terrain& t = sc.terrain;
  t.n = n;
  const int cells = n * n;
  t.fuel.assign(cells, 1);
  t.elev.assign(cells, 0.f);
  t.cluster.assign(cells, kNoCluster);
  t.value.assign(cells, 0.f);

  // Elevation: sum of Gaussian hills.
  const int hills = 6;
  std::vector<float> fuel_field(cells, 0.f);
  for (int h = 0; h < hills; ++h) {
    const double cx = dr.u() * n, cy = dr.u() * n;
    const double r = (0.12 + 0.2 * dr.u()) * n, a = 40.0 + 120.0 * dr.u();
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x) {
        const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        t.elev[y * n + x] += float(a * std::exp(-d2 / (2 * r * r)));
      }
  }
  // Fuel: smooth random field from bumps, quantised to classes 1..3.
  for (int b = 0; b < 14; ++b) {
    const double cx = dr.u() * n, cy = dr.u() * n;
    const double r = (0.06 + 0.12 * dr.u()) * n, a = dr.u() * 2.0 - 1.0;
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x) {
        const double d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
        fuel_field[y * n + x] += float(a * std::exp(-d2 / (2 * r * r)));
      }
  }
  for (int c = 0; c < cells; ++c) {
    const float f = fuel_field[c];
    t.fuel[c] = f < -0.35f ? 1 : (f < 0.35f ? 2 : 3);
  }
  // Non-burnable features: two straight "rock/road" bands with gaps.
  for (int band = 0; band < 2; ++band) {
    const bool vertical = dr.u() < 0.5;
    const int pos = dr.uniform_int(n / 6, n - n / 6);
    const int gap = dr.uniform_int(0, n - 1);
    for (int i = 0; i < n; ++i) {
      if (std::abs(i - gap) < 5) continue;
      const int c = vertical ? i * n + pos : pos * n + i;
      t.fuel[c] = 0;
    }
  }

  // Ignition near the centre.
  const int ix = n / 2 + dr.uniform_int(-n / 8, n / 8);
  const int iy = n / 2 + dr.uniform_int(-n / 8, n / 8);
  sc.ignition_cell = iy * n + ix;
  for (int c : {sc.ignition_cell, sc.ignition_cell + 1, sc.ignition_cell - 1, sc.ignition_cell + n,
                sc.ignition_cell - n})
    t.fuel[c] = 2;

  // Asset clusters: discs at distance 12..42 % of n from the ignition.
  const int J = std::clamp(opt.clusters, 1, kMaxClusters);
  int made = 0;
  for (int attempt = 0; attempt < 400 && made < J; ++attempt) {
    const double ang = dr.u() * 2 * kPi;
    const double dist = (0.12 + 0.30 * dr.u()) * n;
    const int cx = int(ix + dist * std::cos(ang)), cy = int(iy + dist * std::sin(ang));
    const int r = dr.uniform_int(2, 4);
    if (cx - r < 1 || cy - r < 1 || cx + r >= n - 1 || cy + r >= n - 1) continue;
    bool clash = false;
    for (int y = cy - r - 2; y <= cy + r + 2 && !clash; ++y)
      for (int x = cx - r - 2; x <= cx + r + 2; ++x)
        if (x >= 0 && y >= 0 && x < n && y < n && t.cluster[y * n + x] != kNoCluster) {
          clash = true;
          break;
        }
    if (clash) continue;
    const float cell_value = float(1.0 + 3.0 * dr.u());
    double sx = 0, sy = 0;
    int count = 0;
    for (int y = cy - r; y <= cy + r; ++y)
      for (int x = cx - r; x <= cx + r; ++x) {
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r) continue;
        const int c = y * n + x;
        t.cluster[c] = std::uint8_t(made);
        t.value[c] = cell_value;
        if (t.fuel[c] == 0) t.fuel[c] = 1;
        t.cluster_value[made] += cell_value;
        sx += x;
        sy += y;
        ++count;
      }
    t.cluster_cx[made] = float(sx / count);
    t.cluster_cy[made] = float(sy / count);
    ++made;
  }
  t.num_clusters = made;
  compute_slope_bins(sc.mc, t);

  // Truth parameters: theta0 drawn from the prior.
  Theta th;
  th.wind_bias = float(sc.prior.wind_bias_sd * dr.normal());
  th.spread_scale = float(std::exp(sc.prior.log_s_sd * dr.normal()));
  th.moisture_bias = float(sc.prior.moist_sd * dr.normal());
  sc.truth.theta0 = clamp_theta(sc.prior, th);
  sc.truth.weather0.wind_speed = float(3.0 + 7.0 * dr.u());
  sc.truth.weather0.wind_dir = float(dr.u() * 2 * kPi);
  sc.truth.weather0.humidity = float(0.15 + 0.3 * dr.u());
  sc.truth.weather0.temp = float(20.0 + 15.0 * dr.u());

  const double shift_u = dr.u();
  const int shift_tick = dr.uniform_int(sc.ticks / 4, sc.ticks / 2);
  const double shift_ang = (kPi / 3 + dr.u() * kPi / 3) * (dr.u() < 0.5 ? -1 : 1);
  const bool shift = opt.stress == Stress::RegimeShift || (opt.stress == Stress::None && shift_u < opt.shift_prob);
  if (shift) {
    sc.truth.shift_tick = shift_tick;
    sc.truth.shift_dir = float(shift_ang);
    sc.truth.shift_speed_mult = 1.4f;
  }

  switch (opt.stress) {
    case Stress::Dropout30: sc.obs.dropout = 0.30f; break;
    case Stress::LateScans:
      sc.obs.scan_late_prob = 0.6f;
      sc.obs.scan_late_ticks = 5;
      break;
    case Stress::StuckSensor:
      sc.obs.stuck_start = 0.05f;
      sc.obs.stuck_len = 20;
      break;
    default: break;
  }
  return sc;
}

}  // namespace embercast
