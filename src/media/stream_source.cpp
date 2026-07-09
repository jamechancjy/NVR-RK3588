#include "nvr/media/stream_source.h"

#include "nvr/common/logging.h"

namespace nvr {

// TODO(media-input): register concrete sources per protocol/platform:
//   RTSP    -> live555 or ffmpeg pull client
//   ONVIF   -> WS-Discovery + media profile query, then RTSP
//   GB28181 -> SIP (register/invite) + PS stream depacketiser
// Until those are implemented the factory returns nullptr so callers can fall
// back to a synthetic/test source.
std::unique_ptr<StreamSource> CreateStreamSource(const CameraConfig& cfg) {
  NVR_LOGW("CreateStreamSource: protocol %d not yet implemented (ch %d)",
           static_cast<int>(cfg.protocol), cfg.channel);
  return nullptr;
}

}  // namespace nvr
