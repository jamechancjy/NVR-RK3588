#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nvr {

// Describes one logical disk (a ~900GB partition in production). For host builds
// and tests a logical disk is simply a directory.
struct LogicalDisk {
  int index = 0;          // ordering position (Linux enumeration order)
  std::string mount;      // mount point / directory of the logical disk
  bool formatted = false; // has NVR layout (log.txt + A0001.. present)
  uint64_t data_files = 0;// number of pre-allocated data files present
};

struct DiskLayout {
  uint64_t data_file_size = 1ull << 30;   // 1 GB per data file
  uint64_t data_files_per_disk = 900;     // ~900 files => ~900GB logical disk
  uint64_t log_capacity = 0;              // records in log.txt (0 => derive)
};

// Enumerates logical disks in stable Linux order and knows how to detect /
// (quick-)format the NVR on-disk layout.
class DiskManager {
 public:
  explicit DiskManager(DiskLayout layout = {}) : layout_(layout) {}

  // Discovers logical disks under |root| (each immediate sub-directory is a
  // logical disk). On a device this would enumerate mounted partitions instead.
  // The returned list is ordered, matching Linux device ordering.
  std::vector<LogicalDisk> Discover(const std::string& root);

  // True if |mount| already contains a valid NVR layout.
  bool IsFormatted(const std::string& mount) const;

  // Formats a logical disk: creates log.txt (fixed size) and pre-allocates the
  // A0001..A0NNN data files via a dedicated writer.
  bool Format(const std::string& mount);

  const DiskLayout& layout() const { return layout_; }
  uint64_t LogCapacity() const;

  static std::string DataFileName(uint64_t index);  // index 1 => "A0001"

 private:
  DiskLayout layout_;
};

}  // namespace nvr
