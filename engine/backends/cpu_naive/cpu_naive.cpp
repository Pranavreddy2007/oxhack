// cpu_naive: the reference backend. Full-grid scan for every member and tick.

#include <algorithm>
#include <cstring>

#include "embercast/forecast.hpp"

namespace embercast {

namespace {

class CpuNaive final : public IForecastBackend {
 public:
  const char* name() const override { return "cpu_naive"; }

  void prepare(int K, int cells) override {
    (void)K;
    a_.assign(cells, 0);
    b_.assign(cells, 0);
  }

  void advance(Ensemble& ens, const StepArgs& s) override {
    for (int k = 0; k < ens.K; ++k) {
      step_reference(*s.terrain, *s.mc, ens.tables[k], s.crews, s.seed, std::uint32_t(k), s.tick,
                     kStreamSpread, ens.member(k), a_.data());
      std::memcpy(ens.member(k), a_.data(), std::size_t(ens.cells));
    }
  }

  void rollout(const Ensemble& ens, const StepArgs& s, int H, ForecastOut& out) override {
    out.K = ens.K;
    out.H = H;
    out.t0 = s.tick - 1;
    out.J = s.terrain->num_clusters;
    std::fill(out.p_burn.begin(), out.p_burn.end(), 0.f);
    for (int k = 0; k < ens.K; ++k) {
      std::memcpy(a_.data(), ens.member(k), std::size_t(ens.cells));
      for (int h = 0; h < H; ++h) {
        step_reference(*s.terrain, *s.mc, ens.tables[k], s.crews, s.seed, std::uint32_t(k),
                       s.tick + std::uint32_t(h), kStreamForecast, a_.data(), b_.data());
        a_.swap(b_);
      }
      reduce_member(*s.terrain, ens.member(k), a_.data(), k, ens.weight[k], out);
    }
    finish_reduce(*s.terrain, ens.weight, out);
  }

 private:
  std::vector<std::uint8_t> a_, b_;
};

}  // namespace

std::unique_ptr<IForecastBackend> make_cpu_naive() { return std::make_unique<CpuNaive>(); }

}  // namespace embercast
