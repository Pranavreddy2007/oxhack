#include "embercast/engine.hpp"

#include <algorithm>
#include <cmath>

#include "embercast/clock.hpp"

namespace embercast {

const char* policy_kind_name(PolicyKind k) {
  switch (k) {
    case PolicyKind::Ensemble: return "ensemble";
    case PolicyKind::Deterministic: return "deterministic";
    case PolicyKind::FrontNearest: return "front_nearest";
  }
  return "?";
}

EngineConfig engine_config_for(PolicyKind k, const EngineConfig& base) {
  EngineConfig c = base;
  c.policy = k;
  if (k == PolicyKind::Deterministic) {
    c.K = 1;
    c.risk.lambda = 0.0;
    c.filter.weighting = false;
  }
  if (k == PolicyKind::FrontNearest) c.K = 1;
  return c;
}

Engine::Engine(const EngineStatic& st, const EngineConfig& cfg) : st_(st), cfg_(cfg) {
  cfg_.K = std::clamp(cfg_.K, 1, kMaxK);
  cfg_.H = std::clamp(cfg_.H, 1, kMaxH);
  cfg_.D = std::max(1, cfg_.D);
  backend_ = make_backend(cfg_.backend, cfg_.threads);
  const int cells = st.terrain->cells();
  backend_->prepare(cfg_.K, cells);
  const bool point = cfg_.policy != PolicyKind::Ensemble;
  filter_.init(*st.terrain, *st.mc, st.prior, cfg_.filter, cfg_.K, cfg_.seed, st.init_state, point);
  fc_.allocate(cfg_.K, cells);
  if (cfg_.policy == PolicyKind::FrontNearest)
    policy_ = make_front_nearest();
  else
    policy_ = make_ensemble_risk(cfg_.risk);
  plan_.num_crews = st.crews;
  plan_.crew_cluster.fill(kNoCluster);
  pq_.resize(8);
  observed_fire_.assign(cells, 0);
  for (int c = 0; c < cells; ++c) observed_fire_[c] = st.init_state[c] != kUnburned;
}

void Engine::on_sensor(const SensorMsg& m) {
  if (sq_n_ >= kQueue) {
    ++ctr_.queue_full;
    return;
  }
  sq_[sq_n_++] = m;
}

void Engine::on_perimeter(const PerimeterMsg& m) {
  if (pq_n_ >= int(pq_.size())) {
    ++ctr_.queue_full;
    return;
  }
  pq_[pq_n_++] = m;
}

void Engine::ingest_sensors(std::uint32_t t) {
  for (int i = 0; i < sq_n_; ++i) {
    const SensorMsg& m = sq_[i];
    if (std::int64_t(m.seq) <= last_sensor_seq_) {  // duplicate or out of order
      ++(std::int64_t(m.seq) == last_sensor_seq_ ? ctr_.sensor_dup : ctr_.sensor_late);
      continue;
    }
    if (t > m.tick && t - m.tick > std::uint32_t(cfg_.max_late_ticks)) {
      ++ctr_.sensor_late;
      continue;
    }
    if (m.flags != 0) {
      ++ctr_.sensor_flagged;
      continue;
    }
    const bool finite = std::isfinite(m.wind_speed) && std::isfinite(m.wind_dir) &&
                        std::isfinite(m.humidity) && std::isfinite(m.temp);
    if (!finite || m.wind_speed < 0.f || m.wind_speed > 40.f || m.humidity < 0.f ||
        m.humidity > 1.f || m.temp < -30.f || m.temp > 60.f) {
      ++ctr_.sensor_out_of_range;
      continue;
    }
    // Stuck sensor: three identical frames in a row.
    const bool same = have_sensor_ && m.wind_speed == last_raw_.wind_speed &&
                      m.wind_dir == last_raw_.wind_dir && m.humidity == last_raw_.humidity &&
                      m.temp == last_raw_.temp;
    same_count_ = same ? same_count_ + 1 : 0;
    last_raw_ = m;
    last_sensor_seq_ = m.seq;
    if (same_count_ >= 2) {
      ++ctr_.sensor_stuck;
      continue;
    }
    ++ctr_.sensor_ok;
    if (!have_sensor_) {
      est_.wind_speed = m.wind_speed;
      est_.wind_dir = m.wind_dir;
      est_.humidity = m.humidity;
      est_.temp = m.temp;
      have_sensor_ = true;
    } else {
      const float a = cfg_.ema;
      // Smooth the wind as a vector.
      const float ux = (1 - a) * est_.wind_speed * std::cos(est_.wind_dir) +
                       a * m.wind_speed * std::cos(m.wind_dir);
      const float uy = (1 - a) * est_.wind_speed * std::sin(est_.wind_dir) +
                       a * m.wind_speed * std::sin(m.wind_dir);
      est_.wind_speed = std::sqrt(ux * ux + uy * uy);
      est_.wind_dir = std::atan2(uy, ux);
      est_.humidity = (1 - a) * est_.humidity + a * m.humidity;
      est_.temp = (1 - a) * est_.temp + a * m.temp;
    }
    last_sensor_tick_ = std::max(last_sensor_tick_, m.tick);
  }
  sq_n_ = 0;
}

CrewCounts Engine::effective_crews(std::uint32_t t) const {
  CrewCounts n{};
  for (int i = 0; i < plan_.num_crews; ++i)
    if (plan_.crew_cluster[i] != kNoCluster && arrive_[i] <= t) ++n[plan_.crew_cluster[i]];
  n[kNoCluster] = 0;
  return n;
}

CrewCounts Engine::target_crews() const {
  CrewCounts n = counts_from_plan(plan_);
  n[kNoCluster] = 0;
  return n;
}

bool Engine::tick(std::uint32_t t, Plan& out) {
  const std::uint64_t t0 = now_ns();
  ingest_sensors(t);
  const bool stale = link_down_ || !have_sensor_ ||
                     (t > last_sensor_tick_ && t - last_sensor_tick_ > std::uint32_t(cfg_.stale_ticks));
  health_ = stale ? HealthState::Stale : HealthState::Ok;

  const bool use_filter = policy_->needs_forecast();
  if (use_filter && t > 0) filter_.propagate(*backend_, est_, effective_crews(t), t, stale);
  for (int i = 0; i < pq_n_; ++i) {
    const PerimeterMsg& m = pq_[i];
    if (std::int64_t(m.seq) <= last_scan_seq_) {
      ++ctr_.scans_dup;
      continue;
    }
    if (t > m.tick && t - m.tick > std::uint32_t(cfg_.max_late_ticks)) {
      ++ctr_.scans_late;
      continue;
    }
    last_scan_seq_ = m.seq;
    for (int c = 0; c < int(observed_fire_.size()); ++c)
      if (bit_get(m.observed, c) && bit_get(m.fire, c)) observed_fire_[c] = 1;
    if (use_filter) filter_.update(m, t);
    ++ctr_.scans_ok;
  }
  pq_n_ = 0;
  const std::uint64_t t1 = now_ns();
  times_ = {};
  times_.filter_ns = t1 - t0;

  if (t % std::uint32_t(cfg_.D) != 0) {
    times_.total_ns = times_.filter_ns;
    return false;
  }
  PolicyContext ctx;
  ctx.terrain = st_.terrain;
  ctx.mc = st_.mc;
  ctx.crews = st_.crews;
  ctx.tick = t;
  ctx.prev = plan_;
  ctx.observed_fire = &observed_fire_;
  std::uint64_t t2 = t1;
  if (use_filter) {
    StepArgs s;
    s.terrain = st_.terrain;
    s.mc = st_.mc;
    s.crews = target_crews();
    s.seed = cfg_.seed;
    s.tick = t + 1;
    filter_.build_tables(est_);
    backend_->rollout(filter_.ensemble(), s, cfg_.H, fc_);
    has_fc_ = true;
    t2 = now_ns();
    ctx.fc = &fc_;
    ctx.weight = &filter_.ensemble().weight;
    ctx.rollout_crews = s.crews;
  }
  Plan p = policy_->decide(ctx);
  const std::uint64_t t3 = now_ns();
  for (int i = 0; i < p.num_crews; ++i)
    if (p.crew_cluster[i] != plan_.crew_cluster[i])
      arrive_[i] = t + std::uint32_t(st_.travel_ticks);
  plan_ = p;
  out = p;
  times_.rollout_ns = t2 - t1;
  times_.policy_ns = t3 - t2;
  times_.total_ns = t3 - t0;
  return true;
}

}  // namespace embercast
