#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "nvr/media/stream_source.h"

namespace nvr {

// RTSP pull client built on FFmpeg libavformat. It pulls one compressed stream
// (main OR sub) and emits H.264/H.265 video and AAC audio frames WITHOUT
// decoding, matching the "先来先存 / 不解码" storage model. Frames are delivered
// on the main callback (create one source per URL). Automatically reconnects on
// error while running.
class RtspSource : public StreamSource {
 public:
  struct Options {
    bool tcp = true;          // rtsp_transport tcp (more reliable than udp)
    int open_timeout_ms = 5000;
    int read_timeout_ms = 5000;
    int reconnect_delay_ms = 2000;
  };

  RtspSource(int channel, std::string url);
  RtspSource(int channel, std::string url, Options opts);
  ~RtspSource() override;

  bool Start() override;
  void Stop() override;
  bool running() const override { return running_.load(); }

  int channel() const { return channel_; }
  const std::string& url() const { return url_; }

 private:
  void RunLoop();
  bool OpenInput();
  void CloseInput();

  int channel_;
  std::string url_;
  Options opts_;

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread thread_;

  struct Impl;               // holds FFmpeg AV* handles (pImpl, keeps AV out of hdr)
  std::unique_ptr<Impl> impl_;
};

}  // namespace nvr
