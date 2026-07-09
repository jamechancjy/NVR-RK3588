#pragma once

#include <functional>
#include <memory>
#include <string>

#include "nvr/common/types.h"
#include "nvr/storage/frame.h"

namespace nvr {

// Camera connection parameters.
struct CameraConfig {
  int channel = 0;
  std::string name;
  std::string main_url;  // RTSP URL / GB28181 device id for the main stream
  std::string sub_url;   // sub stream
  enum class Protocol { kRtsp, kOnvif, kGb28181 } protocol = Protocol::kRtsp;
};

// Abstract camera stream input. Concrete implementations:
//   * RtspSource     (live555 / ffmpeg)
//   * OnvifSource    (ONVIF discovery + media profiles, then RTSP)
//   * Gb28181Source  (SIP signalling + PS-stream depacketise)
// Frames are delivered compressed (no decode) to the registered callback.
class StreamSource {
 public:
  virtual ~StreamSource() = default;

  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual bool running() const = 0;

  void set_callback(StreamKind kind, FrameCallback cb) {
    if (kind == StreamKind::kMain)
      main_cb_ = std::move(cb);
    else
      sub_cb_ = std::move(cb);
  }

 protected:
  void EmitMain(const Frame& f) { if (main_cb_) main_cb_(f); }
  void EmitSub(const Frame& f) { if (sub_cb_) sub_cb_(f); }

  FrameCallback main_cb_;
  FrameCallback sub_cb_;
};

// Factory for the configured protocol. Concrete sources are registered per
// platform; unimplemented protocols return nullptr for now.
std::unique_ptr<StreamSource> CreateStreamSource(const CameraConfig& cfg);

}  // namespace nvr
