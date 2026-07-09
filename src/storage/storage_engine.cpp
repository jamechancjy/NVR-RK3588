#include "nvr/storage/storage_engine.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "nvr/common/logging.h"

namespace fs = std::filesystem;

namespace nvr {

namespace {
uint64_t ParseFileIndex(const std::string& name) {
  // "A0003" -> 3
  if (name.size() < 2 || name[0] != 'A') return 1;
  return std::strtoull(name.c_str() + 1, nullptr, 10);
}
}  // namespace

StorageEngine::StorageEngine(DiskLayout layout) : layout_(layout) {}

StorageEngine::~StorageEngine() { Flush(); }

bool StorageEngine::Init(const std::string& root) {
  std::lock_guard<std::mutex> lock(mu_);
  root_ = root;
  DiskManager dm(layout_);

  auto found = dm.Discover(root);
  if (found.empty()) {
    NVR_LOGE("no logical disks under %s", root.c_str());
    return false;
  }

  for (auto& d : found) {
    if (!d.formatted) {
      NVR_LOGI("logical disk %s not formatted -> formatting", d.mount.c_str());
      if (!dm.Format(d.mount)) return false;
      d.formatted = true;
    }
    DiskCtx ctx;
    ctx.info = d;
    ctx.log = std::make_unique<LogIndex>();
    if (!ctx.log->Open((fs::path(d.mount) / "log.txt").string(),
                       dm.LogCapacity())) {
      NVR_LOGE("cannot open log.txt on %s", d.mount.c_str());
      return false;
    }
    ctx.data = std::make_unique<RecordFile>();
    ctx.active_file = 1;
    disks_.push_back(std::move(ctx));
  }

  if (!RecoverGlobalEnd()) return false;

  // Open the active data file for the recovered position.
  if (!OpenActiveDataFile(&disks_[active_disk_])) return false;

  NVR_LOGI("storage init: %zu disks, active disk %d file A%04llu offset %llu",
           disks_.size(), active_disk_,
           static_cast<unsigned long long>(disks_[active_disk_].active_file),
           static_cast<unsigned long long>(
               disks_[active_disk_].data->write_offset()));
  return true;
}

bool StorageEngine::RecoverGlobalEnd() {
  // Find the disk closest to the initial logical disk (lowest index) that holds
  // an end marker. Foreign disks (from another NVR) may carry stale ends; the
  // authoritative pointer is the one nearest the initial disk.
  int auth_disk = -1;
  int64_t auth_slot = -1;
  for (int i = 0; i < static_cast<int>(disks_.size()); ++i) {
    int64_t slot = disks_[i].log->FindEndSlot();
    if (slot >= 0) {
      auth_disk = i;
      auth_slot = slot;
      break;
    }
  }

  if (auth_disk < 0) {
    // Fresh system: start at disk 0, slot 0.
    active_disk_ = 0;
    disks_[0].active_file = 1;
    disks_[0].log->SetCursorAfter(-1);
    global_end_ = {0, -1, false};
    NVR_LOGI("no end marker found; starting fresh");
    return true;
  }

  // Validate neighbours: an interrupted rolling update may leave the end on an
  // adjacent slot. Take the last (highest circular) end among {slot-1,slot,+1}.
  LogIndex* log = disks_[auth_disk].log.get();
  uint64_t cap = log->capacity();
  int64_t best = auth_slot;
  for (int d = -1; d <= 1; ++d) {
    int64_t s = (auth_slot + d + cap) % cap;
    LogRecord r;
    if (log->ReadSlot(s, &r) && r.end) {
      // Prefer the slot whose successor is blank (the true frontier).
      LogRecord nxt;
      if (log->ReadSlot((s + 1) % cap, &nxt) && !nxt.valid) best = s;
    }
  }
  auth_slot = best;

  // Clear every other end flag so exactly one remains globally.
  for (int i = 0; i < static_cast<int>(disks_.size()); ++i) {
    if (i == auth_disk) continue;
    disks_[i].log->ClearAllEndFlags();
  }
  // Within the authoritative disk clear stray ends except auth_slot.
  for (uint64_t s = 0; s < cap; ++s) {
    if (static_cast<int64_t>(s) == auth_slot) continue;
    LogRecord r;
    if (log->ReadSlot(s, &r) && r.end) {
      r.end = false;
      log->WriteSlot(s, r);
    }
  }

  LogRecord endr;
  log->ReadSlot(auth_slot, &endr);
  active_disk_ = auth_disk;
  disks_[auth_disk].active_file = ParseFileIndex(endr.file);
  log->SetCursorAfter(auth_slot);
  global_end_ = {auth_disk, auth_slot, true};
  NVR_LOGI("recovered global end: disk %d slot %lld file %s",
           auth_disk, static_cast<long long>(auth_slot), endr.file.c_str());
  return true;
}

bool StorageEngine::OpenActiveDataFile(DiskCtx* disk) {
  std::string name = DiskManager::DataFileName(disk->active_file);
  std::string path = (fs::path(disk->info.mount) / name).string();
  if (!disk->data->Open(path, layout_.data_file_size)) {
    NVR_LOGE("cannot open data file %s", path.c_str());
    return false;
  }
  // Resume appending after the last intact frame (0 for a freshly reused file).
  uint64_t start = 0;
  if (global_end_.found && global_end_.disk_index == disk->info.index) {
    LogRecord endr;
    if (disk->log->ReadSlot(global_end_.slot, &endr)) start = endr.offset;
  }
  disk->data->SetWriteOffset(disk->data->ScanAppendOffset(start));
  return true;
}

void StorageEngine::AdvanceToNextFile() {
  DiskCtx& cur = disks_[active_disk_];
  cur.data->Flush();

  if (cur.active_file < layout_.data_files_per_disk) {
    cur.active_file++;
  } else {
    // Move to the next logical disk, wrapping to the first (circular overwrite).
    active_disk_ = (active_disk_ + 1) % static_cast<int>(disks_.size());
    disks_[active_disk_].active_file = 1;
  }
  DiskCtx& next = disks_[active_disk_];
  std::string path =
      (fs::path(next.info.mount) / DiskManager::DataFileName(next.active_file))
          .string();
  next.data->Open(path, layout_.data_file_size);
  next.data->ResetWriteOffset();  // overwrite old content (files 定长, 内容可删)
}

uint8_t StorageEngine::NextIframeIndex(int channel, uint64_t sec) {
  if (sec != iframe_counter_sec_) {
    iframe_counter_.clear();
    iframe_counter_sec_ = sec;
  }
  uint8_t& n = iframe_counter_[static_cast<uint64_t>(channel)];
  if (n < 255) n++;
  return n;
}

bool StorageEngine::WriteFrame(const Frame& frame, RecordType type) {
  std::lock_guard<std::mutex> lock(mu_);
  if (disks_.empty()) return false;

  DiskCtx* disk = &disks_[active_disk_];
  if (!disk->data->is_open()) {
    if (!OpenActiveDataFile(disk)) return false;
  }
  if (disk->data->WouldOverflow(frame)) {
    AdvanceToNextFile();
    disk = &disks_[active_disk_];
  }

  uint64_t offset = 0;
  if (!disk->data->Append(frame, &offset)) return false;

  // Only key frames get an index record + the rolling end marker.
  if (frame.is_key() && frame.media_type == MediaType::kVideo) {
    uint64_t sec = frame.timestamp_ms / 1000;
    LogRecord rec;
    rec.file = DiskManager::DataFileName(disk->active_file);
    rec.channel = static_cast<uint16_t>(frame.channel);
    rec.time_sec = sec;
    rec.iframe_idx = NextIframeIndex(frame.channel, sec);
    rec.type = type;
    rec.offset = offset;

    // Maintain a single global end: clear it on the previous disk if we moved.
    if (global_end_.found && global_end_.disk_index != active_disk_) {
      LogRecord prev;
      LogIndex* plog = disks_[global_end_.disk_index].log.get();
      if (plog->ReadSlot(global_end_.slot, &prev) && prev.end) {
        prev.end = false;
        plog->WriteSlot(global_end_.slot, prev);
      }
    }

    uint64_t slot = 0;
    if (!disk->log->Append(rec, &slot)) return false;
    global_end_ = {active_disk_, static_cast<int64_t>(slot), true};
  }
  return true;
}

void StorageEngine::Flush() {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto& d : disks_)
    if (d.data && d.data->is_open()) d.data->Flush();
}

}  // namespace nvr
