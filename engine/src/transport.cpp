#include "embercast/transport.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "embercast/clock.hpp"

namespace embercast {

StreamTransport::StreamTransport(int fd) : fd_(fd), ring_(std::make_unique<SpscRing<RawFrame, 64>>()) {
  rx_ = std::thread([this] { rx_loop(); });
}

StreamTransport::~StreamTransport() {
  close();
  if (rx_.joinable()) rx_.join();
  close_fd(fd_);
}

void StreamTransport::close() {
  if (up_.exchange(false)) ++ctr_.disconnects;
  ::shutdown(fd_, SHUT_RDWR);
}

bool StreamTransport::send(const std::uint8_t* data, std::size_t n) {
  if (!up_.load()) return false;
  while (n > 0) {
    const ssize_t w = ::send(fd_, data, n, MSG_NOSIGNAL);
    if (w < 0) {
      if (errno == EINTR) continue;
      close();
      return false;
    }
    data += w;
    n -= std::size_t(w);
  }
  return true;
}

bool StreamTransport::pop(RawFrame& f) { return ring_->pop(f); }

void StreamTransport::rx_loop() {
  std::uint8_t buf[8192];
  // RawFrame is large; keep one per receiver thread (allocated once).
  auto tmp = std::make_unique<RawFrame>();
  while (up_.load()) {
    const ssize_t r = ::recv(fd_, buf, sizeof buf, 0);
    if (r == 0 || (r < 0 && errno != EINTR)) {
      close();
      break;
    }
    if (r < 0) continue;
    ctr_.bytes += std::uint64_t(r);
    const std::uint64_t now = now_ns();
    reader_.feed(buf, std::size_t(r), ctr_, [&](const FrameHeader& h, const std::uint8_t* p) {
      tmp->h = h;
      tmp->t_recv_ns = now;
      tmp->len = h.payload_len;
      std::memcpy(tmp->payload.data(), p, h.payload_len);
      ++ctr_.frames;
      if (!ring_->push(*tmp)) ++ctr_.ring_full;
    });
  }
}

bool InProcessTransport::send(const std::uint8_t* data, std::size_t n) {
  auto tmp = std::make_unique<RawFrame>();
  bool ok = true;
  reader_.feed(data, n, ctr_, [&](const FrameHeader& h, const std::uint8_t* p) {
    tmp->h = h;
    tmp->t_recv_ns = now_ns();
    tmp->len = h.payload_len;
    std::memcpy(tmp->payload.data(), p, h.payload_len);
    ++ctr_.frames;
    if (!tx_->push(*tmp)) {
      ++ctr_.ring_full;
      ok = false;
    }
  });
  return ok;
}

int tcp_listen(const std::string& host, int port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(std::uint16_t(port));
  if (::inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1 ||
      ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 4) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

int tcp_connect(const std::string& host, int port, int timeout_ms) {
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return -1;
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    ::freeaddrinfo(res);
    return -1;
  }
  const int flags = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int rc = ::connect(fd, res->ai_addr, res->ai_addrlen);
  ::freeaddrinfo(res);
  if (rc != 0 && errno == EINPROGRESS) {
    pollfd p{fd, POLLOUT, 0};
    rc = ::poll(&p, 1, timeout_ms) == 1 ? 0 : -1;
    int err = 0;
    socklen_t len = sizeof err;
    if (rc == 0 && (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0)) rc = -1;
  }
  if (rc != 0) {
    ::close(fd);
    return -1;
  }
  ::fcntl(fd, F_SETFL, flags);
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  return fd;
}

int uds_listen(const std::string& path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  if (path.size() >= sizeof a.sun_path) {
    ::close(fd);
    return -1;
  }
  std::strcpy(a.sun_path, path.c_str());
  ::unlink(path.c_str());
  if (::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 2) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

int uds_connect(const std::string& path) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  if (path.size() >= sizeof a.sun_path) {
    ::close(fd);
    return -1;
  }
  std::strcpy(a.sun_path, path.c_str());
  if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

int accept_timeout(int listen_fd, int timeout_ms) {
  pollfd p{listen_fd, POLLIN, 0};
  if (::poll(&p, 1, timeout_ms) != 1) return -1;
  const int fd = ::accept(listen_fd, nullptr, nullptr);
  if (fd >= 0) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);  // fails harmlessly on UDS
  }
  return fd;
}

void close_fd(int fd) {
  if (fd >= 0) ::close(fd);
}

}  // namespace embercast
