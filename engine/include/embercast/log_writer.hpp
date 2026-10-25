#pragma once
// Off-hot-path log writer. The engine thread pushes fixed-size text lines
// into an SPSC ring. A separate writer thread drains the ring to a file.
// If the ring is full, the line is dropped and counted (never blocks).

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "embercast/transport.hpp"

namespace embercast {

class LogWriter {
 public:
  using Line = std::array<char, 384>;

  explicit LogWriter(const std::string& path) : ring_(std::make_unique<SpscRing<Line, 1024>>()) {
    if (!path.empty()) f_ = std::fopen(path.c_str(), "ab");
    if (f_) th_ = std::thread([this] { loop(); });
  }
  ~LogWriter() {
    stop_.store(true);
    if (th_.joinable()) th_.join();
    if (f_) std::fclose(f_);
  }
  bool enabled() const { return f_ != nullptr; }
  void push(const char* text) {
    if (!f_) return;
    Line l{};
    const std::size_t n = std::min(std::strlen(text), l.size() - 1);
    std::memcpy(l.data(), text, n);
    l[n] = '\0';
    if (!ring_->push(l)) ++dropped_;
  }
  std::uint64_t dropped() const { return dropped_.load(); }

 private:
  void loop() {
    Line l;
    for (;;) {
      bool any = false;
      while (ring_->pop(l)) {
        std::fputs(l.data(), f_);
        any = true;
      }
      if (any) std::fflush(f_);
      if (stop_.load() && ring_->empty()) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  std::unique_ptr<SpscRing<Line, 1024>> ring_;
  std::FILE* f_ = nullptr;
  std::thread th_;
  std::atomic<bool> stop_{false};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace embercast
