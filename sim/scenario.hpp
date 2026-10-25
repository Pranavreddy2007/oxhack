#pragma once
// Deterministic scenario generator. Same seed and options -> same scenario.

#include <cstdint>
#include <string>

#include "embercast/model.hpp"
#include "embercast/types.hpp"

namespace embercast {

enum class Stress { None, RegimeShift, Dropout30, LateScans, StuckSensor };

const char* stress_name(Stress s);
bool parse_stress(const std::string& s, Stress& out);

struct ObsParams {
  float noise_ws = 0.8f, noise_wd = 0.12f, noise_h = 0.03f, noise_t = 0.8f;
  float dropout = 0.05f;          // P(sensor frame is missing)
  float stuck_start = 0.01f;      // P(stuck episode starts) per tick
  int stuck_len = 12;             // ticks
  float out_of_range = 0.01f;     // P(one value is out of range)
  int max_delay = 1;              // sensor delay jitter, ticks (0..max_delay)
  int scan_period = 4;            // M
  float scan_coverage = 0.6f;     // fraction of cells in a scan
  float scan_fn = 0.10f;          // false-negative rate of a scan
  float scan_late_prob = 0.15f;   // P(scan is late)
  int scan_late_ticks = 3;
};

struct TruthParams {
  Theta theta0;
  Theta drift_sd{0.01f, 0.006f, 0.003f};  // per tick (s: multiplicative)
  Weather weather0;
  float wd_sd = 0.02f, ws_sd = 0.15f, h_sd = 0.005f, t_sd = 0.05f;  // per tick
  int shift_tick = -1;            // regime shift tick (-1 = none)
  float shift_dir = 0.f;          // radians added at the shift
  float shift_speed_mult = 1.f;
};

struct Scenario {
  std::uint64_t seed = 0;
  Stress stress = Stress::None;
  Terrain terrain;
  ModelConst mc;
  ThetaPrior prior;
  TruthParams truth;
  ObsParams obs;
  int ignition_cell = 0;
  int ticks = 120;
  int crews = 4;         // C
  int travel_ticks = 3;
};

struct ScenarioOptions {
  int n = 96;
  int clusters = 8;      // J
  int crews = 4;
  int ticks = 120;
  Stress stress = Stress::None;
  float shift_prob = 0.5f;  // P(regime shift) when stress is None
};

Scenario make_scenario(std::uint64_t seed, const ScenarioOptions& opt = {});

// Initial fire state: the ignition cell and its 4 neighbours burn.
void initial_state(const Scenario& sc, std::uint8_t* state);

}  // namespace embercast
