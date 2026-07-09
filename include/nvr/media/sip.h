#pragma once

#include <string>
#include <utility>
#include <vector>

namespace nvr {

// Minimal SIP message model (RFC 3261) sufficient for GB28181 live invite:
// REGISTER handling and the INVITE/100/200/ACK/BYE dialog. Header order and
// duplicates (e.g. Via, Route) are preserved.
struct SipMessage {
  bool is_request = true;

  // Request line
  std::string method;  // INVITE, ACK, BYE, REGISTER, MESSAGE ...
  std::string uri;     // request URI

  // Status line
  int status_code = 0;
  std::string reason;

  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;

  // First value of a header (case-insensitive name), or "".
  std::string Get(const std::string& name) const;
  void Set(const std::string& name, const std::string& value);
  void Add(const std::string& name, const std::string& value);

  std::string Serialize() const;
  static SipMessage Parse(const std::string& raw);
};

// Extract a tag/param value (e.g. "branch", "tag") from a header value.
std::string SipParam(const std::string& header_value, const std::string& key);

}  // namespace nvr
