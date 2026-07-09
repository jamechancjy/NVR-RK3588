// Lightweight unit tests for the storage engine (no external test framework).

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

#include "nvr/storage/disk_manager.h"
#include "nvr/storage/log_index.h"
#include "nvr/storage/record_file.h"
#include "nvr/storage/storage_engine.h"

namespace fs = std::filesystem;
using namespace nvr;

static int g_checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond,        \
                   __FILE__, __LINE__);                                 \
      return 1;                                                         \
    }                                                                   \
  } while (0)

static std::string MakeTmpDir(const char* name) {
  std::string dir = (fs::temp_directory_path() /
                     (std::string("nvr_test_") + name)).string();
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

static int TestLogRecordRoundtrip() {
  LogRecord r;
  r.file = "A0003";
  r.channel = 18;
  r.time_sec = 1769440830;  // some epoch
  r.iframe_idx = 10;
  r.type = RecordType::kHuman;
  r.offset = 1234;
  r.valid = true;
  r.end = true;

  std::string line = r.ToLine();
  CHECK(line.size() == LogRecordSize());

  LogRecord back;
  CHECK(LogRecord::FromLine(line, &back));
  CHECK(back.file == "A0003");
  CHECK(back.channel == 18);
  CHECK(back.iframe_idx == 10);
  CHECK(back.type == RecordType::kHuman);
  CHECK(back.offset == 1234);
  CHECK(back.valid);
  CHECK(back.end);
  CHECK(back.time_sec == r.time_sec);
  return 0;
}

static int TestLogIndexEndMarker() {
  std::string dir = MakeTmpDir("logidx");
  std::string path = (fs::path(dir) / "log.txt").string();
  {
    LogIndex log;
    CHECK(log.Open(path, 16));
    for (int i = 0; i < 5; ++i) {
      LogRecord r;
      r.file = "A0001";
      r.channel = i;
      r.offset = i * 100;
      CHECK(log.Append(r));
    }
    // Exactly one end marker after multiple appends.
    int ends = 0;
    for (uint64_t s = 0; s < log.capacity(); ++s) {
      LogRecord tmp;
      if (log.ReadSlot(s, &tmp) && tmp.end) ends++;
    }
    CHECK(ends == 1);
    int64_t es = log.FindEndSlot();
    CHECK(es == 4);
    LogRecord last;
    CHECK(log.ReadSlot(es, &last));
    CHECK(last.channel == 4);
  }
  // File size must be fixed (capacity * record size).
  CHECK(fs::file_size(path) == 16 * LogRecordSize());
  return 0;
}

static int TestRecordFileRoundtrip() {
  std::string dir = MakeTmpDir("recfile");
  std::string path = (fs::path(dir) / "A0001").string();
  RecordFile f;
  CHECK(f.Open(path, 1 << 20, 8));  // 1MB, flush every 8

  uint64_t off0 = 0, off1 = 0;
  Frame a;
  a.channel = 3;
  a.frame_type = FrameType::kI;
  a.timestamp_ms = 1000;
  a.data.assign(500, 0xAB);
  CHECK(f.Append(a, &off0));
  CHECK(off0 == 0);

  Frame b;
  b.channel = 3;
  b.frame_type = FrameType::kP;
  b.timestamp_ms = 1040;
  b.data.assign(300, 0xCD);
  CHECK(f.Append(b, &off1));
  CHECK(off1 > off0);

  Frame ra;
  CHECK(f.ReadAt(off0, &ra));
  CHECK(ra.channel == 3);
  CHECK(ra.is_key());
  CHECK(ra.data.size() == 500 && ra.data[0] == 0xAB);

  // Scan should land right after the two frames.
  uint64_t append = f.ScanAppendOffset(0);
  CHECK(append == f.write_offset());
  return 0;
}

static void PrepDisks(const std::string& root, int n) {
  for (int i = 0; i < n; ++i) {
    std::error_code ec;
    fs::create_directories(fs::path(root) / ("disk" + std::to_string(i)), ec);
  }
}

static DiskLayout SmallLayout() {
  DiskLayout l;
  l.data_file_size = 64 * 1024;  // 64KB files
  l.data_files_per_disk = 2;
  l.log_capacity = 64;
  return l;
}

static int TestStorageWriteAndRecover() {
  std::string root = MakeTmpDir("engine");
  PrepDisks(root, 2);

  // First run: format + write a few key frames.
  {
    StorageEngine eng(SmallLayout());
    CHECK(eng.Init(root));
    CHECK(eng.disk_count() == 2);
    for (int i = 0; i < 6; ++i) {
      Frame f;
      f.channel = 1;
      f.frame_type = FrameType::kI;
      f.timestamp_ms = 1000 + i * 1000;
      f.data.assign(200, static_cast<uint8_t>(i));
      CHECK(eng.WriteFrame(f, RecordType::kTimed));
    }
    eng.Flush();
    CHECK(eng.global_end().found);
  }

  // Second run: re-open, recovery must find exactly one global end.
  {
    StorageEngine eng(SmallLayout());
    CHECK(eng.Init(root));
    CHECK(eng.global_end().found);

    // Count end markers across all disks' logs -> must be exactly one.
    int total_ends = 0;
    DiskManager dm(SmallLayout());
    for (const auto& d : dm.Discover(root)) {
      LogIndex log;
      CHECK(log.Open((fs::path(d.mount) / "log.txt").string(),
                     dm.LogCapacity()));
      for (uint64_t s = 0; s < log.capacity(); ++s) {
        LogRecord r;
        if (log.ReadSlot(s, &r) && r.end) total_ends++;
      }
    }
    CHECK(total_ends == 1);
  }
  return 0;
}

int main() {
  struct { const char* name; int (*fn)(); } tests[] = {
      {"LogRecordRoundtrip", TestLogRecordRoundtrip},
      {"LogIndexEndMarker", TestLogIndexEndMarker},
      {"RecordFileRoundtrip", TestRecordFileRoundtrip},
      {"StorageWriteAndRecover", TestStorageWriteAndRecover},
  };
  for (auto& t : tests) {
    std::printf("running %s ...\n", t.name);
    if (t.fn() != 0) {
      std::fprintf(stderr, "TEST FAILED: %s\n", t.name);
      return 1;
    }
  }
  std::printf("all tests passed (%d checks)\n", g_checks);
  return 0;
}
