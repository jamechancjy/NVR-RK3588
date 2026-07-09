#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "nvr/storage/disk_manager.h"
#include "nvr/storage/frame.h"
#include "nvr/storage/log_index.h"
#include "nvr/storage/record_file.h"

namespace nvr {

// Result of resolving the single global end (write pointer) across all disks.
struct GlobalEnd {
  int disk_index = -1;    // logical disk holding the authoritative end
  int64_t slot = -1;      // slot in that disk's log.txt
  bool found = false;
};

// Central storage engine implementing the design-doc storage model:
//  * all channels feed one global ring buffer, stored sequentially;
//  * fill one logical disk, then continue on the next (Linux order), wrapping
//    around and overwriting the oldest data;
//  * one authoritative end marker (closest to the initial logical disk),
//    validated against neighbours and de-duplicated on startup.
class StorageEngine {
 public:
  explicit StorageEngine(DiskLayout layout = {});
  ~StorageEngine();

  // Mounts all logical disks under |root|, formatting any that are not yet
  // formatted, then recovers the global write pointer.
  bool Init(const std::string& root);

  // Appends a frame to the current write position (先来先存). For key frames an
  // index record is written to the active disk's log.txt with the rolling end
  // marker. |type| classifies the recording (A/M/P/T).
  bool WriteFrame(const Frame& frame, RecordType type);

  // Recovered / current global write pointer.
  const GlobalEnd& global_end() const { return global_end_; }

  int disk_count() const { return static_cast<int>(disks_.size()); }

  // Flushes all buffered writes.
  void Flush();

 private:
  struct DiskCtx {
    LogicalDisk info;
    std::unique_ptr<LogIndex> log;
    std::unique_ptr<RecordFile> data;  // currently active data file
    uint64_t active_file = 1;          // A0001.. index of active data file
  };

  // Startup recovery: find candidate ends on every disk, pick the one closest to
  // the initial logical disk, validate neighbouring slots (take the last), then
  // clear every other end flag so exactly one remains.
  bool RecoverGlobalEnd();

  bool OpenActiveDataFile(DiskCtx* disk);
  void AdvanceToNextFile();

  uint8_t NextIframeIndex(int channel, uint64_t sec);

  DiskLayout layout_;
  std::string root_;
  std::vector<DiskCtx> disks_;
  int active_disk_ = 0;
  GlobalEnd global_end_;

  // Per-(channel,second) I-frame counter for the log's index field.
  std::map<uint64_t, uint8_t> iframe_counter_;
  uint64_t iframe_counter_sec_ = 0;

  std::mutex mu_;
};

}  // namespace nvr
