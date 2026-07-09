#include "nvr/storage/log_index.h"

#include <fcntl.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "nvr/common/logging.h"

namespace nvr {

namespace {
// Fixed record layout (see header):
//   "V" A0003 "_CH" 018 "_" 20260126152030 "_" 10 "_" P "_" 000000001234 "_" E "\n"
constexpr int kFileW = 5;
constexpr int kChanW = 3;
constexpr int kTsW = 14;
constexpr int kIdxW = 2;
constexpr int kOffW = 12;
// 1(v)+5+3(_CH)+3+1+14+1+2+1+1+1+12+1+1+1(\n)
constexpr size_t kRecordSize = 48;
}  // namespace

size_t LogRecordSize() { return kRecordSize; }

std::string LogRecord::ToLine() const {
  char file_buf[kFileW + 1];
  std::snprintf(file_buf, sizeof(file_buf), "%-*.*s", kFileW, kFileW,
                file.c_str());

  std::time_t t = static_cast<std::time_t>(time_sec);
  std::tm tm_buf{};
  gmtime_r(&t, &tm_buf);
  char ts_buf[kTsW + 1];
  std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%d%H%M%S", &tm_buf);

  char buf[kRecordSize + 1];
  std::snprintf(buf, sizeof(buf),
                "%c%s_CH%0*u_%s_%0*u_%c_%0*" PRIu64 "_%c\n",
                valid ? 'V' : '.', file_buf, kChanW,
                static_cast<unsigned>(channel), ts_buf, kIdxW,
                static_cast<unsigned>(iframe_idx),
                static_cast<char>(type), kOffW, offset, end ? 'E' : '-');
  return std::string(buf, kRecordSize);
}

bool LogRecord::FromLine(const std::string& s, LogRecord* out) {
  if (s.size() < kRecordSize) return false;
  LogRecord r;
  r.valid = (s[0] == 'V');
  r.end = (s[s.size() >= kRecordSize ? kRecordSize - 2 : 0] == 'E');

  // file: chars [1 .. 1+kFileW)
  std::string file = s.substr(1, kFileW);
  size_t sp = file.find(' ');
  if (sp != std::string::npos) file = file.substr(0, sp);
  r.file = file;

  unsigned chan = 0, idx = 0;
  char ty = 'T';
  char ts[kTsW + 1] = {0};
  unsigned long long off = 0;
  // Parse the structured body. Offsets are fixed but sscanf keeps it readable.
  if (std::sscanf(s.c_str() + 1 + kFileW, "_CH%3u_%14[0-9]_%2u_%c_%12llu", &chan,
                  ts, &idx, &ty, &off) != 5) {
    // Blank / cleared record: still valid line, just not a data entry.
    if (!r.valid) {
      *out = r;
      return true;
    }
    return false;
  }
  r.channel = static_cast<uint16_t>(chan);
  r.iframe_idx = static_cast<uint8_t>(idx);
  r.type = static_cast<RecordType>(ty);
  r.offset = static_cast<uint64_t>(off);

  std::tm tm_buf{};
  if (std::strlen(ts) == kTsW) {
    int y, mo, d, h, mi, se;
    std::sscanf(ts, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &se);
    tm_buf.tm_year = y - 1900;
    tm_buf.tm_mon = mo - 1;
    tm_buf.tm_mday = d;
    tm_buf.tm_hour = h;
    tm_buf.tm_min = mi;
    tm_buf.tm_sec = se;
    r.time_sec = static_cast<uint64_t>(timegm(&tm_buf));
  }
  *out = r;
  return true;
}

LogIndex::~LogIndex() { Close(); }

bool LogIndex::Open(const std::string& path, uint64_t capacity) {
  Close();
  path_ = path;
  capacity_ = capacity;

  bool exists = access(path.c_str(), F_OK) == 0;
  fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd_ < 0) {
    NVR_LOGE("open log %s failed: %s", path.c_str(), std::strerror(errno));
    return false;
  }

  if (!exists) {
    // Pre-fill the whole file with blank fixed-length records so its size is
    // fixed from the start (design doc: file 固定大小, 只改不删).
    LogRecord blank;
    blank.valid = false;
    std::string line = blank.ToLine();
    for (uint64_t i = 0; i < capacity_; ++i) {
      if (::write(fd_, line.data(), line.size()) !=
          static_cast<ssize_t>(line.size())) {
        NVR_LOGE("prefill log failed at slot %" PRIu64, i);
        return false;
      }
    }
    cursor_ = 0;
  }
  return true;
}

void LogIndex::Close() {
  if (fd_ >= 0) {
    ::fsync(fd_);
    ::close(fd_);
    fd_ = -1;
  }
}

bool LogIndex::ReadSlot(uint64_t slot, LogRecord* out) const {
  if (fd_ < 0 || slot >= capacity_) return false;
  std::string buf(kRecordSize, '\0');
  ssize_t n = ::pread(fd_, &buf[0], kRecordSize, slot * kRecordSize);
  if (n != static_cast<ssize_t>(kRecordSize)) return false;
  return LogRecord::FromLine(buf, out);
}

bool LogIndex::WriteAt(uint64_t slot, const LogRecord& rec) {
  std::string line = rec.ToLine();
  ssize_t n = ::pwrite(fd_, line.data(), line.size(), slot * kRecordSize);
  return n == static_cast<ssize_t>(line.size());
}

bool LogIndex::WriteSlot(uint64_t slot, const LogRecord& rec) {
  if (fd_ < 0 || slot >= capacity_) return false;
  return WriteAt(slot, rec);
}

bool LogIndex::Append(const LogRecord& rec, uint64_t* slot_out) {
  if (fd_ < 0) return false;

  // Clear the end flag on the previous record (the last written slot).
  uint64_t prev = (cursor_ + capacity_ - 1) % capacity_;
  LogRecord prev_rec;
  if (ReadSlot(prev, &prev_rec) && prev_rec.end) {
    prev_rec.end = false;
    WriteAt(prev, prev_rec);
  }

  LogRecord r = rec;
  r.valid = true;
  r.end = true;
  if (!WriteAt(cursor_, r)) return false;

  if (slot_out) *slot_out = cursor_;
  cursor_ = (cursor_ + 1) % capacity_;
  return true;
}

int64_t LogIndex::FindEndSlot() const {
  for (uint64_t i = 0; i < capacity_; ++i) {
    LogRecord r;
    if (ReadSlot(i, &r) && r.end) return static_cast<int64_t>(i);
  }
  return -1;
}

void LogIndex::ClearAllEndFlags() {
  for (uint64_t i = 0; i < capacity_; ++i) {
    LogRecord r;
    if (ReadSlot(i, &r) && r.end) {
      r.end = false;
      WriteAt(i, r);
    }
  }
}

void LogIndex::SetCursorAfter(int64_t slot) {
  if (slot < 0) {
    cursor_ = 0;
  } else {
    cursor_ = (static_cast<uint64_t>(slot) + 1) % capacity_;
  }
}

}  // namespace nvr
