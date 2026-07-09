#include "nvr/common/sha1.h"

#include <cstring>

namespace nvr {

namespace {
inline uint32_t Rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }
}  // namespace

std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t len) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

  // Pad message: append 0x80, then zeros, then 64-bit big-endian bit length.
  uint64_t bitlen = static_cast<uint64_t>(len) * 8;
  size_t total = len + 1;
  while (total % 64 != 56) ++total;
  total += 8;

  std::string msg;
  msg.resize(total, '\0');
  std::memcpy(&msg[0], data, len);
  msg[len] = static_cast<char>(0x80);
  for (int i = 0; i < 8; ++i)
    msg[total - 1 - i] = static_cast<char>((bitlen >> (8 * i)) & 0xFF);

  for (size_t chunk = 0; chunk < total; chunk += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(&msg[chunk + i * 4]);
      w[i] = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    }
    for (int i = 16; i < 80; ++i)
      w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t tmp = Rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = Rol(b, 30);
      b = a;
      a = tmp;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }

  std::array<uint8_t, 20> out;
  for (int i = 0; i < 5; ++i) {
    out[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
    out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
    out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
    out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
  }
  return out;
}

std::array<uint8_t, 20> Sha1(const std::string& in) {
  return Sha1(reinterpret_cast<const uint8_t*>(in.data()), in.size());
}

}  // namespace nvr
