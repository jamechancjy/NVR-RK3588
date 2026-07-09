#include "nvr/media/ps_depacketizer.h"

#include <chrono>
#include <cstring>
#include <string>

namespace nvr {

namespace {

uint64_t NowMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

// A PS start code is 00 00 01 XX where XX >= 0xB9 (pack/system/PSM/PES/...).
// NAL start codes inside video ES are 00 00 01 XX with XX <= 0x7F, so they are
// never mistaken for PS boundaries.
bool IsPsStart(const uint8_t* p, size_t len) {
  return len >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 1 && p[3] >= 0xB9;
}

// Find the next PS start code at or after `from`. Returns npos if none.
size_t FindPsStart(const std::vector<uint8_t>& b, size_t from) {
  if (b.size() < 4) return std::string::npos;
  for (size_t i = from; i + 4 <= b.size(); ++i) {
    if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1 && b[i + 3] >= 0xB9)
      return i;
  }
  return std::string::npos;
}

uint64_t DecodePts(const uint8_t* p) {
  uint64_t pts = (static_cast<uint64_t>(p[0] & 0x0E) << 29) |
                 (static_cast<uint64_t>(p[1]) << 22) |
                 (static_cast<uint64_t>(p[2] & 0xFE) << 14) |
                 (static_cast<uint64_t>(p[3]) << 7) |
                 (static_cast<uint64_t>(p[4]) >> 1);
  return pts / 90;  // 90kHz -> ms
}

// Iterate NAL units (Annex-B) in an access unit, invoking fn(nal_ptr, nal_len).
template <typename F>
void ForEachNal(const uint8_t* d, size_t n, F fn) {
  size_t i = 0;
  while (i + 3 <= n) {
    // find start code 00 00 01 (allow 4-byte 00 00 00 01)
    if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
      size_t start = i + 3;
      size_t j = start;
      while (j + 3 <= n && !(d[j] == 0 && d[j + 1] == 0 && d[j + 2] == 1)) ++j;
      size_t end = (j + 3 <= n) ? j : n;
      // strip a trailing zero belonging to the next 4-byte start code
      size_t len = end - start;
      if (end < n && end >= 1 && d[end - 1] == 0) --len;
      if (len > 0) fn(d + start, len);
      i = end;
    } else {
      ++i;
    }
  }
}

}  // namespace

PsDepacketizer::PsDepacketizer(int channel, FrameCb cb)
    : channel_(channel), cb_(std::move(cb)) {}

void PsDepacketizer::Feed(const uint8_t* data, size_t len) {
  buf_.insert(buf_.end(), data, data + len);

  size_t pos = FindPsStart(buf_, 0);
  if (pos == std::string::npos) {
    // keep only the last few bytes (possible partial start code)
    if (buf_.size() > 3)
      buf_.erase(buf_.begin(), buf_.end() - 3);
    return;
  }
  // drop any leading garbage before the first start code
  if (pos > 0) buf_.erase(buf_.begin(), buf_.begin() + pos);

  size_t cur = 0;
  for (;;) {
    size_t next = FindPsStart(buf_, cur + 3);
    if (next == std::string::npos) break;  // wait for more data
    ProcessUnit(&buf_[cur], next - cur);
    cur = next;
  }
  if (cur > 0) buf_.erase(buf_.begin(), buf_.begin() + cur);
}

void PsDepacketizer::Flush() {
  // End of stream: the final PES has no trailing start code to delimit it, so
  // process whatever remains in the buffer as one last unit.
  if (buf_.size() >= 4 && IsPsStart(buf_.data(), buf_.size())) {
    ProcessUnit(buf_.data(), buf_.size());
    buf_.clear();
  }
  if (!au_.empty()) EmitAccessUnit();
}

void PsDepacketizer::ProcessUnit(const uint8_t* p, size_t len) {
  if (!IsPsStart(p, len)) return;
  uint8_t stream_id = p[3];

  if (stream_id == 0xBA) return;             // pack header
  if (stream_id == 0xBB) return;             // system header
  if (stream_id == 0xBC) {                    // program stream map
    ParsePsm(p, len);
    return;
  }
  if (stream_id == 0xBE || stream_id == 0xBF) return;  // padding / private2

  bool is_video = stream_id >= 0xE0 && stream_id <= 0xEF;
  if (!is_video) return;  // audio (0xC0-0xDF) not handled yet

  if (len < 9) return;
  // PES header
  uint8_t pts_dts_flags = (p[7] & 0xC0) >> 6;
  uint8_t pes_hdr_len = p[8];
  size_t es_off = 9 + pes_hdr_len;
  if (es_off > len) return;

  bool has_pts = (pts_dts_flags & 0x02) != 0;
  uint64_t pts_ms = 0;
  if (has_pts && len >= 14) pts_ms = DecodePts(&p[9]);

  OnVideoEs(p + es_off, len - es_off, has_pts, pts_ms);
}

void PsDepacketizer::ParsePsm(const uint8_t* p, size_t len) {
  if (len < 16) return;
  size_t info_len = (static_cast<size_t>(p[8]) << 8) | p[9];
  size_t idx = 10 + info_len;
  if (idx + 2 > len) return;
  size_t es_map_len = (static_cast<size_t>(p[idx]) << 8) | p[idx + 1];
  idx += 2;
  size_t end = idx + es_map_len;
  if (end > len) end = len;
  while (idx + 4 <= end) {
    uint8_t stream_type = p[idx];
    uint8_t es_id = p[idx + 1];
    size_t es_info_len = (static_cast<size_t>(p[idx + 2]) << 8) | p[idx + 3];
    if (es_id >= 0xE0 && es_id <= 0xEF) {
      video_stream_id_ = es_id;
      if (stream_type == 0x1B)
        video_codec_ = Codec::kH264;
      else if (stream_type == 0x24)
        video_codec_ = Codec::kH265;
    }
    idx += 4 + es_info_len;
  }
}

void PsDepacketizer::OnVideoEs(const uint8_t* es, size_t len, bool has_pts,
                               uint64_t pts_ms) {
  if (has_pts && !au_.empty()) EmitAccessUnit();
  if (has_pts) {
    au_has_pts_ = true;
    au_pts_ms_ = pts_ms;
  }
  au_.insert(au_.end(), es, es + len);
}

void PsDepacketizer::EmitAccessUnit() {
  if (au_.empty()) return;

  // Codec: PSM value if known, else infer from NAL structure.
  Codec codec = video_codec_;
  bool key = false;

  bool looks_h265 = false;
  ForEachNal(au_.data(), au_.size(), [&](const uint8_t* nal, size_t n) {
    if (n < 1) return;
    // H.265 IRAP/param sets under the (type = (b>>1)&0x3f) interpretation.
    uint8_t h265_type = (nal[0] >> 1) & 0x3F;
    uint8_t h264_type = nal[0] & 0x1F;
    if ((nal[0] & 0x81) == 0 && (h265_type == 32 || h265_type == 33 ||
                                 h265_type == 34))
      looks_h265 = true;
    // key-frame candidates for both codecs (final decision after codec known).
    (void)h264_type;
  });
  if (codec == Codec::kUnknown) codec = looks_h265 ? Codec::kH265 : Codec::kH264;

  ForEachNal(au_.data(), au_.size(), [&](const uint8_t* nal, size_t n) {
    if (n < 1) return;
    if (codec == Codec::kH265) {
      uint8_t t = (nal[0] >> 1) & 0x3F;
      if ((t >= 16 && t <= 21) || t == 32 || t == 33 || t == 34) key = true;
    } else {
      uint8_t t = nal[0] & 0x1F;
      if (t == 5 || t == 7 || t == 8) key = true;
    }
  });

  Frame f;
  f.channel = channel_;
  f.media_type = MediaType::kVideo;
  f.frame_type = key ? FrameType::kI : FrameType::kP;
  f.codec = codec;
  f.fps = 25;
  f.timestamp_ms = NowMs();  // wall-clock arrival (drives log/ordering)
  f.pts_ms = au_pts_ms_;
  f.data = au_;

  ++frames_;
  if (key) ++key_frames_;
  if (cb_) cb_(f);

  au_.clear();
  au_has_pts_ = false;
}

}  // namespace nvr
