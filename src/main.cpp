// nvrd - RK3588 NVR daemon (skeleton).
//
// This entry point currently demonstrates the storage pipeline end to end using
// a synthetic frame source, so the design can be validated on a host build. On
// the target it will wire real StreamSources (RTSP/ONVIF/GB28181) -> MediaHub ->
// Recorder -> StorageEngine, plus preview (MPP decode + WebRTC) and the LVGL UI.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "nvr/common/logging.h"
#include "nvr/media/media_hub.h"
#include "nvr/media/onvif.h"
#include "nvr/media/stream_source.h"
#include "nvr/record/recorder.h"
#include "nvr/storage/storage_engine.h"

using namespace nvr;

namespace {
std::atomic<bool> g_stop{false};
void OnSignal(int) { g_stop.store(true); }
}  // namespace

namespace {

Frame MakeFrame(int channel, bool key, uint64_t ts_ms, int payload) {
  Frame f;
  f.channel = channel;
  f.media_type = MediaType::kVideo;
  f.frame_type = key ? FrameType::kI : FrameType::kP;
  f.codec = Codec::kH265;
  f.fps = 25;
  f.timestamp_ms = ts_ms;
  f.data.assign(static_cast<size_t>(payload), static_cast<uint8_t>(channel));
  return f;
}

}  // namespace

int main(int argc, char** argv) {
  std::string root = "/tmp/nvr_storage";
  std::string rtsp_url;
  std::string onvif_url, onvif_user, onvif_pass;
  bool onvif_discover = false;
  int seconds = 0;  // 0 => run until Ctrl-C (live mode)
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc)
      root = argv[++i];
    else if (std::strcmp(argv[i], "--rtsp") == 0 && i + 1 < argc)
      rtsp_url = argv[++i];
    else if (std::strcmp(argv[i], "--onvif-discover") == 0)
      onvif_discover = true;
    else if (std::strcmp(argv[i], "--onvif") == 0 && i + 1 < argc)
      onvif_url = argv[++i];
    else if (std::strcmp(argv[i], "--user") == 0 && i + 1 < argc)
      onvif_user = argv[++i];
    else if (std::strcmp(argv[i], "--pass") == 0 && i + 1 < argc)
      onvif_pass = argv[++i];
    else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
      seconds = std::atoi(argv[++i]);
  }

  // ONVIF LAN discovery: print devices and exit (no storage needed).
  if (onvif_discover) {
    auto devices = OnvifProbe(seconds ? seconds * 1000 : 3000);
    NVR_LOGI("onvif discovery found %zu device(s)", devices.size());
    for (const auto& d : devices)
      NVR_LOGI("  xaddr=%s uuid=%s scopes=%s", d.xaddr.c_str(),
               d.uuid.c_str(), d.scopes.c_str());
    return 0;
  }

  NVR_LOGI("nvrd starting, storage root = %s", root.c_str());

  // Small layout so the demo does not allocate real 900GB per disk.
  DiskLayout layout;
  layout.data_file_size = 64 * 1024 * 1024;  // 64MB data files
  layout.data_files_per_disk = 8;
  layout.log_capacity = 65536;

  StorageEngine storage(layout);
  if (!storage.Init(root)) {
    NVR_LOGE("storage init failed (create per-disk dirs under %s first)",
             root.c_str());
    return 1;
  }

  Recorder recorder(&storage);
  MediaHub hub;  // one camera's distribution node
  hub.Subscribe([&](const Frame& f) { recorder.OnFrame(f); });

  RecordPolicy policy;
  policy.timed = true;  // continuous recording for the demo channel
  recorder.SetPolicy(0, policy);

  if (!rtsp_url.empty() || !onvif_url.empty()) {
    // Live ingest: (RTSP|ONVIF)Source -> MediaHub -> Recorder -> StorageEngine.
    CameraConfig cfg;
    cfg.channel = 0;
    if (!onvif_url.empty()) {
      cfg.protocol = CameraConfig::Protocol::kOnvif;
      cfg.main_url = onvif_url;
      cfg.user = onvif_user;
      cfg.pass = onvif_pass;
    } else {
      cfg.protocol = CameraConfig::Protocol::kRtsp;
      cfg.main_url = rtsp_url;
    }
    auto src = CreateStreamSource(cfg);
    if (!src) {
      NVR_LOGE("cannot create source (built without FFmpeg?)");
      return 1;
    }
    src->set_callback(StreamKind::kMain, [&](const Frame& f) { hub.Publish(f); });

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    src->Start();
    NVR_LOGI("ingesting %s (Ctrl-C to stop)%s",
             onvif_url.empty() ? rtsp_url.c_str() : onvif_url.c_str(),
             seconds ? "" : " ...");

    uint64_t start = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    while (!g_stop.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (seconds) {
        uint64_t now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        if (now - start >= static_cast<uint64_t>(seconds)) break;
      }
    }
    src->Stop();
    storage.Flush();
    const auto& end = storage.global_end();
    NVR_LOGI("stopped. global end: disk=%d slot=%lld found=%d", end.disk_index,
             static_cast<long long>(end.slot), end.found);
    return 0;
  }

  // Synthetic demo: a couple of seconds of frames (1 I-frame/sec + P frames).
  uint64_t ts = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  for (int s = 0; s < 3; ++s) {
    hub.Publish(MakeFrame(0, true, ts, 2048));  // key frame
    ts += 40;
    for (int p = 0; p < 24; ++p) {
      hub.Publish(MakeFrame(0, false, ts, 1024));
      ts += 40;
    }
  }

  recorder.TriggerEvent(0, RecordType::kHuman, ts);
  storage.Flush();

  const auto& end = storage.global_end();
  NVR_LOGI("done. global end: disk=%d slot=%lld found=%d", end.disk_index,
           static_cast<long long>(end.slot), end.found);
  return 0;
}
