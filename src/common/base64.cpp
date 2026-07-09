#include "nvr/common/base64.h"

namespace nvr {

namespace {
constexpr char kEnc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int DecVal(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
}  // namespace

std::string Base64Encode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(kEnc[(n >> 18) & 63]);
    out.push_back(kEnc[(n >> 12) & 63]);
    out.push_back(kEnc[(n >> 6) & 63]);
    out.push_back(kEnc[n & 63]);
    i += 3;
  }
  if (len - i == 1) {
    uint32_t n = data[i] << 16;
    out.push_back(kEnc[(n >> 18) & 63]);
    out.push_back(kEnc[(n >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
  } else if (len - i == 2) {
    uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
    out.push_back(kEnc[(n >> 18) & 63]);
    out.push_back(kEnc[(n >> 12) & 63]);
    out.push_back(kEnc[(n >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

std::string Base64Encode(const std::string& in) {
  return Base64Encode(reinterpret_cast<const uint8_t*>(in.data()), in.size());
}

std::vector<uint8_t> Base64Decode(const std::string& in) {
  std::vector<uint8_t> out;
  int val = 0, bits = -8;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int d = DecVal(c);
    if (d < 0) continue;
    val = (val << 6) | d;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

}  // namespace nvr
