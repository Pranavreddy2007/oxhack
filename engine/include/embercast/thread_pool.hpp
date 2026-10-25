#pragma once
// Fixed thread pool with a per-call barrier. No locks and no allocation in
// run(): workers wait on an atomic generation counter (futex-based
// std::atomic::wait), take indices from an atomic counter, and the caller
// waits until all indices are done. The caller thread also works.

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace embercast {

class ThreadPool {
 public:
  explicit ThreadPool(int threads) {
    if (threads <= 0) threads = int(std::max(1u, std::thread::hardware_concurrency()));
    n_ = threads;
    for (int i = 1; i < n_; ++i) workers_.emplace_back([this, i] { loop(i); });
  }
  ~ThreadPool() {
    stop_.store(true);
    gen_.fetch_add(1);
    gen_.notify_all();
    for (auto& t : workers_) t.join();
  }
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  int size() const { return n_; }

  // Calls fn(index, worker) for index in [0, count). Blocks until done.
  template <class F>
  void run(int count, F& fn) {
    if (count <= 0) return;
    if (n_ == 1 || count == 1) {
      for (int i = 0; i < count; ++i) fn(i, 0);
      return;
    }
    fn_ = [](void* ctx, int i, int w) { (*static_cast<F*>(ctx))(i, w); };
    ctx_ = &fn;
    count_.store(count);
    done_.store(0);
    // The high 32 bits of next_ carry the run id. A late worker from an
    // earlier run sees a different id and stops without taking an index.
    const std::uint64_t run = ++run_;
    next_.store(run << 32);
    gen_.fetch_add(1);
    gen_.notify_all();
    work(0, run);
    while (done_.load() < count) {
    }
  }

 private:
  void work(int w, std::uint64_t run) {
    std::uint64_t v = next_.load();
    for (;;) {
      // Take an index only if it belongs to this run (compare-exchange, so a
      // late worker never consumes an index of a newer run).
      if ((v >> 32) != run || int(v & 0xFFFFFFFFu) >= count_.load()) return;
      if (!next_.compare_exchange_weak(v, v + 1)) continue;
      fn_(ctx_, int(v & 0xFFFFFFFFu), w);
      done_.fetch_add(1);
      v = next_.load();
    }
  }
  void loop(int w) {
    std::uint64_t seen = 0;
    for (;;) {
      gen_.wait(seen);
      seen = gen_.load();
      if (stop_.load()) return;
      work(w, next_.load() >> 32);
    }
  }

  int n_ = 1;
  std::vector<std::thread> workers_;
  std::atomic<std::uint64_t> gen_{0};
  std::atomic<std::uint64_t> next_{0};
  std::atomic<int> done_{0}, count_{0};
  std::uint64_t run_ = 0;
  std::atomic<bool> stop_{false};
  void (*fn_)(void*, int, int) = nullptr;
  void* ctx_ = nullptr;
};

}  // namespace embercast
