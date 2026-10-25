#pragma once
// Monotonic nanosecond clock.

#include <chrono>
#include <cstdint>

namespace embercast {

inline std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace embercast
