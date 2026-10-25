#include "embercast/forecast.hpp"

#include <algorithm>
#include <stdexcept>

namespace embercast {

std::unique_ptr<IForecastBackend> make_cpu_naive();
std::unique_ptr<IForecastBackend> make_cpu_opt(int threads);

std::unique_ptr<IForecastBackend> make_backend(const std::string& name, int threads) {
  if (name == "cpu_naive") return make_cpu_naive();
  if (name == "cpu_opt") return make_cpu_opt(threads);
  throw std::invalid_argument("unknown backend: " + name);
}

void reduce_member(const Terrain& t, const std::uint8_t* start, const std::uint8_t* end, int k,
                   double w, ForecastOut& out) {
  const float wf = float(w);
  float* loss = out.loss.data() + std::size_t(k) * kMaxClusters;
  std::fill(loss, loss + kMaxClusters, 0.f);
  int area = 0;
  for (int c = 0; c < out.cells; ++c) {
    if (end[c] == kUnburned) continue;
    ++area;
    out.p_burn[c] += wf;
    if (start[c] == kUnburned && t.cluster[c] != kNoCluster) loss[t.cluster[c]] += t.value[c];
  }
  out.area[k] = area;
}

void finish_reduce(const Terrain& t, const std::vector<double>& weight, ForecastOut& out) {
  std::fill(out.p_lost.begin(), out.p_lost.end(), 0.f);
  for (int k = 0; k < out.K; ++k) {
    const float* loss = out.loss.data() + std::size_t(k) * kMaxClusters;
    for (int j = 0; j < t.num_clusters; ++j)
      if (loss[j] >= kLostFraction * t.cluster_value[j] && loss[j] > 0.f)
        out.p_lost[j] += float(weight[k]);
  }
}

}  // namespace embercast
