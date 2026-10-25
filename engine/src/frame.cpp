#include "embercast/frame.hpp"

namespace embercast {

namespace {

std::array<std::uint32_t, 256> make_crc_table() {
  std::array<std::uint32_t, 256> t{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t c = i;
    for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    t[i] = c;
  }
  return t;
}
const std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

struct Writer {
  std::uint8_t* p;
  std::size_t cap, pos = 0;
  bool ok = true;
  void bytes(const void* src, std::size_t n) {
    if (pos + n > cap) {
      ok = false;
      return;
    }
    std::memcpy(p + pos, src, n);
    pos += n;
  }
  template <class T>
  void le(T v) {  // little-endian integer
    std::uint8_t b[sizeof(T)];
    for (std::size_t i = 0; i < sizeof(T); ++i) b[i] = std::uint8_t(std::uint64_t(v) >> (8 * i));
    bytes(b, sizeof(T));
  }
  void f32(float v) {
    std::uint32_t u;
    std::memcpy(&u, &v, 4);
    le(u);
  }
};

struct Reader {
  const std::uint8_t* p;
  std::size_t len, pos = 0;
  bool ok = true;
  template <class T>
  T le() {
    if (pos + sizeof(T) > len) {
      ok = false;
      return T{};
    }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) v |= std::uint64_t(p[pos + i]) << (8 * i);
    pos += sizeof(T);
    return T(v);
  }
  float f32() {
    const std::uint32_t u = le<std::uint32_t>();
    float v;
    std::memcpy(&v, &u, 4);
    return v;
  }
  void bytes(void* dst, std::size_t n) {
    if (pos + n > len) {
      ok = false;
      return;
    }
    std::memcpy(dst, p + pos, n);
    pos += n;
  }
};

// Write the header around a payload that is already at buf + kHeaderSize.
std::size_t finish(FrameType type, std::uint32_t seq, std::uint32_t tick, std::uint64_t t_send,
                   std::size_t payload_len, std::uint8_t* buf) {
  Writer w{buf, kHeaderSize};
  w.le(kFrameMagic);
  w.le(kFrameVersion);
  w.le(std::uint16_t(type));
  w.le(seq);
  w.le(tick);
  w.le(t_send);
  w.le(std::uint32_t(payload_len));
  w.le(std::uint32_t(0));
  const std::uint32_t c = crc32(buf, kHeaderSize + payload_len);
  Writer w2{buf + 28, 4};
  w2.le(c);
  return kHeaderSize + payload_len;
}

std::size_t bitset_words(int n) { return (std::size_t(n) * std::size_t(n) + 63) / 64; }

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t len, std::uint32_t crc) {
  crc = ~crc;
  for (std::size_t i = 0; i < len; ++i) crc = kCrcTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}

std::size_t encode(const SensorMsg& m, std::uint8_t* buf, std::size_t cap) {
  if (cap < kHeaderSize + 20) return 0;
  Writer w{buf + kHeaderSize, cap - kHeaderSize};
  w.f32(m.wind_speed);
  w.f32(m.wind_dir);
  w.f32(m.humidity);
  w.f32(m.temp);
  w.le(m.flags);
  return w.ok ? finish(FrameType::Sensor, m.seq, m.tick, m.t_send_ns, w.pos, buf) : 0;
}

std::size_t encode(const PerimeterMsg& m, std::uint8_t* buf, std::size_t cap) {
  if (m.n == 0 || m.n > kMaxN || cap < kHeaderSize) return 0;
  Writer w{buf + kHeaderSize, cap - kHeaderSize};
  const std::size_t words = bitset_words(m.n);
  w.le(m.n);
  w.le(std::uint16_t(0));
  for (std::size_t i = 0; i < words; ++i) w.le(m.observed[i]);
  for (std::size_t i = 0; i < words; ++i) w.le(m.fire[i]);
  return w.ok ? finish(FrameType::Perimeter, m.seq, m.tick, m.t_send_ns, w.pos, buf) : 0;
}

std::size_t encode(const DecisionMsg& m, std::uint8_t* buf, std::size_t cap) {
  if (cap < kHeaderSize) return 0;
  Writer w{buf + kHeaderSize, cap - kHeaderSize};
  w.le(m.state);
  w.f32(m.plan.expected_loss);
  w.f32(m.plan.cvar);
  w.le(std::uint32_t(m.plan.moved));
  w.le(std::uint8_t(m.plan.num_crews));
  w.bytes(m.plan.crew_cluster.data(), kMaxCrews);
  w.bytes(m.plan.top_risk.data(), 3);
  w.le(m.echo_t_send_ns);
  w.le(m.filter_ns);
  w.le(m.rollout_ns);
  w.le(m.policy_ns);
  w.le(m.total_ns);
  return w.ok ? finish(FrameType::Decision, m.seq, m.tick, 0, w.pos, buf) : 0;
}

std::size_t encode(const HeartbeatMsg& m, std::uint8_t* buf, std::size_t cap) {
  if (cap < kHeaderSize) return 0;
  Writer w{buf + kHeaderSize, cap - kHeaderSize};
  w.le(m.state);
  return w.ok ? finish(FrameType::Heartbeat, m.seq, m.tick, m.t_send_ns, w.pos, buf) : 0;
}

std::size_t encode(const ForecastMsg& m, std::uint8_t* buf, std::size_t cap) {
  if (m.n == 0 || m.n > kMaxN || cap < kHeaderSize) return 0;
  Writer w{buf + kHeaderSize, cap - kHeaderSize};
  w.le(m.n);
  w.le(std::uint16_t(0));
  w.f32(m.ess);
  w.bytes(m.p_burn_u8.data(), std::size_t(m.n) * m.n);
  return w.ok ? finish(FrameType::Forecast, m.seq, m.tick, 0, w.pos, buf) : 0;
}

DecodeStatus check_frame(const std::uint8_t* buf, std::size_t len, FrameHeader& h,
                         std::size_t& frame_len) {
  if (len < kHeaderSize) return DecodeStatus::NeedMore;
  Reader r{buf, kHeaderSize};
  h.magic = r.le<std::uint32_t>();
  h.version = r.le<std::uint16_t>();
  h.type = r.le<std::uint16_t>();
  h.seq = r.le<std::uint32_t>();
  h.t_sim_tick = r.le<std::uint32_t>();
  h.t_send_ns = r.le<std::uint64_t>();
  h.payload_len = r.le<std::uint32_t>();
  h.crc32 = r.le<std::uint32_t>();
  if (h.magic != kFrameMagic) return DecodeStatus::BadMagic;
  if (h.version != kFrameVersion) return DecodeStatus::BadVersion;
  if (h.payload_len > kMaxPayload) return DecodeStatus::BadLength;
  if (h.type < 1 || h.type > 5) return DecodeStatus::BadType;
  frame_len = kHeaderSize + h.payload_len;
  if (len < frame_len) return DecodeStatus::NeedMore;
  std::uint8_t hdr[kHeaderSize];
  std::memcpy(hdr, buf, kHeaderSize);
  std::memset(hdr + 28, 0, 4);
  std::uint32_t c = crc32(hdr, kHeaderSize);
  // Continue the CRC over the payload.
  c = crc32(buf + kHeaderSize, h.payload_len, c);
  return c == h.crc32 ? DecodeStatus::Ok : DecodeStatus::BadCrc;
}

bool decode(const FrameHeader& h, const std::uint8_t* payload, SensorMsg& m) {
  if (h.type != std::uint16_t(FrameType::Sensor)) return false;
  Reader r{payload, h.payload_len};
  m.seq = h.seq;
  m.tick = h.t_sim_tick;
  m.t_send_ns = h.t_send_ns;
  m.wind_speed = r.f32();
  m.wind_dir = r.f32();
  m.humidity = r.f32();
  m.temp = r.f32();
  m.flags = r.le<std::uint32_t>();
  return r.ok;
}

bool decode(const FrameHeader& h, const std::uint8_t* payload, PerimeterMsg& m) {
  if (h.type != std::uint16_t(FrameType::Perimeter)) return false;
  Reader r{payload, h.payload_len};
  m.seq = h.seq;
  m.tick = h.t_sim_tick;
  m.t_send_ns = h.t_send_ns;
  m.n = r.le<std::uint16_t>();
  r.le<std::uint16_t>();
  if (!r.ok || m.n == 0 || m.n > kMaxN) return false;
  const std::size_t words = bitset_words(m.n);
  m.observed.fill(0);
  m.fire.fill(0);
  for (std::size_t i = 0; i < words; ++i) m.observed[i] = r.le<std::uint64_t>();
  for (std::size_t i = 0; i < words; ++i) m.fire[i] = r.le<std::uint64_t>();
  return r.ok;
}

bool decode(const FrameHeader& h, const std::uint8_t* payload, DecisionMsg& m) {
  if (h.type != std::uint16_t(FrameType::Decision)) return false;
  Reader r{payload, h.payload_len};
  m.seq = h.seq;
  m.tick = h.t_sim_tick;
  m.plan.tick = h.t_sim_tick;
  m.state = r.le<std::uint32_t>();
  m.plan.expected_loss = r.f32();
  m.plan.cvar = r.f32();
  m.plan.moved = int(r.le<std::uint32_t>());
  m.plan.num_crews = r.le<std::uint8_t>();
  r.bytes(m.plan.crew_cluster.data(), kMaxCrews);
  r.bytes(m.plan.top_risk.data(), 3);
  m.echo_t_send_ns = r.le<std::uint64_t>();
  m.filter_ns = r.le<std::uint64_t>();
  m.rollout_ns = r.le<std::uint64_t>();
  m.policy_ns = r.le<std::uint64_t>();
  m.total_ns = r.le<std::uint64_t>();
  return r.ok && m.plan.num_crews <= kMaxCrews;
}

bool decode(const FrameHeader& h, const std::uint8_t* payload, HeartbeatMsg& m) {
  if (h.type != std::uint16_t(FrameType::Heartbeat)) return false;
  Reader r{payload, h.payload_len};
  m.seq = h.seq;
  m.tick = h.t_sim_tick;
  m.t_send_ns = h.t_send_ns;
  m.state = r.le<std::uint32_t>();
  return r.ok;
}

bool decode(const FrameHeader& h, const std::uint8_t* payload, ForecastMsg& m) {
  if (h.type != std::uint16_t(FrameType::Forecast)) return false;
  Reader r{payload, h.payload_len};
  m.seq = h.seq;
  m.tick = h.t_sim_tick;
  m.n = r.le<std::uint16_t>();
  r.le<std::uint16_t>();
  m.ess = r.f32();
  if (!r.ok || m.n == 0 || m.n > kMaxN) return false;
  r.bytes(m.p_burn_u8.data(), std::size_t(m.n) * m.n);
  return r.ok;
}

}  // namespace embercast
