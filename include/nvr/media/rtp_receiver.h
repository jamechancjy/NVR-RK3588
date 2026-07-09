#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace nvr {

// UDP RTP receiver. Binds a port, parses RTP headers (V2, CSRC, extension) and
// delivers each packet's payload to a callback. Used by GB28181 to receive the
// MPEG-PS stream a device sends after an INVITE. A tiny reorder tolerance is not
// applied here; PS depacketiser is resilient to occasional loss via start-code
// resync. Payload type is not enforced (GB28181 uses PT 96 = PS).
class RtpReceiver {
 public:
  using PayloadCb = std::function<void(const uint8_t* data, size_t len,
                                       uint16_t seq, uint32_t ts, bool marker)>;

  RtpReceiver(int port, PayloadCb cb);  // port 0 => OS-assigned
  ~RtpReceiver();

  bool Start();
  void Stop();
  bool running() const { return running_.load(); }
  int port() const { return bound_port_; }

  uint64_t packets() const { return packets_.load(); }

 private:
  void RunLoop();

  int want_port_;
  int bound_port_ = -1;
  int fd_ = -1;
  PayloadCb cb_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> packets_{0};
  std::thread thread_;
};

}  // namespace nvr
