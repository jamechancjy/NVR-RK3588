#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "nvr/storage/frame.h"

namespace nvr {

// MPEG program-stream (PS) depacketiser used by GB28181. Devices send video (and
// audio) wrapped in MPEG-PS over RTP; this class reassembles the byte stream,
// parses pack/system/PSM/PES structures, extracts the elementary stream and
// emits one Frame per access unit. Codec is taken from the Program Stream Map
// when present (0x1B=H.264, 0x24=H.265, 0x90/0x0F=audio), otherwise inferred
// from NAL structure. Key-frame detection scans NAL units of the access unit.
//
// Feed() accepts arbitrary-sized chunks (e.g. reassembled RTP payloads); partial
// trailing data is buffered until the next start code arrives.
class PsDepacketizer {
 public:
  using FrameCb = std::function<void(const Frame&)>;

  PsDepacketizer(int channel, FrameCb cb);

  void Feed(const uint8_t* data, size_t len);
  void Flush();  // emit any buffered access unit

  // Stats for tests / diagnostics.
  uint64_t frames() const { return frames_; }
  uint64_t key_frames() const { return key_frames_; }

 private:
  void ProcessUnit(const uint8_t* p, size_t len);
  void ParsePsm(const uint8_t* p, size_t len);
  void OnVideoEs(const uint8_t* es, size_t len, bool has_pts, uint64_t pts_ms);
  void EmitAccessUnit();

  int channel_;
  FrameCb cb_;

  std::vector<uint8_t> buf_;     // stream reassembly buffer
  std::vector<uint8_t> au_;      // current access unit ES
  bool au_has_pts_ = false;
  uint64_t au_pts_ms_ = 0;

  Codec video_codec_ = Codec::kUnknown;  // from PSM, else inferred
  int video_stream_id_ = -1;

  uint64_t frames_ = 0;
  uint64_t key_frames_ = 0;
};

}  // namespace nvr
