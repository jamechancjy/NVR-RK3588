#pragma once

#include <memory>
#include <string>

#include "nvr/media/rtsp_source.h"
#include "nvr/media/stream_source.h"

namespace nvr {

// ONVIF camera source: resolves the RTSP stream URI from a device service URL
// via ONVIF (GetProfiles + GetStreamUri, WS-Security auth), then pulls it with
// an internal RtspSource. profile_index selects main(0)/sub(1) stream.
class OnvifSource : public StreamSource {
 public:
  OnvifSource(int channel, std::string device_service, std::string user,
              std::string pass, int profile_index = 0);
  ~OnvifSource() override;

  bool Start() override;
  void Stop() override;
  bool running() const override;

  const std::string& resolved_rtsp_uri() const { return rtsp_uri_; }

 private:
  int channel_;
  std::string device_service_;
  std::string user_;
  std::string pass_;
  int profile_index_;
  std::string rtsp_uri_;
  std::unique_ptr<RtspSource> rtsp_;
};

}  // namespace nvr
