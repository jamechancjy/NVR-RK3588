#include "nvr/record/recorder.h"

namespace nvr {

void Recorder::SetPolicy(int channel, const RecordPolicy& policy) {
  std::lock_guard<std::mutex> lock(mu_);
  channels_[channel].policy = policy;
}

void Recorder::TrimPreBuffer(ChannelState* st, uint64_t now_ms) {
  const uint64_t horizon = static_cast<uint64_t>(st->policy.pre_record_ms);
  while (!st->pre_buffer.empty() &&
         st->pre_buffer.front().timestamp_ms + horizon < now_ms) {
    // Keep at least back to the last key frame so playback starts cleanly.
    if (st->pre_buffer.size() > 1 && !st->pre_buffer[1].is_key() &&
        !st->pre_buffer.front().is_key()) {
      st->pre_buffer.pop_front();
    } else if (st->pre_buffer.front().timestamp_ms + horizon < now_ms) {
      st->pre_buffer.pop_front();
    } else {
      break;
    }
  }
}

void Recorder::OnFrame(const Frame& frame) {
  std::lock_guard<std::mutex> lock(mu_);
  ChannelState& st = channels_[frame.channel];
  uint64_t now = frame.timestamp_ms;

  // Timed (continuous) recording: write everything.
  if (st.policy.timed) {
    storage_->WriteFrame(frame, RecordType::kTimed);
    st.timed_started = true;
  }

  // Event recording window.
  if (st.event_active) {
    if (now <= st.event_until_ms) {
      if (!st.policy.timed)  // avoid double-writing when timed already stored it
        storage_->WriteFrame(frame, st.event_type);
    } else {
      st.event_active = false;
    }
  }

  // Maintain the pre-record ring buffer for future events.
  st.pre_buffer.push_back(frame);
  TrimPreBuffer(&st, now);
}

void Recorder::TriggerEvent(int channel, RecordType type, uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  ChannelState& st = channels_[channel];

  bool was_active = st.event_active;
  st.event_active = true;
  st.event_type = type;
  st.event_until_ms = now_ms + st.policy.post_event_ms;

  // On a fresh event, flush the pre-record buffer so the clip starts before the
  // trigger. Skip if timed recording already persists these frames.
  if (!was_active && !st.policy.timed) {
    for (const auto& f : st.pre_buffer) storage_->WriteFrame(f, type);
  }
}

}  // namespace nvr
