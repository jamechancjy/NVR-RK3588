#include "nvr/storage/disk_manager.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "nvr/common/logging.h"
#include "nvr/storage/log_index.h"
#include "nvr/storage/record_file.h"

namespace fs = std::filesystem;

namespace nvr {

std::string DiskManager::DataFileName(uint64_t index) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "A%04" "llu", static_cast<unsigned long long>(index));
  return buf;
}

uint64_t DiskManager::LogCapacity() const {
  if (layout_.log_capacity) return layout_.log_capacity;
  // Production default: a 1GB log.txt filled with fixed-length records.
  return (1ull << 30) / LogRecordSize();
}

bool DiskManager::IsFormatted(const std::string& mount) const {
  std::error_code ec;
  if (!fs::exists(fs::path(mount) / "log.txt", ec)) return false;
  if (!fs::exists(fs::path(mount) / DataFileName(1), ec)) return false;
  return true;
}

std::vector<LogicalDisk> DiskManager::Discover(const std::string& root) {
  std::vector<LogicalDisk> disks;
  std::error_code ec;
  if (!fs::exists(root, ec)) return disks;

  std::vector<std::string> mounts;
  for (const auto& entry : fs::directory_iterator(root, ec)) {
    if (entry.is_directory(ec)) mounts.push_back(entry.path().string());
  }
  // Stable ordering mirrors Linux device enumeration (sda, sdb, ...).
  std::sort(mounts.begin(), mounts.end());

  int idx = 0;
  for (const auto& m : mounts) {
    LogicalDisk d;
    d.index = idx++;
    d.mount = m;
    d.formatted = IsFormatted(m);
    if (d.formatted) {
      for (uint64_t i = 1; i <= layout_.data_files_per_disk; ++i) {
        if (fs::exists(fs::path(m) / DataFileName(i), ec))
          d.data_files++;
        else
          break;
      }
    }
    disks.push_back(std::move(d));
  }
  return disks;
}

bool DiskManager::Format(const std::string& mount) {
  std::error_code ec;
  fs::create_directories(mount, ec);

  // 1) Fixed-size log.txt.
  {
    LogIndex log;
    if (!log.Open((fs::path(mount) / "log.txt").string(), LogCapacity())) {
      NVR_LOGE("format: cannot create log.txt on %s", mount.c_str());
      return false;
    }
  }

  // 2) Pre-allocate the A0001..A0NNN data files.
  for (uint64_t i = 1; i <= layout_.data_files_per_disk; ++i) {
    RecordFile f;
    if (!f.Open((fs::path(mount) / DataFileName(i)).string(),
                layout_.data_file_size)) {
      NVR_LOGE("format: cannot pre-allocate %s on %s",
               DataFileName(i).c_str(), mount.c_str());
      return false;
    }
  }
  NVR_LOGI("formatted logical disk %s (%llu x %llu bytes)", mount.c_str(),
           static_cast<unsigned long long>(layout_.data_files_per_disk),
           static_cast<unsigned long long>(layout_.data_file_size));
  return true;
}

}  // namespace nvr
