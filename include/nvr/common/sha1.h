#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace nvr {

// SHA-1 (RFC 3174). Used for ONVIF WS-Security PasswordDigest:
//   Digest = Base64( SHA1( Nonce + Created + Password ) )
std::array<uint8_t, 20> Sha1(const uint8_t* data, size_t len);
std::array<uint8_t, 20> Sha1(const std::string& in);

}  // namespace nvr
