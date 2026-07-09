#include "nvr/media/stream_source.h"

#include "nvr/common/logging.h"

#ifdef NVR_HAVE_FFMPEG
#include "nvr/media/rtsp_source.h"
#endif

namespace nvr {

// TODO(media-input): remaining protocols:
//   ONVIF   -> WS-Discovery + media profile query, then RTSP
//   GB28181 -> SIP (register/invite) + PS stream depacketiser
std::unique_ptr<StreamSource> CreateStreamSource(const CameraConfig& cfg) {
  switch (cfg.protocol) {
    case CameraConfig::Protocol::kRtsp:
#ifdef NVR_HAVE_FFMPEG
      if (!cfg.main_url.empty())
        return std::make_unique<RtspSource>(cfg.channel, cfg.main_url);
      NVR_LOGW("ch%d rtsp: empty main_url", cfg.channel);
      return nullptr;
#else
      NVR_LOGW("ch%d rtsp: built without FFmpeg (NVR_HAVE_FFMPEG off)",
               cfg.channel);
      return nullptr;
#endif
    default:
      NVR_LOGW("CreateStreamSource: protocol %d not yet implemented (ch %d)",
               static_cast<int>(cfg.protocol), cfg.channel);
      return nullptr;
  }
}

}  // namespace nvr
