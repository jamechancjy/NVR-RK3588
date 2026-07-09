#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "nvr/common/types.h"

namespace nvr {

struct Frame;
// Callback invoked for every compressed frame flowing through the pipeline.
using FrameCallback = std::function<void(const Frame&)>;

// On-disk, fixed-size frame header written before every frame payload inside a
// 1GB data file. Layout matches the design doc section 2.7:
//   [通道序号][音频/视频标志][帧率][I/P 帧标志][帧长度][帧数据...]
// A magic word is added so a scan can resynchronise after a corrupted region.
#pragma pack(push, 1)
struct FrameHeader {
  uint32_t magic;        // kFrameMagic
  uint16_t channel;      // 通道序号 (0..kMaxChannels-1)
  uint8_t media_type;    // MediaType (音频/视频标志)
  uint8_t frame_type;    // FrameType (I/P 帧标志)
  uint16_t codec;        // Codec
  uint16_t fps;          // 帧率
  uint64_t timestamp_ms; // presentation timestamp (epoch ms)
  uint32_t data_len;     // 帧长度 (bytes of payload following this header)
  uint32_t header_crc;   // crc32 of the header with this field zeroed
};
#pragma pack(pop)

constexpr uint32_t kFrameMagic = 0x4E565246;  // "NVRF"
static_assert(sizeof(FrameHeader) == 28, "FrameHeader must stay packed/stable");

// In-memory representation of a frame flowing through the pipeline.
struct Frame {
  int channel = 0;
  MediaType media_type = MediaType::kVideo;
  FrameType frame_type = FrameType::kP;
  Codec codec = Codec::kH265;
  int fps = 25;
  uint64_t timestamp_ms = 0;
  std::vector<uint8_t> data;

  bool is_key() const { return frame_type == FrameType::kI; }
};

}  // namespace nvr
