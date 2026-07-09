#pragma once

#include <memory>
#include <string>

#include "nvr/media/ps_depacketizer.h"
#include "nvr/media/rtp_receiver.h"
#include "nvr/media/stream_source.h"

namespace nvr {

// GB/T 28181 live source. Two modes:
//   * SIP mode: NVR acts as SIP client, sends an INVITE to the device to start a
//     real-time stream; device sends MPEG-PS over RTP to our rtp_port. On Stop()
//     a BYE tears the dialog down.
//   * passive mode: skip SIP; just receive RTP/PS on rtp_port (useful for lab
//     testing and when signalling is handled elsewhere).
// Received RTP payloads are depacketised (PS -> H.264/H.265 access units) and
// emitted through the StreamSource main callback.
struct Gb28181Config {
  // Local (NVR) SIP identity, reachable by the device.
  std::string local_id;   // 20-digit SIP ID of the NVR/platform
  std::string local_ip;
  int local_sip_port = 5060;
  int rtp_port = 0;       // local UDP port to receive RTP/PS (0 => OS-assigned)

  // Remote device/channel to invite.
  std::string device_id;  // 20-digit device or channel ID
  std::string device_ip;
  int device_sip_port = 5060;

  bool passive = false;   // skip SIP signalling, only receive RTP
  int sip_timeout_ms = 5000;
};

class Gb28181Source : public StreamSource {
 public:
  Gb28181Source(int channel, Gb28181Config cfg);
  ~Gb28181Source() override;

  bool Start() override;
  void Stop() override;
  bool running() const override;

  int rtp_port() const { return rtp_ ? rtp_->port() : -1; }

 private:
  bool SendInvite();
  void SendBye();

  int channel_;
  Gb28181Config cfg_;
  std::unique_ptr<PsDepacketizer> depack_;
  std::unique_ptr<RtpReceiver> rtp_;

  // SIP dialog state (SIP mode only).
  std::string call_id_;
  std::string local_tag_;
  std::string remote_tag_;
  std::string branch_;
  bool dialog_up_ = false;
};

}  // namespace nvr
