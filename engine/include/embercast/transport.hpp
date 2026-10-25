#pragma once
// ITransport (InProcess, Tcp, Uds), ISensorSource, IStatusSink.
// Receiver threads push frames into a fixed-size SPSC ring buffer.
// The engine thread pops from the ring buffer. Step 7 and Step 9 fill this file.

namespace embercast {

class ITransport {
 public:
  virtual ~ITransport() = default;
};

class ISensorSource {
 public:
  virtual ~ISensorSource() = default;
};

class IStatusSink {
 public:
  virtual ~IStatusSink() = default;
};

}  // namespace embercast
