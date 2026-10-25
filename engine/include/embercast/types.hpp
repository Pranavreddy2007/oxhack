#pragma once
// Grid, Params, Frame, Plan. Fixed sizes.

#include <array>
#include <cstdint>
#include <vector>

namespace embercast {

const char* version();
const char* safety_notice();

// Compile-time limits. Buffers use these sizes.
inline constexpr int kMaxN = 128;
inline constexpr int kMaxCells = kMaxN * kMaxN;
inline constexpr int kMaxK = 512;
inline constexpr int kMaxCrews = 8;
inline constexpr int kMaxClusters = 16;
inline constexpr int kMaxH = 64;
inline constexpr int kFuelClasses = 4;
inline constexpr int kDirs = 8;
inline constexpr int kSlopeBins = 9;

// Cluster id for a cell that is in no asset cluster.
inline constexpr std::uint8_t kNoCluster = kMaxClusters;

// Cell state encoding (one byte per cell).
//   0         UNBURNED
//   1..B      BURNING, value = ticks left
//   255       BURNT
inline constexpr std::uint8_t kUnburned = 0;
inline constexpr std::uint8_t kBurnt = 255;

enum class CellState : std::uint8_t { Unburned = 0, Burning = 1, Burnt = 2 };

inline CellState cell_state(std::uint8_t v) {
  if (v == kUnburned) return CellState::Unburned;
  if (v == kBurnt) return CellState::Burnt;
  return CellState::Burning;
}
inline bool is_burning(std::uint8_t v) { return v != kUnburned && v != kBurnt; }
inline bool is_footprint(std::uint8_t v) { return v != kUnburned; }

// Hidden parameters theta_h.
struct Theta {
  float wind_bias = 0.f;      // radians, added to the sensed wind direction
  float spread_scale = 1.f;   // s
  float moisture_bias = 0.f;  // added to the sensed humidity
};

// Weather. wind_dir is the direction the wind blows TO, in radians,
// grid frame: 0 = +x, pi/2 = +y.
struct Weather {
  float wind_speed = 5.f;  // m/s
  float wind_dir = 0.f;
  float humidity = 0.3f;   // 0..1
  float temp = 25.f;       // deg C
};

// Static terrain data. Allocated once.
struct Terrain {
  int n = 0;
  int cells() const { return n * n; }
  std::vector<std::uint8_t> fuel;        // 0..3, 0 = does not burn
  std::vector<float> elev;               // m
  std::vector<std::uint8_t> cluster;     // 0..J-1 or kNoCluster
  std::vector<float> value;              // asset value (0 outside clusters)
  std::vector<std::uint8_t> slope_bin;   // [cell * kDirs + d]
  int num_clusters = 0;
  std::array<float, kMaxClusters> cluster_value{};
  std::array<float, kMaxClusters> cluster_cx{};
  std::array<float, kMaxClusters> cluster_cy{};
};

// Crews per cluster. Index kNoCluster is always 0.
using CrewCounts = std::array<std::uint8_t, kMaxClusters + 1>;

// A crew plan: cluster target for each crew (kNoCluster = stay at base).
struct Plan {
  std::uint32_t tick = 0;
  int num_crews = 0;
  std::array<std::uint8_t, kMaxCrews> crew_cluster{};
  float expected_loss = 0.f;
  float cvar = 0.f;
  int moved = 0;
  std::array<std::uint8_t, 3> top_risk{};  // top-3 clusters by P(lost)
};

inline CrewCounts counts_from_plan(const Plan& p) {
  CrewCounts c{};
  for (int i = 0; i < p.num_crews; ++i)
    if (p.crew_cluster[i] != kNoCluster) ++c[p.crew_cluster[i]];
  return c;
}

// Engine health state (HEARTBEAT).
enum class HealthState : std::uint32_t { Ok = 0, Stale = 1, Degraded = 2, Fault = 3 };

}  // namespace embercast
