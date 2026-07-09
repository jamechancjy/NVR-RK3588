#include "nvr/media/sip.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace nvr {

namespace {
std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}
std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
}  // namespace

std::string SipMessage::Get(const std::string& name) const {
  std::string ln = Lower(name);
  for (const auto& h : headers)
    if (Lower(h.first) == ln) return h.second;
  return "";
}

void SipMessage::Set(const std::string& name, const std::string& value) {
  std::string ln = Lower(name);
  for (auto& h : headers) {
    if (Lower(h.first) == ln) {
      h.second = value;
      return;
    }
  }
  headers.emplace_back(name, value);
}

void SipMessage::Add(const std::string& name, const std::string& value) {
  headers.emplace_back(name, value);
}

std::string SipMessage::Serialize() const {
  std::ostringstream os;
  if (is_request)
    os << method << " " << uri << " SIP/2.0\r\n";
  else
    os << "SIP/2.0 " << status_code << " " << reason << "\r\n";
  for (const auto& h : headers) os << h.first << ": " << h.second << "\r\n";
  os << "Content-Length: " << body.size() << "\r\n\r\n";
  os << body;
  return os.str();
}

SipMessage SipMessage::Parse(const std::string& raw) {
  SipMessage m;
  size_t hdr_end = raw.find("\r\n\r\n");
  std::string head = hdr_end == std::string::npos ? raw : raw.substr(0, hdr_end);
  m.body = hdr_end == std::string::npos ? "" : raw.substr(hdr_end + 4);

  std::istringstream is(head);
  std::string line;
  std::getline(is, line);
  if (!line.empty() && line.back() == '\r') line.pop_back();

  if (line.compare(0, 4, "SIP/") == 0) {
    m.is_request = false;
    size_t sp1 = line.find(' ');
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp1 != std::string::npos && sp2 != std::string::npos) {
      m.status_code = std::atoi(line.substr(sp1 + 1, sp2 - sp1 - 1).c_str());
      m.reason = line.substr(sp2 + 1);
    }
  } else {
    m.is_request = true;
    size_t sp1 = line.find(' ');
    size_t sp2 = line.rfind(' ');
    if (sp1 != std::string::npos && sp2 != std::string::npos && sp2 > sp1) {
      m.method = line.substr(0, sp1);
      m.uri = line.substr(sp1 + 1, sp2 - sp1 - 1);
    }
  }

  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;
    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = Trim(line.substr(0, colon));
    std::string value = Trim(line.substr(colon + 1));
    m.headers.emplace_back(name, value);
  }
  return m;
}

std::string SipParam(const std::string& header_value, const std::string& key) {
  std::string needle = key + "=";
  size_t p = header_value.find(needle);
  if (p == std::string::npos) return "";
  p += needle.size();
  size_t e = header_value.find_first_of(";> \t\r\n", p);
  return header_value.substr(p, e == std::string::npos ? std::string::npos
                                                       : e - p);
}

}  // namespace nvr
