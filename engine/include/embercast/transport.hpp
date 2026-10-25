#pragma once
// Transports. A receiver thread reads a byte stream (TCP or Unix socket),
// cuts it into frames, checks the CRC, and pushes good frames into a
// fixed-size SPSC ring buffer. The engine thread pops frames from the ring.
// Corrupt frames are dropped and counted. The reader resynchronises on the
// magic word after an error.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "embercast/frame.hpp"

namespace embercast {

// Single-producer single-consumer ring. Capacity N - 1. No locks.
template <class T, std::size_t N>
class SpscRing {
 public:
  bool push(const T& v) {
    const std::size_t h = head_.load(std::memory_order_relaxed);
    const std::size_t n = (h + 1) % N;
    if (n == tail_.load(std::memory_order_acquire)) return false;
    buf_[h] = v;
    head_.store(n, std::memory_order_release);
    return true;
  }
  bool pop(T& v) {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;
    v = buf_[t];
    tail_.store((t + 1) % N, std::memory_order_release);
    return true;
  }
  bool empty() const { return tail_.load() == head_.load(); }

 private:
  std::array<T, N> buf_{};
  std::atomic<std::size_t> head_{0}, tail_{0};
};

struct RawFrame {
  FrameHeader h;
  std::uint64_t t_recv_ns = 0;  // receiver monotonic clock
  std::uint32_t len = 0;        // payload length
  std::array<std::uint8_t, kMaxPayload> payload;
};

struct TransportCounters {
  std::atomic<std::uint64_t> frames{0}, bad_crc{0}, bad_header{0}, ring_full{0}, bytes{0},
      disconnects{0};
};

// Cuts a byte stream into checked frames. Fixed buffer, no allocation.
class FrameReader {
 public:
  // Add bytes. Calls on_frame(header, payload) for each good frame.
  template <class F>
  void feed(const std::uint8_t* data, std::size_t n, TransportCounters& c, F&& on_frame);
  void reset() { len_ = 0; }

 private:
  std::array<std::uint8_t, 2 * kMaxFrame> buf_;
  std::size_t len_ = 0;
};

class ITransport {
 public:
  virtual ~ITransport() = default;
  virtual bool send(const std::uint8_t* data, std::size_t n) = 0;
  virtual bool connected() const = 0;
  virtual bool pop(RawFrame& f) = 0;
  virtual TransportCounters& counters() = 0;
  virtual void close() = 0;
};

// Byte-stream transport over a connected socket fd (TCP or Unix).
class StreamTransport final : public ITransport {
 public:
  explicit StreamTransport(int fd);
  ~StreamTransport() override;
  bool send(const std::uint8_t* data, std::size_t n) override;
  bool connected() const override { return up_.load(); }
  bool pop(RawFrame& f) override;
  TransportCounters& counters() override { return ctr_; }
  void close() override;

 private:
  void rx_loop();
  int fd_;
  std::atomic<bool> up_{true};
  std::thread rx_;
  TransportCounters ctr_;
  std::unique_ptr<SpscRing<RawFrame, 64>> ring_;
  FrameReader reader_;
};

// In-process transport: a pair of rings, no sockets.
class InProcessTransport final : public ITransport {
 public:
  InProcessTransport(SpscRing<RawFrame, 64>* rx, SpscRing<RawFrame, 64>* tx) : rx_(rx), tx_(tx) {}
  bool send(const std::uint8_t* data, std::size_t n) override;
  bool connected() const override { return true; }
  bool pop(RawFrame& f) override { return rx_->pop(f); }
  TransportCounters& counters() override { return ctr_; }
  void close() override {}

 private:
  SpscRing<RawFrame, 64>* rx_;
  SpscRing<RawFrame, 64>* tx_;
  TransportCounters ctr_;
  FrameReader reader_;
};

// Socket helpers. They return -1 on failure.
int tcp_listen(const std::string& host, int port);
int tcp_connect(const std::string& host, int port, int timeout_ms);
int uds_listen(const std::string& path);
int uds_connect(const std::string& path);
// Accept with a timeout (ms). Returns -1 on timeout.
int accept_timeout(int listen_fd, int timeout_ms);
void close_fd(int fd);

// Sensor source and status sink interfaces (MCU via sidecar, or laptop).
class ISensorSource {
 public:
  virtual ~ISensorSource() = default;
  virtual bool poll(SensorMsg& m) = 0;
};

class IStatusSink {
 public:
  virtual ~IStatusSink() = default;
  virtual void status(HealthState s, std::uint8_t top_risk) = 0;
};

// ---- FrameReader implementation ----
template <class F>
void FrameReader::feed(const std::uint8_t* data, std::size_t n, TransportCounters& c,
                       F&& on_frame) {
  while (n > 0) {
    const std::size_t take = std::min(n, buf_.size() - len_);
    std::copy(data, data + take, buf_.data() + len_);
    len_ += take;
    data += take;
    n -= take;
    std::size_t pos = 0;
    while (len_ - pos >= kHeaderSize) {
      FrameHeader h;
      std::size_t flen = 0;
      const DecodeStatus st = check_frame(buf_.data() + pos, len_ - pos, h, flen);
      if (st == DecodeStatus::NeedMore) break;
      if (st == DecodeStatus::Ok) {
        on_frame(h, buf_.data() + pos + kHeaderSize);
        pos += flen;
        continue;
      }
      if (st == DecodeStatus::BadCrc) {
        ++c.bad_crc;
        pos += flen;  // the length field passed its checks; skip the frame
      } else {
        ++c.bad_header;
        ++pos;  // resynchronise byte by byte
      }
    }
    std::copy(buf_.data() + pos, buf_.data() + len_, buf_.data());
    len_ -= pos;
    if (len_ == buf_.size()) len_ = 0;  // cannot happen with valid lengths; stay safe
  }
}

}  // namespace embercast
