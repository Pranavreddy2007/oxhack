#pragma once
// Truth simulator. It owns the true fire state, the true weather and the
// hidden parameters. It makes noisy, late, and partial observations.
// All random numbers come from the counter RNG keyed by the scenario seed,
// so every policy sees the same realisation (common random numbers).

#include <array>
#include <cstdint>
#include <vector>

#include "embercast/frame.hpp"
#include "embercast/model.hpp"
#include "scenario.hpp"

namespace embercast {

class TruthSim {
 public:
  explicit TruthSim(const Scenario& sc);

  std::uint32_t tick() const { return tick_; }
  const std::vector<std::uint8_t>& state() const { return state_; }
  const Weather& weather() const { return weather_; }
  const Theta& theta() const { return theta_; }
  const Scenario& scenario() const { return *sc_; }

  // New crew targets. A crew that changes target is in transit for
  // travel_ticks. A crew in transit protects nothing.
  void set_plan(const Plan& p);
  CrewCounts effective_crews() const;
  void clear_crews();

  // Advance one tick.
  void step();

  double total_loss() const { return total_loss_; }
  const std::array<double, kMaxClusters>& cluster_loss() const { return cluster_loss_; }
  int burned_area() const;

  // Observation outbox. Pops the messages with delivery tick <= now, in
  // delivery order (ties by creation order).
  void collect(std::uint32_t now, std::vector<SensorMsg>& sensors,
               std::vector<PerimeterMsg>& scans);
  void set_emit_obs(bool on) { emit_obs_ = on; }

  // Demo control: add a wind direction change now (radians).
  void add_wind_shift(float rad) { pending_shift_ += rad; }

 private:
  struct Crew {
    std::uint8_t at = kNoCluster;
    std::uint8_t target = kNoCluster;
    std::uint32_t arrive = 0;
  };
  struct PendingSensor {
    std::uint32_t deliver;
    SensorMsg m;
  };
  struct PendingScan {
    std::uint32_t deliver;
    PerimeterMsg m;
  };

  void emit_observations();

  const Scenario* sc_;
  std::uint32_t tick_ = 0;
  std::vector<std::uint8_t> state_, next_;
  Weather weather_;
  Theta theta_;
  float pending_shift_ = 0.f;
  std::array<Crew, kMaxCrews> crews_{};
  double total_loss_ = 0.0;
  std::array<double, kMaxClusters> cluster_loss_{};
  ThresholdTable table_;
  bool emit_obs_ = true;
  std::uint32_t sensor_seq_ = 0, scan_seq_ = 0;
  int stuck_left_ = 0;
  SensorMsg stuck_values_;
  std::vector<PendingSensor> sensor_out_;
  std::vector<PendingScan> scan_out_;
};

}  // namespace embercast
