#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "nvr/storage/frame.h"
#include "nvr/storage/storage_engine.h"

namespace nvr {

// Per-channel recording policy.
struct RecordPolicy {
  bool timed = false;          // T: continuous/scheduled recording
  int pre_record_ms = 5000;    // pre-event buffer duration (RING BUFFER 预录像)
  int post_event_ms = 10000;   // keep recording this long after an event
};

// Recorder bridges the media hub to the storage engine. It keeps a small
// per-channel ring buffer so event recordings (报警/移动/人形) include the frames
// leading up to the trigger, and continuously records channels in timed mode.
class Recorder {
 public:
  explicit Recorder(StorageEngine* storage) : storage_(storage) {}

  void SetPolicy(int channel, const RecordPolicy& policy);

  // Feed every received compressed frame here (MediaHub consumer).
  void OnFrame(const Frame& frame);

  // Raise an event that starts/extends event recording on |channel|, flushing
  // the pre-record buffer first.
  void TriggerEvent(int channel, RecordType type, uint64_t now_ms);

 private:
  struct ChannelState {
    RecordPolicy policy;
    std::deque<Frame> pre_buffer;  // recent frames for pre-record
    bool event_active = false;
    RecordType event_type = RecordType::kMotion;
    uint64_t event_until_ms = 0;
    bool timed_started = false;
  };

  void TrimPreBuffer(ChannelState* st, uint64_t now_ms);

  StorageEngine* storage_;
  std::mutex mu_;
  std::unordered_map<int, ChannelState> channels_;
};

}  // namespace nvr
