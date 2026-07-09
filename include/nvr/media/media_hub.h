#pragma once

#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "nvr/storage/frame.h"

namespace nvr {

// Zero-fan-out distributor for one camera stream (the "数据分发" node in the data
// flow diagram). A single received compressed stream is dispatched by reference
// to every registered consumer (recorder, decoder/preview, RTSP re-streamers)
// without re-pulling from the camera.
class MediaHub {
 public:
  using ConsumerId = int;

  ConsumerId Subscribe(FrameCallback cb) {
    std::lock_guard<std::mutex> lock(mu_);
    ConsumerId id = next_id_++;
    consumers_[id] = std::move(cb);
    return id;
  }

  void Unsubscribe(ConsumerId id) {
    std::lock_guard<std::mutex> lock(mu_);
    consumers_.erase(id);
  }

  // Called by the stream source thread for each frame.
  void Publish(const Frame& f) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& kv : consumers_) kv.second(f);
  }

  size_t consumer_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return consumers_.size();
  }

 private:
  mutable std::mutex mu_;
  std::unordered_map<ConsumerId, FrameCallback> consumers_;
  ConsumerId next_id_ = 1;
};

}  // namespace nvr
