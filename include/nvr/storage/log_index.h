#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nvr/common/types.h"

namespace nvr {

// Fixed-length log record persisted in log.txt. Human-readable form (matches the
// design doc, one record per I frame):
//   "A0003_CH018_20260126152030_10_P_000000001234_E\n"
//    file   chan   timestamp     idx ty  offset       end-flag
//
// Rules (design doc 2.6):
//  * Every record has the SAME byte length (kRecordSize).
//  * Records are only MODIFIED or CLEARED, never removed (file stays fixed size).
//  * Exactly one record in the whole system carries the 'E' (end) flag: the
//    write pointer. Writing a new record sets 'E' on it and clears the previous.
struct LogRecord {
  std::string file;       // data file name, e.g. "A0003"
  uint16_t channel = 0;   // CHnnn
  uint64_t time_sec = 0;  // epoch seconds
  uint8_t iframe_idx = 1; // Nth I frame within that second (usually 1)
  RecordType type = RecordType::kTimed;
  uint64_t offset = 0;    // byte offset of the I frame inside the data file
  bool valid = false;     // false => a cleared slot (blank record)
  bool end = false;       // end/write-pointer marker

  std::string ToLine() const;                 // fixed-width serialisation
  static bool FromLine(const std::string& s, LogRecord* out);
};

// Size of one serialised record in bytes (including trailing '\n').
size_t LogRecordSize();

// Manages the fixed-size log.txt of a single logical disk as a circular array of
// fixed-length records.
class LogIndex {
 public:
  LogIndex() = default;
  ~LogIndex();

  // Opens (or creates) log.txt at |path| holding |capacity| records. When the
  // file is created it is filled with blank (cleared) records so its size is
  // fixed up front.
  bool Open(const std::string& path, uint64_t capacity);
  void Close();

  bool is_open() const { return fd_ >= 0; }
  uint64_t capacity() const { return capacity_; }
  const std::string& path() const { return path_; }

  // Appends |rec| at the circular write position, marks it as the end record and
  // clears the end flag of the previous record. Returns the slot index used.
  bool Append(const LogRecord& rec, uint64_t* slot_out = nullptr);

  bool ReadSlot(uint64_t slot, LogRecord* out) const;
  bool WriteSlot(uint64_t slot, const LogRecord& rec);

  // Returns the slot index carrying the end flag, or -1 if none present.
  int64_t FindEndSlot() const;

  // Clears the end flag on every slot (used when de-duplicating global ends).
  void ClearAllEndFlags();

  // Sets the internal write cursor to the slot after |slot|.
  void SetCursorAfter(int64_t slot);
  uint64_t cursor() const { return cursor_; }

 private:
  bool WriteAt(uint64_t slot, const LogRecord& rec);

  int fd_ = -1;
  std::string path_;
  uint64_t capacity_ = 0;  // number of record slots
  uint64_t cursor_ = 0;    // next slot to write
};

}  // namespace nvr
