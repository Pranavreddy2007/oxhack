#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "doctest.h"
#include "embercast/clock.hpp"
#include "embercast/transport.hpp"

using namespace embercast;

namespace {

std::vector<std::uint8_t> sensor_frame(std::uint32_t seq) {
  SensorMsg m;
  m.seq = seq;
  m.tick = seq;
  m.wind_speed = float(seq);
  std::vector<std::uint8_t> b(kMaxFrame);
  b.resize(encode(m, b.data(), b.size()));
  return b;
}

bool wait_pop(ITransport& t, RawFrame& f, int ms = 1000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    if (t.pop(f)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

}  // namespace

TEST_CASE("frame reader: split frames, garbage, and corrupt CRC") {
  FrameReader r;
  TransportCounters c;
  std::vector<std::uint32_t> got;
  auto on = [&](const FrameHeader& h, const std::uint8_t*) { got.push_back(h.seq); };
  std::vector<std::uint8_t> stream;
  auto f1 = sensor_frame(1), f2 = sensor_frame(2), f3 = sensor_frame(3);
  f2[kHeaderSize + 2] ^= 0xFF;  // corrupt payload -> CRC fail
  const std::uint8_t junk[] = {0x00, 0x42, 0x4D, 0x43, 0x45, 0x11};
  stream.insert(stream.end(), junk, junk + sizeof junk);
  stream.insert(stream.end(), f1.begin(), f1.end());
  stream.insert(stream.end(), f2.begin(), f2.end());
  stream.insert(stream.end(), f3.begin(), f3.end());
  // Feed in odd-sized pieces.
  for (std::size_t i = 0; i < stream.size(); i += 7)
    r.feed(stream.data() + i, std::min<std::size_t>(7, stream.size() - i), c, on);
  CHECK(got == std::vector<std::uint32_t>{1, 3});
  CHECK(c.bad_crc == 1);
  CHECK(c.bad_header >= 1);
}

TEST_CASE("spsc ring keeps order and reports full") {
  SpscRing<int, 4> q;
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK(q.push(3));
  CHECK(!q.push(4));
  int v;
  CHECK(q.pop(v));
  CHECK(v == 1);
  CHECK(q.push(4));
  CHECK(q.pop(v));
  CHECK(v == 2);
}

TEST_CASE("stream transport: frames arrive, disconnect is detected") {
  int sv[2];
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  auto a = std::make_unique<StreamTransport>(sv[0]);
  auto b = std::make_unique<StreamTransport>(sv[1]);
  auto f = sensor_frame(9);
  CHECK(a->send(f.data(), f.size()));
  auto rf = std::make_unique<RawFrame>();
  REQUIRE(wait_pop(*b, *rf));
  CHECK(rf->h.seq == 9);
  SensorMsg m;
  CHECK(decode(rf->h, rf->payload.data(), m));
  CHECK(m.wind_speed == 9.f);
  a.reset();  // peer closes
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (b->connected() && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  CHECK(!b->connected());
  CHECK(!b->send(f.data(), f.size()));
}

TEST_CASE("tcp: connect fails fast when nothing listens; reconnect works after listen") {
  const int lfd = tcp_listen("127.0.0.1", 0);
  REQUIRE(lfd >= 0);
  sockaddr_storage ss{};
  socklen_t len = sizeof ss;
  ::getsockname(lfd, reinterpret_cast<sockaddr*>(&ss), &len);
  const int port = ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
  close_fd(lfd);
  CHECK(tcp_connect("127.0.0.1", port, 100) < 0);  // nothing listens now
  const int lfd2 = tcp_listen("127.0.0.1", port);
  REQUIRE(lfd2 >= 0);
  const int cfd = tcp_connect("127.0.0.1", port, 500);
  REQUIRE(cfd >= 0);
  const int sfd = accept_timeout(lfd2, 500);
  REQUIRE(sfd >= 0);
  StreamTransport client(cfd), server(sfd);
  auto f = sensor_frame(5);
  CHECK(client.send(f.data(), f.size()));
  auto rf = std::make_unique<RawFrame>();
  CHECK(wait_pop(server, *rf));
  CHECK(rf->h.seq == 5);
  close_fd(lfd2);
}
