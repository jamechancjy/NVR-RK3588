#include "nvr/media/gb28181_source.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>

#include "nvr/common/logging.h"
#include "nvr/media/sip.h"

namespace nvr {

namespace {

std::string RandHex(int n) {
  static const char* h = "0123456789abcdef";
  std::random_device rd;
  std::string s;
  for (int i = 0; i < n; ++i) s.push_back(h[rd() & 0xF]);
  return s;
}

// Build a GB28181 SSRC: 0 (live) + 5-digit realm + 4-digit sequence. Here we use
// a simple decimal string; the device echoes it in the RTP SSRC / y= line.
std::string MakeSsrc() {
  std::random_device rd;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0%09u", rd() % 1000000000u);
  return buf;
}

// One-shot UDP request/response: send `msg` to ip:port, wait for a datagram.
bool UdpExchange(const std::string& ip, int port, const std::string& msg,
                 int timeout_ms, std::string* resp) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_in dst;
  std::memset(&dst, 0, sizeof(dst));
  dst.sin_family = AF_INET;
  dst.sin_port = htons(static_cast<uint16_t>(port));
  dst.sin_addr.s_addr = inet_addr(ip.c_str());

  if (sendto(fd, msg.data(), msg.size(), 0,
             reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst)) < 0) {
    close(fd);
    return false;
  }
  if (!resp) {
    close(fd);
    return true;
  }
  char buf[4096];
  ssize_t n = recv(fd, buf, sizeof(buf), 0);
  close(fd);
  if (n <= 0) return false;
  resp->assign(buf, static_cast<size_t>(n));
  return true;
}

}  // namespace

Gb28181Source::Gb28181Source(int channel, Gb28181Config cfg)
    : channel_(channel), cfg_(std::move(cfg)) {}

Gb28181Source::~Gb28181Source() { Stop(); }

bool Gb28181Source::Start() {
  if (rtp_ && rtp_->running()) return true;

  depack_ = std::make_unique<PsDepacketizer>(
      channel_, [this](const Frame& f) { EmitMain(f); });

  PsDepacketizer* dp = depack_.get();
  rtp_ = std::make_unique<RtpReceiver>(
      cfg_.rtp_port,
      [dp](const uint8_t* d, size_t n, uint16_t, uint32_t, bool) {
        dp->Feed(d, n);
      });
  if (!rtp_->Start()) {
    NVR_LOGE("ch%d gb28181: rtp receiver failed", channel_);
    return false;
  }

  if (cfg_.passive) {
    NVR_LOGI("ch%d gb28181 passive: receiving RTP/PS on udp/%d", channel_,
             rtp_->port());
    return true;
  }
  if (!SendInvite()) {
    NVR_LOGW("ch%d gb28181: INVITE failed", channel_);
    rtp_->Stop();
    return false;
  }
  return true;
}

void Gb28181Source::Stop() {
  if (dialog_up_) SendBye();
  if (rtp_) rtp_->Stop();
  dialog_up_ = false;
}

bool Gb28181Source::running() const { return rtp_ && rtp_->running(); }

bool Gb28181Source::SendInvite() {
  call_id_ = RandHex(16);
  local_tag_ = RandHex(8);
  branch_ = "z9hG4bK" + RandHex(12);
  std::string ssrc = MakeSsrc();
  int rtp_port = rtp_->port();

  // SDP: request PS over RTP, receive-only, with GB28181 y= (ssrc) line.
  std::ostringstream sdp;
  sdp << "v=0\r\n"
      << "o=" << cfg_.local_id << " 0 0 IN IP4 " << cfg_.local_ip << "\r\n"
      << "s=Play\r\n"
      << "c=IN IP4 " << cfg_.local_ip << "\r\n"
      << "t=0 0\r\n"
      << "m=video " << rtp_port << " RTP/AVP 96 98 97\r\n"
      << "a=recvonly\r\n"
      << "a=rtpmap:96 PS/90000\r\n"
      << "a=rtpmap:98 H264/90000\r\n"
      << "a=rtpmap:97 MPEG4/90000\r\n"
      << "y=" << ssrc << "\r\n";
  std::string sdp_body = sdp.str();

  SipMessage inv;
  inv.is_request = true;
  inv.method = "INVITE";
  inv.uri = "sip:" + cfg_.device_id + "@" + cfg_.device_ip + ":" +
            std::to_string(cfg_.device_sip_port);
  inv.Add("Via", "SIP/2.0/UDP " + cfg_.local_ip + ":" +
                     std::to_string(cfg_.local_sip_port) + ";branch=" + branch_);
  inv.Add("From", "<sip:" + cfg_.local_id + "@" + cfg_.local_ip + ">;tag=" +
                      local_tag_);
  inv.Add("To", "<sip:" + cfg_.device_id + "@" + cfg_.device_ip + ">");
  inv.Add("Call-ID", call_id_);
  inv.Add("CSeq", "1 INVITE");
  inv.Add("Contact", "<sip:" + cfg_.local_id + "@" + cfg_.local_ip + ":" +
                         std::to_string(cfg_.local_sip_port) + ">");
  inv.Add("Content-Type", "application/sdp");
  inv.Add("Max-Forwards", "70");
  inv.Add("Subject", cfg_.device_id + ":" + ssrc + "," + cfg_.local_id + ":0");
  inv.Add("User-Agent", "NVR-RK3588");
  inv.body = sdp_body;

  std::string resp;
  if (!UdpExchange(cfg_.device_ip, cfg_.device_sip_port, inv.Serialize(),
                   cfg_.sip_timeout_ms, &resp)) {
    NVR_LOGW("ch%d gb28181: no INVITE response", channel_);
    return false;
  }
  SipMessage r = SipMessage::Parse(resp);
  // NOTE: a 100 Trying may precede the 200 OK on the same socket; this one-shot
  // exchange accepts the first final response. A full transaction layer (retry,
  // reading past provisional responses, 401 digest) is future work.
  if (r.status_code < 200 || r.status_code >= 300) {
    NVR_LOGW("ch%d gb28181: INVITE -> %d %s", channel_, r.status_code,
             r.reason.c_str());
    return false;
  }
  remote_tag_ = SipParam(r.Get("To"), "tag");

  // ACK the 200 OK.
  SipMessage ack;
  ack.is_request = true;
  ack.method = "ACK";
  ack.uri = inv.uri;
  ack.Add("Via", "SIP/2.0/UDP " + cfg_.local_ip + ":" +
                     std::to_string(cfg_.local_sip_port) + ";branch=" +
                     branch_);
  ack.Add("From", inv.Get("From"));
  ack.Add("To", r.Get("To"));
  ack.Add("Call-ID", call_id_);
  ack.Add("CSeq", "1 ACK");
  ack.Add("Max-Forwards", "70");
  UdpExchange(cfg_.device_ip, cfg_.device_sip_port, ack.Serialize(), 1, nullptr);

  dialog_up_ = true;
  NVR_LOGI("ch%d gb28181: stream invited, RTP/PS on udp/%d", channel_,
           rtp_port);
  return true;
}

void Gb28181Source::SendBye() {
  SipMessage bye;
  bye.is_request = true;
  bye.method = "BYE";
  bye.uri = "sip:" + cfg_.device_id + "@" + cfg_.device_ip + ":" +
            std::to_string(cfg_.device_sip_port);
  bye.Add("Via", "SIP/2.0/UDP " + cfg_.local_ip + ":" +
                     std::to_string(cfg_.local_sip_port) + ";branch=z9hG4bK" +
                     RandHex(12));
  bye.Add("From", "<sip:" + cfg_.local_id + "@" + cfg_.local_ip + ">;tag=" +
                      local_tag_);
  bye.Add("To", "<sip:" + cfg_.device_id + "@" + cfg_.device_ip + ">;tag=" +
                    remote_tag_);
  bye.Add("Call-ID", call_id_);
  bye.Add("CSeq", "2 BYE");
  bye.Add("Max-Forwards", "70");
  UdpExchange(cfg_.device_ip, cfg_.device_sip_port, bye.Serialize(), 1, nullptr);
}

}  // namespace nvr
