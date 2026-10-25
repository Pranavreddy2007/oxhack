#pragma once
// Latency recorder. Raw samples go to a buffer that is allocated before the
// measured loop (no allocation while recording). Summary statistics are
// computed after the loop.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace embercast {

struct LatencyStats {
  std::size_t count = 0;
  double mean = 0, p50 = 0, p95 = 0, p99 = 0, p999 = -1, max = 0;  // ns; p999 < 0: not reported
};

class LatencyRecorder {
 public:
  explicit LatencyRecorder(std::size_t capacity = 0) { samples_.reserve(capacity); }
  void reserve(std::size_t n) { samples_.reserve(n); }
  // Drops the sample if the buffer is full (never allocates).
  void record(std::uint64_t ns) {
    if (samples_.size() < samples_.capacity()) samples_.push_back(ns);
    else ++dropped_;
  }
  const std::vector<std::uint64_t>& samples() const { return samples_; }
  std::size_t dropped() const { return dropped_; }

  LatencyStats stats() const {
    LatencyStats s;
    s.count = samples_.size();
    if (s.count == 0) return s;
    std::vector<std::uint64_t> v = samples_;
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (auto x : v) sum += double(x);
    s.mean = sum / double(v.size());
    auto q = [&](double p) {  // nearest-rank percentile
      const std::size_t r = std::size_t(std::ceil(p * double(v.size())));
      return double(v[std::min(v.size() - 1, r == 0 ? 0 : r - 1)]);
    };
    s.p50 = q(0.50);
    s.p95 = q(0.95);
    s.p99 = q(0.99);
    if (v.size() >= 10000) s.p999 = q(0.999);
    s.max = double(v.back());
    return s;
  }

 private:
  std::vector<std::uint64_t> samples_;
  std::size_t dropped_ = 0;
};

}  // namespace embercast
