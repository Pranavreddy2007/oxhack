#pragma once
// IForecastBackend: one-tick ensemble advance (filter) and H-tick rollout
// (forecast). Backends: cpu_naive (reference), cpu_opt (sparse front,
// bitsets, thread pool), opencl (optional, off by default).
//
// All backends must give bit-identical outputs for the same inputs.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "embercast/model.hpp"
#include "embercast/types.hpp"

namespace embercast {

// Ensemble state in structure-of-arrays form. Allocated once.
struct Ensemble {
  int K = 0;
  int cells = 0;
  std::vector<std::uint8_t> state;       // K * cells
  std::vector<Theta> theta;              // K
  std::vector<double> weight;            // K, sums to 1
  std::vector<ThresholdTable> tables;    // K, rebuilt by the caller
  std::uint8_t* member(int k) { return state.data() + std::size_t(k) * cells; }
  const std::uint8_t* member(int k) const { return state.data() + std::size_t(k) * cells; }
  void allocate(int k, int c) {
    K = k;
    cells = c;
    state.assign(std::size_t(k) * c, kUnburned);
    theta.assign(k, Theta{});
    weight.assign(k, 1.0 / k);
    tables.resize(k);
  }
};

struct StepArgs {
  const Terrain* terrain = nullptr;
  const ModelConst* mc = nullptr;
  CrewCounts crews{};
  std::uint64_t seed = 0;
  std::uint32_t tick = 0;  // the tick that the output represents (first tick for rollout)
};

// Forecast output. Allocated once with the maximum sizes.
struct ForecastOut {
  int K = 0, cells = 0, J = 0, H = 0;
  std::uint32_t t0 = 0;
  std::vector<float> p_burn;     // cells: weighted P(cell in footprint at t0 + H)
  std::vector<float> loss;       // K * kMaxClusters: value newly burned in cluster j
  std::vector<int> area;         // K: footprint size at t0 + H
  std::vector<float> p_lost;     // kMaxClusters: weighted P(cluster lost)
  void allocate(int k, int c) {
    K = k;
    cells = c;
    p_burn.assign(c, 0.f);
    loss.assign(std::size_t(k) * kMaxClusters, 0.f);
    area.assign(k, 0);
    p_lost.assign(kMaxClusters, 0.f);
  }
};

// A cluster is "lost" in a member when the newly burned value reaches this
// fraction of the cluster value.
inline constexpr float kLostFraction = 0.25f;

class IForecastBackend {
 public:
  virtual ~IForecastBackend() = default;
  virtual const char* name() const = 0;
  // Allocate scratch memory for up to K members on this grid. Call once.
  virtual void prepare(int K, int cells) = 0;
  // Advance every member one tick in place (filter propagation).
  // Uses ens.tables[k] and the RNG stream kStreamSpread.
  virtual void advance(Ensemble& ens, const StepArgs& a) = 0;
  // Roll every member forward H ticks from its current state (not changed).
  // Uses ens.tables[k], ens.weight and the RNG stream kStreamForecast.
  virtual void rollout(const Ensemble& ens, const StepArgs& a, int H, ForecastOut& out) = 0;
};

// Names: "cpu_naive", "cpu_opt", "opencl" (only with EMBERCAST_OPENCL=ON; throws
// when no OpenCL device is found). threads <= 0 means hardware concurrency.
std::unique_ptr<IForecastBackend> make_backend(const std::string& name, int threads = 0);

// Shared reduction: computes p_burn, loss, area and p_lost from the final
// footprints. Both CPU backends call it in member order, so float sums
// have the same order.
void reduce_member(const Terrain& t, const std::uint8_t* start, const std::uint8_t* end,
                   int k, double w, ForecastOut& out);
void finish_reduce(const Terrain& t, const std::vector<double>& weight, ForecastOut& out);

}  // namespace embercast
