#pragma once

#include <cstddef>
#include <cstdint>

namespace nvr {

// Standard CRC-32 (IEEE 802.3) over |data|.
uint32_t Crc32(const void* data, size_t len);

}  // namespace nvr
