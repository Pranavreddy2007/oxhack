#include "embercast/policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace embercast {

double weighted_cvar(const double* loss, const double* w, int K, double alpha, int* order) {
  for (int k = 0; k < K; ++k) order[k] = k;
  std::sort(order, order + K, [&](int a, int b) {
    return loss[a] != loss[b] ? loss[a] > loss[b] : a < b;
  });
  const double tail = 1.0 - alpha;
  if (tail <= 0.0) return loss[order[0]];
  double mass = 0, acc = 0;
  for (int i = 0; i < K && mass < tail; ++i) {
    const double take = std::min(w[order[i]], tail - mass);
    acc += take * loss[order[i]];
    mass += take;
  }
  return mass > 0 ? acc / mass : 0.0;
}

GreedyResult greedy_allocate(const float* l_free, const double* w, int K, int J, int C,
                             const ModelConst& mc, const RiskParams& rp, const CrewCounts& prev) {
  double keep[kMaxCrews + 2];
  for (int n = 0; n <= kMaxCrews + 1; ++n) keep[n] = 1.0 - crew_efficacy(mc, n);
  double member_loss[kMaxK];
  int order[kMaxK];
  auto eval = [&](const CrewCounts& n, double& el, double& cv) {
    el = 0;
    for (int k = 0; k < K; ++k) {
      const float* row = l_free + std::size_t(k) * kMaxClusters;
      double s = 0;
      for (int j = 0; j < J; ++j) s += keep[n[j]] * row[j];
      member_loss[k] = s;
      el += w[k] * s;
    }
    cv = rp.lambda != 0.0 ? weighted_cvar(member_loss, w, K, rp.alpha, order) : 0.0;
  };
  auto moved = [&](const CrewCounts& n) {
    int m = 0;
    for (int j = 0; j < J; ++j) m += std::max(0, int(n[j]) - int(prev[j]));
    return m;
  };
  GreedyResult r;
  for (int c = 0; c < C; ++c) {
    double best = std::numeric_limits<double>::infinity();
    int best_j = 0;
    for (int j = 0; j < J; ++j) {
      CrewCounts n = r.counts;
      ++n[j];
      double el, cv;
      eval(n, el, cv);
      const double obj = el + rp.lambda * cv + rp.switch_cost * moved(n);
      if (obj < best) {
        best = obj;
        best_j = j;
      }
    }
    ++r.counts[best_j];
  }
  eval(r.counts, r.eloss, r.cvar);
  if (rp.lambda == 0.0) {
    int scratch[kMaxK];
    r.cvar = weighted_cvar(member_loss, w, K, rp.alpha, scratch);
  }
  r.objective = r.eloss + rp.lambda * r.cvar + rp.switch_cost * moved(r.counts);
  return r;
}

Plan counts_to_plan(const CrewCounts& counts, const Plan& prev, int C, int J,
                    std::uint32_t tick) {
  Plan p;
  p.tick = tick;
  p.num_crews = C;
  CrewCounts left = counts;
  std::array<bool, kMaxCrews> placed{};
  // Keep crews at their current cluster when the quota allows.
  for (int i = 0; i < C; ++i) {
    const std::uint8_t j = i < prev.num_crews ? prev.crew_cluster[i] : kNoCluster;
    if (j != kNoCluster && j < J && left[j] > 0) {
      p.crew_cluster[i] = j;
      --left[j];
      placed[i] = true;
    }
  }
  int j = 0;
  for (int i = 0; i < C; ++i) {
    if (placed[i]) continue;
    while (j < J && left[j] == 0) ++j;
    if (j >= J) {
      p.crew_cluster[i] = kNoCluster;
      continue;
    }
    p.crew_cluster[i] = std::uint8_t(j);
    --left[j];
  }
  for (int i = 0; i < C; ++i)
    if (i >= prev.num_crews || p.crew_cluster[i] != prev.crew_cluster[i]) ++p.moved;
  return p;
}

namespace {

void fill_top_risk(const ForecastOut* fc, int J, Plan& p) {
  std::array<int, kMaxClusters> idx{};
  for (int j = 0; j < J; ++j) idx[j] = j;
  if (fc)
    std::stable_sort(idx.begin(), idx.begin() + J,
                     [&](int a, int b) { return fc->p_lost[a] > fc->p_lost[b]; });
  for (int i = 0; i < 3; ++i) p.top_risk[i] = i < J ? std::uint8_t(idx[i]) : kNoCluster;
}

class EnsembleRisk final : public IPolicy {
 public:
  explicit EnsembleRisk(const RiskParams& rp) : rp_(rp) {}
  const char* name() const override { return rp_.lambda == 0.0 ? "deterministic" : "ensemble"; }
  bool needs_forecast() const override { return true; }

  Plan decide(const PolicyContext& ctx) override {
    const ForecastOut& fc = *ctx.fc;
    const int K = fc.K, J = ctx.terrain->num_clusters;
    // Crew-free loss: undo the crew factor that the rollout used (linear model).
    for (int k = 0; k < K; ++k)
      for (int j = 0; j < kMaxClusters; ++j) {
        const double keep = 1.0 - crew_efficacy(*ctx.mc, j < J ? ctx.rollout_crews[j] : 0);
        l_free_[std::size_t(k) * kMaxClusters + j] =
            float(fc.loss[std::size_t(k) * kMaxClusters + j] / keep);
      }
    const CrewCounts prev = counts_from_plan(ctx.prev);
    const GreedyResult g = greedy_allocate(l_free_.data(), ctx.weight->data(), K, J, ctx.crews,
                                           *ctx.mc, rp_, prev);
    Plan p = counts_to_plan(g.counts, ctx.prev, ctx.crews, J, ctx.tick);
    p.expected_loss = float(g.eloss);
    p.cvar = float(g.cvar);
    fill_top_risk(&fc, J, p);
    return p;
  }

 private:
  RiskParams rp_;
  std::array<float, std::size_t(kMaxK) * kMaxClusters> l_free_{};
};

// B0: score_j = value_j / (1 + distance from the cluster centre to the
// nearest observed fire cell). Greedy on score_j * (e(n+1) - e(n)).
class FrontNearest final : public IPolicy {
 public:
  const char* name() const override { return "front_nearest"; }
  bool needs_forecast() const override { return false; }

  Plan decide(const PolicyContext& ctx) override {
    const Terrain& t = *ctx.terrain;
    const int J = t.num_clusters, n = t.n;
    std::array<double, kMaxClusters> dmin;
    dmin.fill(1e9);
    const auto& fire = *ctx.observed_fire;
    for (int c = 0; c < t.cells(); ++c) {
      if (!fire[c]) continue;
      const double x = c % n, y = c / n;
      for (int j = 0; j < J; ++j) {
        const double dx = x - t.cluster_cx[j], dy = y - t.cluster_cy[j];
        dmin[j] = std::min(dmin[j], dx * dx + dy * dy);
      }
    }
    std::array<double, kMaxClusters> score{};
    for (int j = 0; j < J; ++j) score[j] = t.cluster_value[j] / (1.0 + std::sqrt(dmin[j]));
    CrewCounts counts{};
    for (int c = 0; c < ctx.crews; ++c) {
      int best = 0;
      double bv = -1;
      for (int j = 0; j < J; ++j) {
        const double gain = score[j] * (crew_efficacy(*ctx.mc, counts[j] + 1) -
                                        crew_efficacy(*ctx.mc, counts[j]));
        if (gain > bv) {
          bv = gain;
          best = j;
        }
      }
      ++counts[best];
    }
    Plan p = counts_to_plan(counts, ctx.prev, ctx.crews, J, ctx.tick);
    fill_top_risk(nullptr, J, p);
    return p;
  }
};

}  // namespace

std::unique_ptr<IPolicy> make_ensemble_risk(const RiskParams& rp) {
  return std::make_unique<EnsembleRisk>(rp);
}
std::unique_ptr<IPolicy> make_front_nearest() { return std::make_unique<FrontNearest>(); }

}  // namespace embercast
