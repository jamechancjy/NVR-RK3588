// nvrd - RK3588 NVR daemon (skeleton).
//
// This entry point currently demonstrates the storage pipeline end to end using
// a synthetic frame source, so the design can be validated on a host build. On
// the target it will wire real StreamSources (RTSP/ONVIF/GB28181) -> MediaHub ->
// Recorder -> StorageEngine, plus preview (MPP decode + WebRTC) and the LVGL UI.

#include <chrono>
#include <cstring>
#include <string>

#include "nvr/common/logging.h"
#include "nvr/media/media_hub.h"
#include "nvr/record/recorder.h"
#include "nvr/storage/storage_engine.h"

using namespace nvr;

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
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--root") == 0 && i + 1 < argc) root = argv[++i];
  }

  NVR_LOGI("nvrd starting, storage root = %s", root.c_str());

  // Small layout so the demo does not allocate real 900GB per disk.
  DiskLayout layout;
  layout.data_file_size = 4 * 1024 * 1024;  // 4MB data files
  layout.data_files_per_disk = 4;
  layout.log_capacity = 1024;

  StorageEngine storage(layout);
  if (!storage.Init(root)) {
    NVR_LOGE("storage init failed (create per-disk dirs under %s first)",
             root.c_str());
    return 1;
  }

  Recorder recorder(&storage);
  MediaHub hub;  // one camera's distribution node (demo)
  hub.Subscribe([&](const Frame& f) { recorder.OnFrame(f); });

  RecordPolicy policy;
  policy.timed = true;  // continuous recording for the demo channel
  recorder.SetPolicy(0, policy);

  // Push a couple of seconds of synthetic frames (1 I-frame/sec + P frames).
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
