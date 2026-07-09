#pragma once

#include <cstdint>
#include <string>

#include "nvr/storage/frame.h"

namespace nvr {

// A single pre-allocated fixed-size data file (default 1GB) named A0001, A0002...
// Frames from all channels are appended sequentially (先来先存). Writes are
// buffered and flushed to disk every |flush_frames| frames (design doc 2.7).
class RecordFile {
 public:
  RecordFile() = default;
  ~RecordFile();

  // Opens (creating and pre-allocating to |size_bytes| if needed) the file.
  bool Open(const std::string& path, uint64_t size_bytes, int flush_frames = 100);
  void Close();

  bool is_open() const { return fd_ >= 0; }
  uint64_t size() const { return size_bytes_; }
  uint64_t write_offset() const { return write_offset_; }

  // Remaining free bytes in the file.
  uint64_t remaining() const { return size_bytes_ - write_offset_; }

  // True if |frame| (header + payload) no longer fits.
  bool WouldOverflow(const Frame& frame) const;

  // Appends a frame at the current write offset. On success |offset_out| holds
  // the byte offset where the frame header was written (used for the I-frame
  // index entry).
  bool Append(const Frame& frame, uint64_t* offset_out);

  // Reads a frame located at |offset|.
  bool ReadAt(uint64_t offset, Frame* out) const;

  // Resets the write cursor (used when a file is reused during circular wrap).
  void ResetWriteOffset() { write_offset_ = 0; }
  void SetWriteOffset(uint64_t off) { write_offset_ = off; }

  // Scans forward from |start| following valid frame headers and returns the
  // offset just past the last intact frame. Used on startup to resume appending
  // in a partially written file.
  uint64_t ScanAppendOffset(uint64_t start) const;

  bool Flush();

 private:
  int fd_ = -1;
  std::string path_;
  uint64_t size_bytes_ = 0;
  uint64_t write_offset_ = 0;
  int flush_frames_ = 100;
  int frames_since_flush_ = 0;
};

}  // namespace nvr
