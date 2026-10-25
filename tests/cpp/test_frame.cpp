#include <vector>

#include "doctest.h"
#include "embercast/frame.hpp"

using namespace embercast;

TEST_CASE("crc32 matches the IEEE check value") {
  const char* s = "123456789";
  CHECK(crc32(reinterpret_cast<const std::uint8_t*>(s), 9) == 0xCBF43926u);
}

TEST_CASE("sensor frame round trip") {
  SensorMsg m;
  m.seq = 7;
  m.tick = 33;
  m.t_send_ns = 123456789012345ull;
  m.wind_speed = 4.5f;
  m.wind_dir = -1.25f;
  m.humidity = 0.31f;
  m.temp = 22.f;
  m.flags = kFlagStuck;
  std::vector<std::uint8_t> buf(kMaxFrame);
  const auto len = encode(m, buf.data(), buf.size());
  REQUIRE(len == kHeaderSize + 20);
  FrameHeader h;
  std::size_t flen = 0;
  REQUIRE(check_frame(buf.data(), len, h, flen) == DecodeStatus::Ok);
  CHECK(flen == len);
  SensorMsg d;
  REQUIRE(decode(h, buf.data() + kHeaderSize, d));
  CHECK(d.seq == 7);
  CHECK(d.tick == 33);
  CHECK(d.t_send_ns == m.t_send_ns);
  CHECK(d.wind_speed == m.wind_speed);
  CHECK(d.wind_dir == m.wind_dir);
  CHECK(d.flags == kFlagStuck);
}

TEST_CASE("perimeter, decision, heartbeat and forecast round trip") {
  std::vector<std::uint8_t> buf(kMaxFrame);
  FrameHeader h;
  std::size_t flen = 0;

  PerimeterMsg p;
  p.n = 96;
  p.tick = 8;
  bit_set(p.observed, 5);
  bit_set(p.observed, 9215);
  bit_set(p.fire, 9215);
  auto len = encode(p, buf.data(), buf.size());
  REQUIRE(len > 0);
  REQUIRE(check_frame(buf.data(), len, h, flen) == DecodeStatus::Ok);
  PerimeterMsg pd;
  REQUIRE(decode(h, buf.data() + kHeaderSize, pd));
  CHECK(pd.observed == p.observed);
  CHECK(pd.fire == p.fire);

  DecisionMsg dm;
  dm.tick = 10;
  dm.plan.num_crews = 4;
  dm.plan.crew_cluster = {1, 2, 3, kNoCluster, 0, 0, 0, 0};
  dm.plan.expected_loss = 12.5f;
  dm.plan.cvar = 40.f;
  dm.echo_t_send_ns = 99;
  dm.rollout_ns = 1234;
  len = encode(dm, buf.data(), buf.size());
  REQUIRE(check_frame(buf.data(), len, h, flen) == DecodeStatus::Ok);
  DecisionMsg dd;
  REQUIRE(decode(h, buf.data() + kHeaderSize, dd));
  CHECK(dd.plan.crew_cluster == dm.plan.crew_cluster);
  CHECK(dd.plan.cvar == 40.f);
  CHECK(dd.echo_t_send_ns == 99);
  CHECK(dd.rollout_ns == 1234);

  HeartbeatMsg hb;
  hb.state = 1;
  len = encode(hb, buf.data(), buf.size());
  REQUIRE(check_frame(buf.data(), len, h, flen) == DecodeStatus::Ok);
  HeartbeatMsg hd;
  REQUIRE(decode(h, buf.data() + kHeaderSize, hd));
  CHECK(hd.state == 1);

  ForecastMsg fm;
  fm.n = 16;
  fm.p_burn_u8[3] = 200;
  len = encode(fm, buf.data(), buf.size());
  REQUIRE(check_frame(buf.data(), len, h, flen) == DecodeStatus::Ok);
  ForecastMsg fd;
  REQUIRE(decode(h, buf.data() + kHeaderSize, fd));
  CHECK(fd.p_burn_u8[3] == 200);
}

TEST_CASE("corrupt, short, and wrong frames are rejected") {
  SensorMsg m;
  std::vector<std::uint8_t> buf(kMaxFrame);
  const auto len = encode(m, buf.data(), buf.size());
  FrameHeader h;
  std::size_t flen = 0;
  CHECK(check_frame(buf.data(), 10, h, flen) == DecodeStatus::NeedMore);
  CHECK(check_frame(buf.data(), len - 1, h, flen) == DecodeStatus::NeedMore);
  auto bad = buf;
  bad[kHeaderSize + 3] ^= 0x40;  // flip a payload bit
  CHECK(check_frame(bad.data(), len, h, flen) == DecodeStatus::BadCrc);
  bad = buf;
  bad[0] ^= 1;
  CHECK(check_frame(bad.data(), len, h, flen) == DecodeStatus::BadMagic);
  bad = buf;
  bad[24] = 0xFF;  // payload_len too large
  bad[25] = 0xFF;
  CHECK(check_frame(bad.data(), len, h, flen) == DecodeStatus::BadLength);
}
