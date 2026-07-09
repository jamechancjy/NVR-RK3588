#include "nvr/media/rtp_receiver.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>

#include "nvr/common/logging.h"

namespace nvr {

RtpReceiver::RtpReceiver(int port, PayloadCb cb)
    : want_port_(port), cb_(std::move(cb)) {}

RtpReceiver::~RtpReceiver() { Stop(); }

bool RtpReceiver::Start() {
  if (running_.load()) return true;
  fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ < 0) {
    NVR_LOGW("rtp: socket failed");
    return false;
  }
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  int rcvbuf = 4 * 1024 * 1024;
  setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(want_port_));
  if (bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
    NVR_LOGW("rtp: bind port %d failed", want_port_);
    close(fd_);
    fd_ = -1;
    return false;
  }
  socklen_t alen = sizeof(addr);
  if (getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr), &alen) == 0)
    bound_port_ = ntohs(addr.sin_port);

  // 1s recv timeout so Stop() is responsive.
  struct timeval tv{1, 0};
  setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  stop_.store(false);
  running_.store(true);
  thread_ = std::thread([this] { RunLoop(); });
  NVR_LOGI("rtp receiver listening on udp/%d", bound_port_);
  return true;
}

void RtpReceiver::Stop() {
  stop_.store(true);
  if (thread_.joinable()) thread_.join();
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  running_.store(false);
}

void RtpReceiver::RunLoop() {
  uint8_t buf[65536];
  while (!stop_.load()) {
    ssize_t n = recv(fd_, buf, sizeof(buf), 0);
    if (n <= 0) continue;  // timeout or error
    if (n < 12) continue;  // too small for RTP

    uint8_t v = (buf[0] >> 6) & 0x03;
    if (v != 2) continue;
    bool padding = (buf[0] & 0x20) != 0;
    bool ext = (buf[0] & 0x10) != 0;
    uint8_t cc = buf[0] & 0x0F;
    bool marker = (buf[1] & 0x80) != 0;
    uint16_t seq = (buf[2] << 8) | buf[3];
    uint32_t ts = (static_cast<uint32_t>(buf[4]) << 24) |
                  (static_cast<uint32_t>(buf[5]) << 16) |
                  (static_cast<uint32_t>(buf[6]) << 8) | buf[7];

    size_t off = 12 + cc * 4;
    if (ext) {
      if (off + 4 > static_cast<size_t>(n)) continue;
      uint16_t extlen = (buf[off + 2] << 8) | buf[off + 3];
      off += 4 + extlen * 4;
    }
    if (off > static_cast<size_t>(n)) continue;

    size_t plen = static_cast<size_t>(n) - off;
    if (padding && plen > 0) {
      uint8_t pad = buf[n - 1];
      if (pad <= plen) plen -= pad;
    }
    if (plen == 0) continue;

    packets_.fetch_add(1);
    if (cb_) cb_(buf + off, plen, seq, ts, marker);
  }
}

}  // namespace nvr
