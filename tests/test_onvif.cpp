// Unit tests for ONVIF crypto building blocks (WS-Security UsernameToken).

#include <array>
#include <cstdio>
#include <string>

#include "nvr/common/base64.h"
#include "nvr/common/sha1.h"

using namespace nvr;

static int g_checks = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond,    \
                   __FILE__, __LINE__);                             \
      return 1;                                                     \
    }                                                               \
  } while (0)

static std::string ToHex(const std::array<uint8_t, 20>& d) {
  static const char* h = "0123456789abcdef";
  std::string s;
  for (uint8_t b : d) {
    s.push_back(h[b >> 4]);
    s.push_back(h[b & 0xF]);
  }
  return s;
}

static int TestBase64() {
  CHECK(Base64Encode(std::string("")) == "");
  CHECK(Base64Encode(std::string("f")) == "Zg==");
  CHECK(Base64Encode(std::string("fo")) == "Zm8=");
  CHECK(Base64Encode(std::string("foo")) == "Zm9v");
  CHECK(Base64Encode(std::string("foob")) == "Zm9vYg==");
  CHECK(Base64Encode(std::string("fooba")) == "Zm9vYmE=");
  CHECK(Base64Encode(std::string("foobar")) == "Zm9vYmFy");
  CHECK(Base64Encode(std::string("Man")) == "TWFu");

  // Roundtrip on binary data (including embedded NUL / high bytes).
  const uint8_t raw[] = {0x00, 0x01, 0x02, 0xff, 0xfe, 0x10,
                         'H', 'e', 'l', 'l', 'o'};
  std::string s(reinterpret_cast<const char*>(raw), sizeof(raw));
  auto dec = Base64Decode(Base64Encode(s));
  CHECK(dec.size() == s.size());
  for (size_t i = 0; i < s.size(); ++i)
    CHECK(dec[i] == static_cast<uint8_t>(s[i]));
  return 0;
}

static int TestSha1() {
  CHECK(ToHex(Sha1(std::string(""))) ==
        "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  CHECK(ToHex(Sha1(std::string("abc"))) ==
        "a9993e364706816aba3e25717850c26c9cd0d89d");
  CHECK(ToHex(Sha1(std::string(
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  return 0;
}

static int TestPasswordDigest() {
  // PasswordDigest = Base64( SHA1( nonce + created + password ) ).
  // Fixed inputs -> known output (computed independently with Python hashlib).
  std::string nonce = "0123456789abcdef";  // 16 raw bytes
  std::string created = "2026-07-09T00:00:00Z";
  std::string password = "admin123";
  std::string tohash = nonce + created + password;
  auto d = Sha1(tohash);
  std::string digest = Base64Encode(d.data(), d.size());
  CHECK(digest == "mEmb4reW7d487/QdktndHd4oBAY=");
  return 0;
}

int main() {
  struct { const char* name; int (*fn)(); } tests[] = {
      {"Base64", TestBase64},
      {"Sha1", TestSha1},
      {"PasswordDigest", TestPasswordDigest},
  };
  for (auto& t : tests) {
    std::printf("running %s ...\n", t.name);
    if (t.fn() != 0) {
      std::fprintf(stderr, "TEST FAILED: %s\n", t.name);
      return 1;
    }
  }
  std::printf("all onvif tests passed (%d checks)\n", g_checks);
  return 0;
}
