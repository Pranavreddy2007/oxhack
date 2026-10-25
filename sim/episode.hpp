#pragma once
// One episode: truth simulator + one policy, with common random numbers.
// Collects realised loss, prediction metrics, decisions, and stage timings.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "embercast/engine.hpp"
#include "scenario.hpp"

namespace embercast {

struct PredMetrics {
  long n = 0;                       // evaluated cell-forecasts
  double brier = 0, logloss = 0;    // sums
  std::array<double, 10> bin_p{}, bin_y{};
  std::array<long, 10> bin_n{};
  long crps_n = 0;
  double crps = 0;                  // burned-area CRPS sum (cells)
  double var_sum = 0, sqerr_sum = 0;  // spread-skill
  long lost_n = 0;
  double lost_brier = 0;
};

struct EpisodeOptions {
  ScenarioOptions scenario;
  EngineConfig engine;        // base config; the policy name selects the variant
  int eval_radius = 12;       // cells near the true footprint that the metrics use
  bool collect_pred = true;
};

struct EpisodeResult {
  std::uint64_t seed = 0;
  std::string policy;
  std::string stress;
  double loss = 0;            // realised asset loss over the episode
  double total_value = 0;
  int final_area = 0;
  int resamples = 0;
  std::vector<Plan> plans;
  PredMetrics pm;
  EngineCounters counters;
  std::vector<std::uint64_t> filter_ns, rollout_ns, policy_ns, decision_ns;
  std::uint64_t first_decision_ns = 0;  // cold start (tick 0)
};

// policy: "ensemble", "deterministic" (B1), "front_nearest" (B0), "oracle".
EpisodeResult run_episode(std::uint64_t seed, const std::string& policy,
                          const EpisodeOptions& opt);

// One JSON line per decision. Timings are not included, so the log is
// byte-identical for the same seed and build (AC1).
std::string decision_log(const EpisodeResult& r);

bool parse_policy(const std::string& s, PolicyKind& k);

}  // namespace embercast
