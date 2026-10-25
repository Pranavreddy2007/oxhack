#pragma once
// Wire frames (PRD.md Section 8). Little-endian. Fixed 32-byte header.
// The CRC32 (IEEE) covers the header (with crc32 = 0) and the payload.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "embercast/types.hpp"

namespace embercast {

inline constexpr std::uint32_t kFrameMagic = 0x45434D42u;  // "BMCE" on the wire
inline constexpr std::uint16_t kFrameVersion = 1;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::size_t kBitsetWords = kMaxCells / 64;
// Largest payload: FORECAST map (header fields + one byte per cell).
inline constexpr std::size_t kMaxPayload = 16 + kMaxCells;
inline constexpr std::size_t kMaxFrame = kHeaderSize + kMaxPayload;

enum class FrameType : std::uint16_t {
  Sensor = 1,
  Perimeter = 2,
  Decision = 3,
  Heartbeat = 4,
  Forecast = 5,  // extension: quantised P(burn) map for the dashboard
};

enum SensorFlags : std::uint32_t {
  kFlagDropout = 1u << 0,
  kFlagStuck = 1u << 1,
  kFlagOutOfRange = 1u << 2,
};

struct FrameHeader {
  std::uint32_t magic = kFrameMagic;
  std::uint16_t version = kFrameVersion;
  std::uint16_t type = 0;
  std::uint32_t seq = 0;
  std::uint32_t t_sim_tick = 0;
  std::uint64_t t_send_ns = 0;
  std::uint32_t payload_len = 0;
  std::uint32_t crc32 = 0;
};

struct SensorMsg {
  std::uint32_t seq = 0;
  std::uint32_t tick = 0;
  std::uint64_t t_send_ns = 0;
  float wind_speed = 0.f, wind_dir = 0.f, humidity = 0.f, temp = 0.f;
  std::uint32_t flags = 0;
};

struct PerimeterMsg {
  std::uint32_t seq = 0;
  std::uint32_t tick = 0;  // tick of the scan
  std::uint64_t t_send_ns = 0;
  std::uint16_t n = 0;
  std::array<std::uint64_t, kBitsetWords> observed{};
  std::array<std::uint64_t, kBitsetWords> fire{};  // cell burns or has burnt
};

inline bool bit_get(const std::array<std::uint64_t, kBitsetWords>& b, int i) {
  return (b[std::size_t(i) >> 6] >> (i & 63)) & 1u;
}
inline void bit_set(std::array<std::uint64_t, kBitsetWords>& b, int i) {
  b[std::size_t(i) >> 6] |= std::uint64_t(1) << (i & 63);
}

struct DecisionMsg {
  std::uint32_t seq = 0;
  std::uint32_t tick = 0;
  std::uint64_t echo_t_send_ns = 0;  // t_send_ns of the frame that caused the decision
  Plan plan;
  std::uint32_t state = 0;           // HealthState
  std::uint64_t filter_ns = 0, rollout_ns = 0, policy_ns = 0, total_ns = 0;
};

struct HeartbeatMsg {
  std::uint32_t seq = 0;
  std::uint32_t tick = 0;
  std::uint64_t t_send_ns = 0;
  std::uint32_t state = 0;
};

struct ForecastMsg {
  std::uint32_t seq = 0;
  std::uint32_t tick = 0;
  std::uint16_t n = 0;
  float ess = 0.f;
  std::array<std::uint8_t, kMaxCells> p_burn_u8{};  // round(255 * P)
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t len, std::uint32_t crc = 0);

// Encoders return the frame size in bytes, or 0 on error.
std::size_t encode(const SensorMsg& m, std::uint8_t* buf, std::size_t cap);
std::size_t encode(const PerimeterMsg& m, std::uint8_t* buf, std::size_t cap);
std::size_t encode(const DecisionMsg& m, std::uint8_t* buf, std::size_t cap);
std::size_t encode(const HeartbeatMsg& m, std::uint8_t* buf, std::size_t cap);
std::size_t encode(const ForecastMsg& m, std::uint8_t* buf, std::size_t cap);

enum class DecodeStatus { Ok, NeedMore, BadMagic, BadVersion, BadLength, BadCrc, BadType };

// Parse and check one header. On Ok, `frame_len` is the full frame size.
// NeedMore: `len` is too short. Checks the CRC only when the full frame is present.
DecodeStatus check_frame(const std::uint8_t* buf, std::size_t len, FrameHeader& h,
                         std::size_t& frame_len);

bool decode(const FrameHeader& h, const std::uint8_t* payload, SensorMsg& m);
bool decode(const FrameHeader& h, const std::uint8_t* payload, PerimeterMsg& m);
bool decode(const FrameHeader& h, const std::uint8_t* payload, DecisionMsg& m);
bool decode(const FrameHeader& h, const std::uint8_t* payload, HeartbeatMsg& m);
bool decode(const FrameHeader& h, const std::uint8_t* payload, ForecastMsg& m);

}  // namespace embercast
