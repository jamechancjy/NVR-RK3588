#include "nvr/storage/record_file.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "nvr/common/crc32.h"
#include "nvr/common/logging.h"

namespace nvr {

RecordFile::~RecordFile() { Close(); }

bool RecordFile::Open(const std::string& path, uint64_t size_bytes,
                      int flush_frames) {
  Close();
  path_ = path;
  size_bytes_ = size_bytes;
  flush_frames_ = flush_frames;
  write_offset_ = 0;
  frames_since_flush_ = 0;

  bool exists = access(path.c_str(), F_OK) == 0;
  fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd_ < 0) {
    NVR_LOGE("open data file %s failed: %s", path.c_str(),
             std::strerror(errno));
    return false;
  }
  if (!exists) {
    // Pre-allocate so recording never waits on file growth (design doc 2.6).
    if (::ftruncate(fd_, static_cast<off_t>(size_bytes_)) != 0) {
      NVR_LOGE("preallocate %s failed: %s", path.c_str(),
               std::strerror(errno));
      return false;
    }
#if defined(__linux__) && defined(NVR_PLATFORM_RK3588)
    ::posix_fallocate(fd_, 0, static_cast<off_t>(size_bytes_));
#endif
  }
  return true;
}

void RecordFile::Close() {
  if (fd_ >= 0) {
    Flush();
    ::close(fd_);
    fd_ = -1;
  }
}

bool RecordFile::WouldOverflow(const Frame& frame) const {
  uint64_t need = sizeof(FrameHeader) + frame.data.size();
  return write_offset_ + need > size_bytes_;
}

bool RecordFile::Append(const Frame& frame, uint64_t* offset_out) {
  if (fd_ < 0) return false;
  if (WouldOverflow(frame)) return false;

  FrameHeader h{};
  h.magic = kFrameMagic;
  h.channel = static_cast<uint16_t>(frame.channel);
  h.media_type = static_cast<uint8_t>(frame.media_type);
  h.frame_type = static_cast<uint8_t>(frame.frame_type);
  h.codec = static_cast<uint16_t>(frame.codec);
  h.fps = static_cast<uint16_t>(frame.fps);
  h.timestamp_ms = frame.timestamp_ms;
  h.data_len = static_cast<uint32_t>(frame.data.size());
  h.header_crc = 0;
  h.header_crc = Crc32(&h, sizeof(h));

  uint64_t off = write_offset_;
  if (::pwrite(fd_, &h, sizeof(h), off) != static_cast<ssize_t>(sizeof(h)))
    return false;
  if (!frame.data.empty()) {
    if (::pwrite(fd_, frame.data.data(), frame.data.size(),
                 off + sizeof(h)) !=
        static_cast<ssize_t>(frame.data.size()))
      return false;
  }

  write_offset_ = off + sizeof(h) + frame.data.size();
  if (offset_out) *offset_out = off;

  if (++frames_since_flush_ >= flush_frames_) Flush();
  return true;
}

bool RecordFile::ReadAt(uint64_t offset, Frame* out) const {
  if (fd_ < 0) return false;
  FrameHeader h{};
  if (::pread(fd_, &h, sizeof(h), offset) != static_cast<ssize_t>(sizeof(h)))
    return false;
  if (h.magic != kFrameMagic) return false;
  uint32_t crc = h.header_crc;
  h.header_crc = 0;
  if (Crc32(&h, sizeof(h)) != crc) return false;

  out->channel = h.channel;
  out->media_type = static_cast<MediaType>(h.media_type);
  out->frame_type = static_cast<FrameType>(h.frame_type);
  out->codec = static_cast<Codec>(h.codec);
  out->fps = h.fps;
  out->timestamp_ms = h.timestamp_ms;
  out->data.resize(h.data_len);
  if (h.data_len) {
    if (::pread(fd_, out->data.data(), h.data_len, offset + sizeof(h)) !=
        static_cast<ssize_t>(h.data_len))
      return false;
  }
  return true;
}

uint64_t RecordFile::ScanAppendOffset(uint64_t start) const {
  if (fd_ < 0) return start;
  uint64_t off = start;
  while (off + sizeof(FrameHeader) <= size_bytes_) {
    FrameHeader h{};
    if (::pread(fd_, &h, sizeof(h), off) != static_cast<ssize_t>(sizeof(h)))
      break;
    if (h.magic != kFrameMagic) break;
    uint32_t crc = h.header_crc;
    h.header_crc = 0;
    if (Crc32(&h, sizeof(h)) != crc) break;
    off += sizeof(FrameHeader) + h.data_len;
  }
  return off;
}

bool RecordFile::Flush() {
  if (fd_ < 0) return false;
  frames_since_flush_ = 0;
  return ::fsync(fd_) == 0;
}

}  // namespace nvr
