#pragma once
// Particle filter: K members, each with its own theta and state copy.
// Weights from perimeter scans, systematic resampling when ESS < K/2,
// theta jitter after a resample.

#include <cstdint>
#include <vector>

#include "embercast/forecast.hpp"
#include "embercast/frame.hpp"
#include "embercast/model.hpp"

namespace embercast {

struct FilterConfig {
  bool weighting = true;     // false: no weights, no resample, no process noise (B1)
  bool assimilate = true;    // set observed fire cells to BURNING in each member
  float beta = 0.05f;        // likelihood temperature
  float miss_w = 1.0f;       // observed fire, member has no fire
  float extra_w = 0.3f;      // observed no fire, member has fire (scans have false negatives)
  Theta process_sd{0.015f, 0.01f, 0.004f};  // per tick (s: multiplicative)
  float stale_inflate = 3.0f;
  Theta jitter_sd{0.05f, 0.05f, 0.02f};
};

class ParticleFilter {
 public:
  // Allocates all memory. `init_state` is the known initial fire state.
  void init(const Terrain& t, const ModelConst& mc, const ThetaPrior& prior,
            const FilterConfig& cfg, int K, std::uint64_t seed, const std::uint8_t* init_state,
            bool point_theta);

  // Build per-member threshold tables for the weather estimate.
  void build_tables(const Weather& est);
  // Advance all members to `tick` (process noise first).
  void propagate(IForecastBackend& be, const Weather& est, const CrewCounts& crews,
                 std::uint32_t tick, bool stale);
  // Apply one perimeter scan. Returns the number of observed cells.
  int update(const PerimeterMsg& m, std::uint32_t tick);

  double ess() const;
  Ensemble& ensemble() { return ens_; }
  const Ensemble& ensemble() const { return ens_; }
  int resamples() const { return resamples_; }
  Theta mean_theta() const;

 private:
  void resample(std::uint32_t tick);

  const Terrain* t_ = nullptr;
  const ModelConst* mc_ = nullptr;
  ThetaPrior prior_;
  FilterConfig cfg_;
  std::uint64_t seed_ = 0;
  Ensemble ens_;
  std::vector<std::uint8_t> scratch_;    // K * cells, for resampling
  std::vector<Theta> theta_scratch_;
  std::vector<double> logw_;
  std::vector<std::uint64_t> fp_bits_;   // one member footprint bitset
  int resamples_ = 0;
};

// Systematic resampling indices. `u0` in [0, 1). Exposed for tests.
void systematic_resample(const double* w, int K, double u0, int* idx);

}  // namespace embercast
