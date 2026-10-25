#include "truth.hpp"

#include <algorithm>
#include <cmath>

#include "embercast/rng.hpp"

namespace embercast {

TruthSim::TruthSim(const Scenario& sc) : sc_(&sc) {
  const int cells = sc.terrain.cells();
  state_.assign(cells, kUnburned);
  next_.assign(cells, kUnburned);
  initial_state(sc, state_.data());
  weather_ = sc.truth.weather0;
  theta_ = sc.truth.theta0;
  sensor_out_.reserve(64);
  scan_out_.reserve(8);
  emit_observations();
}

void TruthSim::set_plan(const Plan& p) {
  for (int i = 0; i < sc_->crews && i < p.num_crews; ++i) {
    Crew& c = crews_[i];
    const std::uint8_t tgt = p.crew_cluster[i];
    if (tgt == c.target) continue;
    c.target = tgt;
    c.at = kNoCluster;
    c.arrive = tick_ + std::uint32_t(sc_->travel_ticks);
  }
}

void TruthSim::clear_crews() { crews_ = {}; }

CrewCounts TruthSim::effective_crews() const {
  CrewCounts n{};
  for (int i = 0; i < sc_->crews; ++i)
    if (crews_[i].at != kNoCluster) ++n[crews_[i].at];
  n[kNoCluster] = 0;
  return n;
}

int TruthSim::burned_area() const {
  int a = 0;
  for (std::uint8_t v : state_) a += is_footprint(v);
  return a;
}

void TruthSim::step() {
  const std::uint32_t t = tick_ + 1;
  const TruthParams& tp = sc_->truth;

  // Weather random walk and regime shift.
  {
    const auto a = rng4(sc_->seed, t, 0, 0, kStreamWeather);
    const auto b = rng4(sc_->seed, t, 1, 0, kStreamWeather);
    weather_.wind_dir += float(tp.wd_sd * normal01(a[0], a[1]));
    weather_.wind_speed =
        std::clamp(weather_.wind_speed + float(tp.ws_sd * normal01(a[2], a[3])), 0.5f, 18.f);
    weather_.humidity =
        std::clamp(weather_.humidity + float(tp.h_sd * normal01(b[0], b[1])), 0.05f, 0.9f);
    weather_.temp += float(tp.t_sd * normal01(b[2], b[3]));
    if (int(t) == tp.shift_tick) {
      weather_.wind_dir += tp.shift_dir;
      weather_.wind_speed = std::min(18.f, weather_.wind_speed * tp.shift_speed_mult);
    }
    weather_.wind_dir += pending_shift_;
    pending_shift_ = 0.f;
  }
  // Hidden parameter drift (bounded random walk).
  {
    const auto a = rng4(sc_->seed, t, 0, 0, kStreamTheta);
    const auto b = rng4(sc_->seed, t, 1, 0, kStreamTheta);
    Theta th = theta_;
    th.wind_bias += float(tp.drift_sd.wind_bias * normal01(a[0], a[1]));
    th.spread_scale *= float(std::exp(tp.drift_sd.spread_scale * normal01(a[2], a[3])));
    th.moisture_bias += float(tp.drift_sd.moisture_bias * normal01(b[0], b[1]));
    theta_ = clamp_theta(sc_->prior, th);
  }
  // Crew arrivals.
  for (int i = 0; i < sc_->crews; ++i)
    if (crews_[i].at == kNoCluster && crews_[i].target != kNoCluster && crews_[i].arrive <= t)
      crews_[i].at = crews_[i].target;

  build_thresholds(sc_->mc, weather_, theta_, table_);
  step_reference(sc_->terrain, sc_->mc, table_, effective_crews(), sc_->seed, kTruthMember, t,
                 kStreamSpread, state_.data(), next_.data());
  const Terrain& tr = sc_->terrain;
  for (int c = 0; c < tr.cells(); ++c)
    if (state_[c] == kUnburned && next_[c] != kUnburned) {
      total_loss_ += tr.value[c];
      if (tr.cluster[c] != kNoCluster) cluster_loss_[tr.cluster[c]] += tr.value[c];
    }
  state_.swap(next_);
  tick_ = t;
  emit_observations();
}

void TruthSim::emit_observations() {
  if (!emit_obs_) return;
  const ObsParams& op = sc_->obs;
  const std::uint32_t t = tick_;
  const std::uint64_t seed = sc_->seed;

  // Sensor frame.
  {
    const auto a = rng4(seed, t, 0, 0, kStreamSensor);
    const auto b = rng4(seed, t, 1, 0, kStreamSensor);
    const auto c = rng4(seed, t, 2, 0, kStreamSensor);
    SensorMsg m;
    m.seq = sensor_seq_++;
    m.tick = t;
    m.wind_speed = std::max(0.f, weather_.wind_speed + float(op.noise_ws * normal01(a[0], a[1])));
    m.wind_dir = weather_.wind_dir + float(op.noise_wd * normal01(a[2], a[3]));
    m.humidity = std::clamp(weather_.humidity + float(op.noise_h * normal01(b[0], b[1])), 0.f, 1.f);
    m.temp = weather_.temp + float(op.noise_t * normal01(b[2], b[3]));
    if (stuck_left_ > 0) {
      --stuck_left_;
      const auto seq = m.seq;
      m = stuck_values_;
      m.seq = seq;
      m.tick = t;
    } else if (u01(c[0]) < op.stuck_start) {
      stuck_left_ = op.stuck_len;
      stuck_values_ = m;
    }
    if (u01(c[1]) < op.out_of_range) m.humidity = 1.7f;
    const bool dropped = u01(c[2]) < op.dropout;
    const std::uint32_t delay = std::uint32_t(u01(c[3]) * (op.max_delay + 1));
    if (!dropped) sensor_out_.push_back({t + delay, m});
  }
  // Perimeter scan every M ticks.
  if (op.scan_period > 0 && t % std::uint32_t(op.scan_period) == 0) {
    PendingScan ps;
    ps.m.seq = scan_seq_++;
    ps.m.tick = t;
    ps.m.n = std::uint16_t(sc_->terrain.n);
    for (int cidx = 0; cidx < sc_->terrain.cells(); ++cidx) {
      const auto r = rng4(seed, std::uint32_t(cidx), t, 0, kStreamScan);
      if (u01(r[0]) >= op.scan_coverage) continue;
      bit_set(ps.m.observed, cidx);
      if (is_footprint(state_[cidx]) && u01(r[1]) >= op.scan_fn) bit_set(ps.m.fire, cidx);
    }
    const auto r = rng4(seed, t, 0, 1, kStreamScan);
    const bool late = u01(r[0]) < op.scan_late_prob;
    ps.deliver = t + (late ? std::uint32_t(op.scan_late_ticks) : 0u);
    scan_out_.push_back(ps);
  }
}

void TruthSim::collect(std::uint32_t now, std::vector<SensorMsg>& sensors,
                       std::vector<PerimeterMsg>& scans) {
  // Stable selection keeps creation order among equal delivery ticks.
  std::size_t keep = 0;
  for (std::size_t i = 0; i < sensor_out_.size(); ++i) {
    if (sensor_out_[i].deliver <= now)
      sensors.push_back(sensor_out_[i].m);
    else
      sensor_out_[keep++] = sensor_out_[i];
  }
  sensor_out_.resize(keep);
  keep = 0;
  for (std::size_t i = 0; i < scan_out_.size(); ++i) {
    if (scan_out_[i].deliver <= now)
      scans.push_back(scan_out_[i].m);
    else
      scan_out_[keep++] = scan_out_[i];
  }
  scan_out_.resize(keep);
}

}  // namespace embercast
