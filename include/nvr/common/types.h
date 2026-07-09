#pragma once

#include <cstdint>

namespace nvr {

// Maximum number of camera channels the system supports.
constexpr int kMaxChannels = 196;

// Codec identifiers carried in the on-disk frame header.
enum class Codec : uint16_t {
  kUnknown = 0,
  kH264 = 1,
  kH265 = 2,
  kAAC = 100,
};

// Media kind of a frame (音频/视频标志).
enum class MediaType : uint8_t {
  kVideo = 0,
  kAudio = 1,
};

// Frame coding type (I/P 帧标志). Only I frames are indexed in log.txt.
enum class FrameType : uint8_t {
  kP = 0,  // P/B or any non-keyframe
  kI = 1,  // keyframe (indexed, seekable)
};

// Recording trigger type, persisted as a single char in log.txt.
//   A = IO 报警 (external IO alarm)
//   M = 移动报警 (motion detection)
//   P = 人形报警 (human/person detection)
//   T = 定时录像 (scheduled/continuous)
enum class RecordType : char {
  kIoAlarm = 'A',
  kMotion = 'M',
  kHuman = 'P',
  kTimed = 'T',
};

// Stream variant of a camera.
enum class StreamKind : uint8_t {
  kMain = 0,  // 主码流
  kSub = 1,   // 子码流
};

}  // namespace nvr
