#pragma once
// IPolicy: decide(context) -> Plan.
// EnsembleRisk: greedy crew-by-crew allocation that minimises
//   E[loss] + lambda * CVaR_alpha[loss] + switch_cost * moved
// over the weighted members. Deterministic (B1) is the same optimiser with
// K = 1 and lambda = 0. FrontNearest (B0) uses no model. The oracle uses
// greedy_allocate() on the true future loss.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "embercast/forecast.hpp"
#include "embercast/model.hpp"
#include "embercast/types.hpp"

namespace embercast {

struct PolicyContext {
  const Terrain* terrain = nullptr;
  const ModelConst* mc = nullptr;
  int crews = 0;
  std::uint32_t tick = 0;
  Plan prev;                                   // previous plan
  const ForecastOut* fc = nullptr;             // null for B0
  const std::vector<double>* weight = nullptr; // member weights
  CrewCounts rollout_crews{};                  // crews used in the rollout
  const std::vector<std::uint8_t>* observed_fire = nullptr;  // per cell 0/1 (B0)
};

struct RiskParams {
  double lambda = 0.5;
  double alpha = 0.95;
  double switch_cost = 4.0;  // loss units per moved crew
};

// Weighted CVaR_alpha: mean of the worst (1 - alpha) probability mass.
// `order` is scratch of size K. No allocation.
double weighted_cvar(const double* loss, const double* w, int K, double alpha, int* order);

// Greedy allocation over member loss rows L_free[k][j] (crew-free loss).
// Returns crew counts per cluster and fills the risk numbers.
struct GreedyResult {
  CrewCounts counts{};
  double eloss = 0, cvar = 0, objective = 0;
};
GreedyResult greedy_allocate(const float* l_free, const double* w, int K, int J, int C,
                             const ModelConst& mc, const RiskParams& rp, const CrewCounts& prev);

// Map crew counts to crew ids. Crews stay where they are when possible.
Plan counts_to_plan(const CrewCounts& counts, const Plan& prev, int C, int J,
                    std::uint32_t tick);

class IPolicy {
 public:
  virtual ~IPolicy() = default;
  virtual const char* name() const = 0;
  virtual bool needs_forecast() const = 0;
  virtual Plan decide(const PolicyContext& ctx) = 0;
};

std::unique_ptr<IPolicy> make_ensemble_risk(const RiskParams& rp);  // also B1 with lambda = 0
std::unique_ptr<IPolicy> make_front_nearest();

}  // namespace embercast
