#pragma once
// Engine: feature update -> particle filter -> forecast -> policy, per tick.
// The same class runs in-process on the laptop and in board_daemon.
// All buffers are allocated in the constructor.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "embercast/filter.hpp"
#include "embercast/forecast.hpp"
#include "embercast/frame.hpp"
#include "embercast/policy.hpp"

namespace embercast {

enum class PolicyKind { Ensemble, Deterministic, FrontNearest };

const char* policy_kind_name(PolicyKind k);

struct EngineConfig {
  PolicyKind policy = PolicyKind::Ensemble;
  int K = 64;
  int H = 20;
  int D = 5;
  RiskParams risk;
  FilterConfig filter;
  std::string backend = "cpu_naive";
  int threads = 1;
  int stale_ticks = 3;      // sensor age (ticks) that makes the state STALE
  int max_late_ticks = 6;   // older frames are dropped
  float ema = 0.5f;         // smoothing of sensor values
  std::uint64_t seed = 1;
};

// Make the configuration of a named policy: ensemble, deterministic (B1),
// front_nearest (B0).
EngineConfig engine_config_for(PolicyKind k, const EngineConfig& base);

struct EngineCounters {
  std::uint32_t sensor_ok = 0, sensor_late = 0, sensor_out_of_range = 0, sensor_stuck = 0,
                sensor_dup = 0, sensor_flagged = 0;
  std::uint32_t scans_ok = 0, scans_late = 0, scans_dup = 0;
  std::uint32_t queue_full = 0;
};

struct StageTimes {
  std::uint64_t filter_ns = 0, rollout_ns = 0, policy_ns = 0, total_ns = 0;
};

struct EngineStatic {
  const Terrain* terrain = nullptr;
  const ModelConst* mc = nullptr;
  ThetaPrior prior;
  int crews = 4;
  int travel_ticks = 3;
  const std::uint8_t* init_state = nullptr;
};

class Engine {
 public:
  Engine(const EngineStatic& st, const EngineConfig& cfg);

  // Queue observations. They apply at the next tick() call.
  void on_sensor(const SensorMsg& m);
  void on_perimeter(const PerimeterMsg& m);

  // Advance the engine to tick t. Returns true when a new plan is made.
  bool tick(std::uint32_t t, Plan& plan);

  const ForecastOut& forecast() const { return fc_; }
  bool has_forecast() const { return has_fc_; }
  const StageTimes& last_times() const { return times_; }
  const EngineCounters& counters() const { return ctr_; }
  HealthState health() const { return health_; }
  const Weather& weather_estimate() const { return est_; }
  double ess() const { return filter_.ess(); }
  const std::vector<double>& weights() const { return filter_.ensemble().weight; }
  int resamples() const { return filter_.resamples(); }
  Theta mean_theta() const { return filter_.mean_theta(); }
  const Plan& plan() const { return plan_; }
  const EngineConfig& config() const { return cfg_; }
  const std::vector<std::uint8_t>& observed_fire() const { return observed_fire_; }
  const IForecastBackend& backend() const { return *backend_; }
  // Force STALE (for example on a transport disconnect).
  void set_link_down(bool down) { link_down_ = down; }

 private:
  void ingest_sensors(std::uint32_t t);
  CrewCounts effective_crews(std::uint32_t t) const;
  CrewCounts target_crews() const;

  EngineStatic st_;
  EngineConfig cfg_;
  std::unique_ptr<IForecastBackend> backend_;
  std::unique_ptr<IPolicy> policy_;
  ParticleFilter filter_;
  ForecastOut fc_;
  bool has_fc_ = false;
  Plan plan_;
  std::array<std::uint32_t, kMaxCrews> arrive_{};
  StageTimes times_;
  EngineCounters ctr_;
  HealthState health_ = HealthState::Stale;
  bool link_down_ = false;

  // Feature state.
  Weather est_;
  bool have_sensor_ = false;
  std::uint32_t last_sensor_tick_ = 0;
  std::int64_t last_sensor_seq_ = -1;
  SensorMsg last_raw_;
  int same_count_ = 0;
  std::int64_t last_scan_seq_ = -1;

  // Fixed-capacity queues (no allocation after construction).
  static constexpr int kQueue = 64;
  std::array<SensorMsg, kQueue> sq_;
  int sq_n_ = 0;
  std::vector<PerimeterMsg> pq_;
  int pq_n_ = 0;
  std::vector<std::uint8_t> observed_fire_;
};

}  // namespace embercast
