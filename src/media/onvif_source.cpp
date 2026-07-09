#include "nvr/media/onvif_source.h"

#include "nvr/common/logging.h"
#include "nvr/media/onvif.h"

namespace nvr {

OnvifSource::OnvifSource(int channel, std::string device_service,
                         std::string user, std::string pass, int profile_index)
    : channel_(channel),
      device_service_(std::move(device_service)),
      user_(std::move(user)),
      pass_(std::move(pass)),
      profile_index_(profile_index) {}

OnvifSource::~OnvifSource() { Stop(); }

bool OnvifSource::Start() {
  if (rtsp_ && rtsp_->running()) return true;

  OnvifClient client(device_service_, user_, pass_);
  std::string err;
  if (!client.GetStreamUri(profile_index_, &rtsp_uri_, &err)) {
    NVR_LOGW("ch%d onvif resolve failed: %s (%s)", channel_, err.c_str(),
             device_service_.c_str());
    return false;
  }
  NVR_LOGI("ch%d onvif resolved rtsp uri: %s", channel_, rtsp_uri_.c_str());

  rtsp_ = std::make_unique<RtspSource>(channel_, rtsp_uri_);
  // Forward the inner RtspSource frames out through this source's callbacks.
  rtsp_->set_callback(StreamKind::kMain,
                      [this](const Frame& f) { EmitMain(f); });
  return rtsp_->Start();
}

void OnvifSource::Stop() {
  if (rtsp_) rtsp_->Stop();
}

bool OnvifSource::running() const { return rtsp_ && rtsp_->running(); }

}  // namespace nvr
