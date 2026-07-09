#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nvr {

// Standard base64 (RFC 4648) encode/decode. Used for ONVIF WS-Security
// UsernameToken (Nonce + PasswordDigest).
std::string Base64Encode(const uint8_t* data, size_t len);
std::string Base64Encode(const std::string& in);
std::vector<uint8_t> Base64Decode(const std::string& in);

}  // namespace nvr
