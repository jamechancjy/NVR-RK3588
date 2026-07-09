#include "nvr/common/crc32.h"

namespace nvr {

namespace {
struct Crc32Table {
  uint32_t t[256];
  Crc32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      t[i] = c;
    }
  }
};
const Crc32Table kTable;
}  // namespace

uint32_t Crc32(const void* data, size_t len) {
  const auto* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i)
    crc = kTable.t[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace nvr
