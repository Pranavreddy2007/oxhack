#include "embercast/filter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "embercast/rng.hpp"

namespace embercast {

void systematic_resample(const double* w, int K, double u0, int* idx) {
  double cum = w[0];
  int j = 0;
  for (int i = 0; i < K; ++i) {
    const double u = (u0 + i) / K;
    while (u > cum && j < K - 1) cum += w[++j];
    idx[i] = j;
  }
}

void ParticleFilter::init(const Terrain& t, const ModelConst& mc, const ThetaPrior& prior,
                          const FilterConfig& cfg, int K, std::uint64_t seed,
                          const std::uint8_t* init_state, bool point_theta) {
  t_ = &t;
  mc_ = &mc;
  prior_ = prior;
  cfg_ = cfg;
  seed_ = seed;
  K = std::clamp(K, 1, kMaxK);
  const int cells = t.cells();
  ens_.allocate(K, cells);
  scratch_.assign(std::size_t(K) * cells, 0);
  theta_scratch_.assign(K, Theta{});
  logw_.assign(K, 0.0);
  fp_bits_.assign((std::size_t(cells) + 63) / 64, 0);
  for (int k = 0; k < K; ++k) {
    std::memcpy(ens_.member(k), init_state, std::size_t(cells));
    Theta th;  // prior mean
    if (!point_theta) {
      const auto a = rng4(seed, std::uint32_t(k), 0, 0, kStreamPrior);
      const auto b = rng4(seed, std::uint32_t(k), 1, 0, kStreamPrior);
      th.wind_bias = float(prior.wind_bias_sd * normal01(a[0], a[1]));
      th.spread_scale = float(std::exp(prior.log_s_sd * normal01(a[2], a[3])));
      th.moisture_bias = float(prior.moist_sd * normal01(b[0], b[1]));
    }
    ens_.theta[k] = clamp_theta(prior, th);
  }
}

void ParticleFilter::build_tables(const Weather& est) {
  for (int k = 0; k < ens_.K; ++k) build_thresholds(*mc_, est, ens_.theta[k], ens_.tables[k]);
}

void ParticleFilter::propagate(IForecastBackend& be, const Weather& est, const CrewCounts& crews,
                               std::uint32_t tick, bool stale) {
  if (cfg_.weighting) {
    const float m = stale ? cfg_.stale_inflate : 1.f;
    for (int k = 0; k < ens_.K; ++k) {
      const auto a = rng4(seed_, std::uint32_t(k), tick, 0, kStreamProcess);
      const auto b = rng4(seed_, std::uint32_t(k), tick, 1, kStreamProcess);
      Theta& th = ens_.theta[k];
      th.wind_bias += float(m * cfg_.process_sd.wind_bias * normal01(a[0], a[1]));
      th.spread_scale *= float(std::exp(m * cfg_.process_sd.spread_scale * normal01(a[2], a[3])));
      th.moisture_bias += float(m * cfg_.process_sd.moisture_bias * normal01(b[0], b[1]));
      th = clamp_theta(prior_, th);
    }
  }
  build_tables(est);
  StepArgs s;
  s.terrain = t_;
  s.mc = mc_;
  s.crews = crews;
  s.seed = seed_;
  s.tick = tick;
  be.advance(ens_, s);
}

int ParticleFilter::update(const PerimeterMsg& m, std::uint32_t tick) {
  const int cells = ens_.cells;
  const std::size_t words = fp_bits_.size();
  int observed = 0;
  for (std::size_t i = 0; i < words; ++i) observed += std::popcount(m.observed[i]);
  if (observed == 0) return 0;
  const std::uint8_t assim = std::uint8_t(std::max(1, mc_->burn_ticks / 2));
  double maxl = -1e300;
  for (int k = 0; k < ens_.K; ++k) {
    std::uint8_t* st = ens_.member(k);
    std::fill(fp_bits_.begin(), fp_bits_.end(), 0);
    for (int c = 0; c < cells; ++c)
      if (st[c] != kUnburned) fp_bits_[std::size_t(c) >> 6] |= std::uint64_t(1) << (c & 63);
    long miss = 0, extra = 0;
    for (std::size_t i = 0; i < words; ++i) {
      miss += std::popcount(m.observed[i] & m.fire[i] & ~fp_bits_[i]);
      extra += std::popcount(m.observed[i] & ~m.fire[i] & fp_bits_[i]);
    }
    logw_[k] = std::log(std::max(ens_.weight[k], 1e-300)) -
               cfg_.beta * (cfg_.miss_w * double(miss) + cfg_.extra_w * double(extra));
    maxl = std::max(maxl, logw_[k]);
  }
  if (cfg_.weighting) {
    double sum = 0;
    for (int k = 0; k < ens_.K; ++k) {
      ens_.weight[k] = std::exp(logw_[k] - maxl);
      sum += ens_.weight[k];
    }
    for (int k = 0; k < ens_.K; ++k) ens_.weight[k] /= sum;
  }
  if (cfg_.assimilate) {
    for (int k = 0; k < ens_.K; ++k) {
      std::uint8_t* st = ens_.member(k);
      for (std::size_t i = 0; i < words; ++i) {
        std::uint64_t bits = m.observed[i] & m.fire[i];
        while (bits) {
          const int c = int(i * 64) + std::countr_zero(bits);
          bits &= bits - 1;
          if (c < cells && st[c] == kUnburned && t_->fuel[c] != 0) st[c] = assim;
        }
      }
    }
  }
  if (cfg_.weighting && ens_.K > 1 && ess() < 0.5 * ens_.K) resample(tick);
  return observed;
}

double ParticleFilter::ess() const {
  double s2 = 0;
  for (double w : ens_.weight) s2 += w * w;
  return s2 > 0 ? 1.0 / s2 : 0.0;
}

Theta ParticleFilter::mean_theta() const {
  Theta m{0.f, 0.f, 0.f};
  for (int k = 0; k < ens_.K; ++k) {
    m.wind_bias += float(ens_.weight[k] * ens_.theta[k].wind_bias);
    m.spread_scale += float(ens_.weight[k] * ens_.theta[k].spread_scale);
    m.moisture_bias += float(ens_.weight[k] * ens_.theta[k].moisture_bias);
  }
  return m;
}

void ParticleFilter::resample(std::uint32_t tick) {
  const int K = ens_.K;
  const std::size_t cells = std::size_t(ens_.cells);
  int idx[kMaxK];
  const double u0 = u01(rng4(seed_, tick, 0, 0, kStreamResample)[0]);
  systematic_resample(ens_.weight.data(), K, u0, idx);
  for (int k = 0; k < K; ++k) {
    std::memcpy(scratch_.data() + k * cells, ens_.member(idx[k]), cells);
    theta_scratch_[k] = ens_.theta[idx[k]];
  }
  ens_.state.swap(scratch_);
  for (int k = 0; k < K; ++k) {
    const auto a = rng4(seed_, std::uint32_t(k), tick, 0, kStreamJitter);
    const auto b = rng4(seed_, std::uint32_t(k), tick, 1, kStreamJitter);
    Theta th = theta_scratch_[k];
    th.wind_bias += float(cfg_.jitter_sd.wind_bias * normal01(a[0], a[1]));
    th.spread_scale *= float(std::exp(cfg_.jitter_sd.spread_scale * normal01(a[2], a[3])));
    th.moisture_bias += float(cfg_.jitter_sd.moisture_bias * normal01(b[0], b[1]));
    ens_.theta[k] = clamp_theta(prior_, th);
    ens_.weight[k] = 1.0 / K;
  }
  ++resamples_;
}

}  // namespace embercast
