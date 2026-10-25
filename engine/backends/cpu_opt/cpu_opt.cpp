// cpu_opt: sparse active front, per-thread arenas, fixed thread pool.
//
// Only UNBURNED cells next to a BURNING cell can change to BURNING, and
// only BURNING cells count down. cpu_opt visits only those cells. Each
// candidate cell uses the same RNG draws and thresholds as cpu_naive, and
// the ignition result does not depend on the order of the neighbour checks,
// so the output is bit-identical to cpu_naive (tested, AC2).
//
// Parallelism: members are independent. The pool runs one member per task.
// The reduction keeps the cpu_naive float summation order: member losses
// in cell order, and p_burn per cell in member order (parallel over cell
// blocks).

#include <algorithm>
#include <cstring>
#include <memory>

#include "embercast/forecast.hpp"
#include "embercast/thread_pool.hpp"

namespace embercast {

namespace {

struct Arena {
  std::vector<std::uint8_t> grid;       // rollout working state
  std::vector<int> burning, next_burning, cand, ignite;
  std::vector<std::uint32_t> stamp;
  std::uint32_t gen = 0;
};

class CpuOpt final : public IForecastBackend {
 public:
  explicit CpuOpt(int threads) : pool_(threads) {}
  const char* name() const override { return "cpu_opt"; }

  void prepare(int K, int cells) override {
    arenas_.resize(std::size_t(pool_.size()));
    for (auto& a : arenas_) {
      a.grid.assign(cells, 0);
      a.burning.reserve(cells);
      a.next_burning.reserve(cells);
      a.cand.reserve(cells);
      a.ignite.reserve(cells);
      a.stamp.assign(cells, 0);
      a.gen = 0;
    }
    ends_.assign(std::size_t(K) * cells, 0);
    cells_ = cells;
  }

  void advance(Ensemble& ens, const StepArgs& s) override {
    auto fn = [&](int k, int w) {
      Arena& a = arenas_[std::size_t(w)];
      std::uint8_t* st = ens.member(k);
      collect_burning(st, a);
      step_sparse(*s.terrain, *s.mc, ens.tables[k], s.crews, s.seed, std::uint32_t(k), s.tick,
                  kStreamSpread, st, a);
    };
    pool_.run(ens.K, fn);
  }

  void rollout(const Ensemble& ens, const StepArgs& s, int H, ForecastOut& out) override {
    out.K = ens.K;
    out.H = H;
    out.t0 = s.tick - 1;
    out.J = s.terrain->num_clusters;
    const Terrain& t = *s.terrain;
    auto member = [&](int k, int w) {
      Arena& a = arenas_[std::size_t(w)];
      std::uint8_t* g = ends_.data() + std::size_t(k) * cells_;
      std::memcpy(g, ens.member(k), std::size_t(cells_));
      collect_burning(g, a);
      for (int h = 0; h < H; ++h)
        step_sparse(t, *s.mc, ens.tables[k], s.crews, s.seed, std::uint32_t(k),
                    s.tick + std::uint32_t(h), kStreamForecast, g, a);
      // Member loss and area, in cell order (same order as cpu_naive).
      const std::uint8_t* start = ens.member(k);
      float* loss = out.loss.data() + std::size_t(k) * kMaxClusters;
      std::fill(loss, loss + kMaxClusters, 0.f);
      int area = 0;
      for (int c = 0; c < cells_; ++c) {
        if (g[c] == kUnburned) continue;
        ++area;
        if (start[c] == kUnburned && t.cluster[c] != kNoCluster) loss[t.cluster[c]] += t.value[c];
      }
      out.area[k] = area;
    };
    pool_.run(ens.K, member);
    // p_burn: per cell, sum the weights in member order.
    constexpr int kBlock = 1024;
    const int blocks = (cells_ + kBlock - 1) / kBlock;
    auto reduce = [&](int b, int) {
      const int c0 = b * kBlock, c1 = std::min(cells_, c0 + kBlock);
      float* p = out.p_burn.data();
      std::fill(p + c0, p + c1, 0.f);
      for (int k = 0; k < ens.K; ++k) {
        const float wf = float(ens.weight[k]);
        const std::uint8_t* g = ends_.data() + std::size_t(k) * cells_;
        for (int c = c0; c < c1; ++c)
          if (g[c] != kUnburned) p[c] += wf;
      }
    };
    pool_.run(blocks, reduce);
    finish_reduce(t, ens.weight, out);
  }

 private:
  void collect_burning(const std::uint8_t* st, Arena& a) {
    a.burning.clear();
    for (int c = 0; c < cells_; ++c)
      if (is_burning(st[c])) a.burning.push_back(c);
  }

  // One tick in place. Uses a.burning as the BURNING cells of `st`, and
  // leaves the new BURNING cells in a.burning.
  static void step_sparse(const Terrain& t, const ModelConst& mc, const ThresholdTable& tab,
                          const CrewCounts& crews, std::uint64_t seed, std::uint32_t member,
                          std::uint32_t tick, std::uint32_t stream, std::uint8_t* st, Arena& a) {
    const int n = t.n;
    if (++a.gen == 0) {
      std::fill(a.stamp.begin(), a.stamp.end(), 0u);
      a.gen = 1;
    }
    a.cand.clear();
    for (int b : a.burning) {
      const int x = b % n, y = b / n;
      for (int d = 0; d < kDirs; ++d) {
        const int nx = x - kDx[d], ny = y - kDy[d];  // cells that have b as neighbour d
        if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
        const int c = ny * n + nx;
        if (st[c] != kUnburned || t.fuel[c] == 0 || a.stamp[c] == a.gen) continue;
        a.stamp[c] = a.gen;
        a.cand.push_back(c);
      }
    }
    a.ignite.clear();
    for (int c : a.cand) {
      const int x = c % n, y = c / n;
      const int fuel = t.fuel[c];
      const int ncrew = crews[t.cluster[c]];
      bool have[2] = {false, false};
      u32x4 dr[2];
      for (int d = 0; d < kDirs; ++d) {
        const int nx = x + kDx[d], ny = y + kDy[d];
        if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
        if (!is_burning(st[ny * n + nx])) continue;
        const int lane = d >> 2;
        if (!have[lane]) {
          dr[lane] = spread_draws(seed, member, tick, std::uint32_t(c), stream, lane);
          have[lane] = true;
        }
        if (dr[lane][d & 3] < tab.t[fuel][d][t.slope_bin[std::size_t(c) * kDirs + d]][ncrew]) {
          a.ignite.push_back(c);
          break;
        }
      }
    }
    a.next_burning.clear();
    for (int b : a.burning) {
      const std::uint8_t v = st[b];
      if (v == 1) {
        st[b] = kBurnt;
      } else {
        st[b] = std::uint8_t(v - 1);
        a.next_burning.push_back(b);
      }
    }
    const std::uint8_t B = std::uint8_t(mc.burn_ticks);
    for (int c : a.ignite) {
      st[c] = B;
      a.next_burning.push_back(c);
    }
    a.burning.swap(a.next_burning);
  }

  ThreadPool pool_;
  std::vector<Arena> arenas_;
  std::vector<std::uint8_t> ends_;
  int cells_ = 0;
};

}  // namespace

std::unique_ptr<IForecastBackend> make_cpu_opt(int threads) {
  return std::make_unique<CpuOpt>(threads);
}

}  // namespace embercast
