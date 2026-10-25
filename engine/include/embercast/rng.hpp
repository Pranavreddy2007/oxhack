#pragma once
// Counter-based RNG: Philox4x32-10 (Salmon et al., SC'11).
// Key = seed (64 bit). Counter = (a, b, c, stream). The call has no state.
// The same inputs give the same outputs on every backend.

#include <array>
#include <cmath>
#include <cstdint>

namespace embercast {

using u32x4 = std::array<std::uint32_t, 4>;

inline u32x4 philox4x32_10(u32x4 ctr, std::uint32_t k0, std::uint32_t k1) {
  constexpr std::uint32_t M0 = 0xD2511F53u, M1 = 0xCD9E8D57u;
  constexpr std::uint32_t W0 = 0x9E3779B9u, W1 = 0xBB67AE85u;
  for (int r = 0; r < 10; ++r) {
    const std::uint64_t p0 = std::uint64_t(M0) * ctr[0];
    const std::uint64_t p1 = std::uint64_t(M1) * ctr[2];
    const std::uint32_t hi0 = std::uint32_t(p0 >> 32), lo0 = std::uint32_t(p0);
    const std::uint32_t hi1 = std::uint32_t(p1 >> 32), lo1 = std::uint32_t(p1);
    ctr = {hi1 ^ ctr[1] ^ k0, lo1, hi0 ^ ctr[3] ^ k1, lo0};
    k0 += W0;
    k1 += W1;
  }
  return ctr;
}

// Stream ids. Each use of random numbers has its own stream.
enum Stream : std::uint32_t {
  kStreamSpread = 0x100,     // + lane (0, 1): filter / truth spread draws
  kStreamForecast = 0x200,   // + lane (0, 1): forecast rollout draws
  kStreamWeather = 0x300,
  kStreamTheta = 0x310,
  kStreamSensor = 0x320,
  kStreamScan = 0x330,
  kStreamScenario = 0x340,
  kStreamPrior = 0x350,
  kStreamJitter = 0x360,
  kStreamResample = 0x370,
  kStreamProcess = 0x380,
};

// Member id that the truth simulator uses.
inline constexpr std::uint32_t kTruthMember = 0xFFFFFFFFu;

inline u32x4 rng4(std::uint64_t seed, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                  std::uint32_t stream) {
  return philox4x32_10({a, b, c, stream}, std::uint32_t(seed), std::uint32_t(seed >> 32));
}

// Uniform in [0, 1) with 32-bit resolution.
inline double u01(std::uint32_t x) { return x * (1.0 / 4294967296.0); }

// Standard normal from two uniform words (Box-Muller).
inline double normal01(std::uint32_t a, std::uint32_t b) {
  const double u1 = (a + 1.0) * (1.0 / 4294967297.0);  // (0, 1]
  const double u2 = u01(b);
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

// splitmix64, for seed derivation only.
inline std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

}  // namespace embercast
