#include "nvr/media/onvif.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>
#include <sstream>

#include "nvr/common/base64.h"
#include "nvr/common/logging.h"
#include "nvr/common/sha1.h"

namespace nvr {

namespace {

// ---- tiny XML helpers (substring based; sufficient for ONVIF replies) -------

// Return the text between the first ">"-closed occurrence of any tag whose
// local name is `local` and its matching close. Handles namespace prefixes by
// matching on ":local" or the bare local name at a tag boundary.
std::string TagText(const std::string& xml, const std::string& local,
                    size_t from = 0) {
  // Find an opening tag ending in `local` (with optional ns prefix).
  size_t pos = from;
  while (pos < xml.size()) {
    size_t lt = xml.find('<', pos);
    if (lt == std::string::npos) return "";
    size_t gt = xml.find('>', lt);
    if (gt == std::string::npos) return "";
    std::string tag = xml.substr(lt + 1, gt - lt - 1);
    if (!tag.empty() && tag[0] != '/') {
      // strip attributes
      std::string name = tag.substr(0, tag.find_first_of(" \t\r\n/"));
      std::string ln = name;
      size_t colon = ln.find(':');
      if (colon != std::string::npos) ln = ln.substr(colon + 1);
      if (ln == local) {
        if (!tag.empty() && tag.back() == '/') return "";  // self-closed
        size_t close = xml.find('<', gt + 1);
        if (close == std::string::npos) return "";
        return xml.substr(gt + 1, close - gt - 1);
      }
    }
    pos = gt + 1;
  }
  return "";
}

// Extract the value of attribute `attr` from the first tag with local name.
std::string TagAttr(const std::string& xml, const std::string& local,
                    const std::string& attr, size_t from = 0) {
  size_t pos = from;
  while (pos < xml.size()) {
    size_t lt = xml.find('<', pos);
    if (lt == std::string::npos) return "";
    size_t gt = xml.find('>', lt);
    if (gt == std::string::npos) return "";
    std::string tag = xml.substr(lt + 1, gt - lt - 1);
    std::string name = tag.substr(0, tag.find_first_of(" \t\r\n/"));
    std::string ln = name;
    size_t colon = ln.find(':');
    if (colon != std::string::npos) ln = ln.substr(colon + 1);
    if (ln == local) {
      std::string key = attr + "=\"";
      size_t a = tag.find(key);
      if (a != std::string::npos) {
        a += key.size();
        size_t e = tag.find('"', a);
        if (e != std::string::npos) return tag.substr(a, e - a);
      }
    }
    pos = gt + 1;
  }
  return "";
}

std::string NowIso8601Utc() {
  time_t now = time(nullptr);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return buf;
}

std::string RandomNonceRaw() {
  std::random_device rd;
  std::string n(16, '\0');
  for (auto& c : n) c = static_cast<char>(rd() & 0xFF);
  return n;
}

std::string MakeUuid() {
  std::random_device rd;
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x", rd(),
                rd() & 0xFFFF, rd() & 0xFFFF, rd() & 0xFFFF, rd() & 0xFFFF, rd());
  return std::string("urn:uuid:") + buf;
}

// WS-Security UsernameToken (PasswordDigest) SOAP header.
std::string SecurityHeader(const std::string& user, const std::string& pass) {
  std::string nonce_raw = RandomNonceRaw();
  std::string created = NowIso8601Utc();
  std::string tohash = nonce_raw + created + pass;
  auto digest = Sha1(reinterpret_cast<const uint8_t*>(tohash.data()),
                     tohash.size());
  std::string pass_digest =
      Base64Encode(digest.data(), digest.size());
  std::string nonce_b64 = Base64Encode(
      reinterpret_cast<const uint8_t*>(nonce_raw.data()), nonce_raw.size());

  std::ostringstream os;
  os << "<s:Header><Security s:mustUnderstand=\"1\" "
        "xmlns=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
        "<UsernameToken><Username>"
     << user << "</Username>"
     << "<Password Type=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">"
     << pass_digest << "</Password>"
     << "<Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-soap-message-security-1.0#Base64Binary\">"
     << nonce_b64 << "</Nonce>"
     << "<Created xmlns=\"http://docs.oasis-open.org/wss/2004/01/"
        "oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
     << created << "</Created>"
     << "</UsernameToken></Security></s:Header>";
  return os.str();
}

struct Url {
  std::string host;
  int port = 80;
  std::string path = "/";
  bool ok = false;
};

Url ParseHttpUrl(const std::string& url) {
  Url u;
  const std::string scheme = "http://";
  if (url.compare(0, scheme.size(), scheme) != 0) return u;
  size_t start = scheme.size();
  size_t slash = url.find('/', start);
  std::string authority =
      slash == std::string::npos ? url.substr(start) : url.substr(start, slash - start);
  u.path = slash == std::string::npos ? "/" : url.substr(slash);
  size_t colon = authority.find(':');
  if (colon == std::string::npos) {
    u.host = authority;
    u.port = 80;
  } else {
    u.host = authority.substr(0, colon);
    u.port = std::atoi(authority.substr(colon + 1).c_str());
  }
  u.ok = !u.host.empty();
  return u;
}

// Blocking HTTP POST of a SOAP body. Returns the response body (after headers).
bool HttpPostSoap(const Url& u, const std::string& body, int timeout_ms,
                  std::string* resp, std::string* err) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port_str[16];
  std::snprintf(port_str, sizeof(port_str), "%d", u.port);
  struct addrinfo* res = nullptr;
  if (getaddrinfo(u.host.c_str(), port_str, &hints, &res) != 0 || !res) {
    if (err) *err = "dns failed for " + u.host;
    return false;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    freeaddrinfo(res);
    if (err) *err = "socket failed";
    return false;
  }
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    freeaddrinfo(res);
    close(fd);
    if (err) *err = "connect failed to " + u.host;
    return false;
  }
  freeaddrinfo(res);

  std::ostringstream req;
  req << "POST " << u.path << " HTTP/1.1\r\n"
      << "Host: " << u.host << ":" << u.port << "\r\n"
      << "Content-Type: application/soap+xml; charset=utf-8\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
  std::string reqs = req.str();
  size_t sent = 0;
  while (sent < reqs.size()) {
    ssize_t n = send(fd, reqs.data() + sent, reqs.size() - sent, 0);
    if (n <= 0) {
      close(fd);
      if (err) *err = "send failed";
      return false;
    }
    sent += static_cast<size_t>(n);
  }

  std::string raw;
  char buf[4096];
  for (;;) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    raw.append(buf, static_cast<size_t>(n));
  }
  close(fd);

  size_t hdr_end = raw.find("\r\n\r\n");
  if (hdr_end == std::string::npos) {
    if (err) *err = "malformed http response";
    return false;
  }
  std::string headers = raw.substr(0, hdr_end);
  std::string content = raw.substr(hdr_end + 4);

  // Handle chunked transfer-encoding minimally.
  if (headers.find("Transfer-Encoding: chunked") != std::string::npos ||
      headers.find("transfer-encoding: chunked") != std::string::npos) {
    std::string dechunked;
    size_t p = 0;
    while (p < content.size()) {
      size_t nl = content.find("\r\n", p);
      if (nl == std::string::npos) break;
      long sz = strtol(content.substr(p, nl - p).c_str(), nullptr, 16);
      if (sz <= 0) break;
      p = nl + 2;
      if (p + sz > content.size()) break;
      dechunked.append(content, p, static_cast<size_t>(sz));
      p += sz + 2;
    }
    content.swap(dechunked);
  }
  *resp = content;
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// WS-Discovery
// ---------------------------------------------------------------------------
std::vector<OnvifDevice> OnvifProbe(int timeout_ms) {
  std::vector<OnvifDevice> devices;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    NVR_LOGW("onvif probe: socket failed");
    return devices;
  }
  int ttl = 2;
  setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  std::string uuid = MakeUuid();
  std::ostringstream probe;
  probe << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
           "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
           "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
           "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
           "<e:Header><w:MessageID>"
        << uuid
        << "</w:MessageID>"
           "<w:To e:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:"
           "discovery</w:To>"
           "<w:Action e:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/"
           "2005/04/discovery/Probe</w:Action></e:Header>"
           "<e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types>"
           "</d:Probe></e:Body></e:Envelope>";
  std::string msg = probe.str();

  struct sockaddr_in mcast;
  std::memset(&mcast, 0, sizeof(mcast));
  mcast.sin_family = AF_INET;
  mcast.sin_port = htons(3702);
  mcast.sin_addr.s_addr = inet_addr("239.255.255.250");

  if (sendto(fd, msg.data(), msg.size(), 0,
             reinterpret_cast<struct sockaddr*>(&mcast), sizeof(mcast)) < 0) {
    NVR_LOGW("onvif probe: sendto failed");
    close(fd);
    return devices;
  }

  char buf[8192];
  for (;;) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) break;  // timeout ends the collection window
    std::string xml(buf, static_cast<size_t>(n));
    OnvifDevice dev;
    dev.xaddr = TagText(xml, "XAddrs");
    // XAddrs may contain multiple space-separated URLs; keep the first http one.
    if (!dev.xaddr.empty()) {
      std::istringstream is(dev.xaddr);
      std::string first;
      is >> first;
      dev.xaddr = first;
    }
    dev.types = TagText(xml, "Types");
    dev.scopes = TagText(xml, "Scopes");
    dev.uuid = TagText(xml, "Address");
    if (!dev.xaddr.empty()) {
      bool dup = false;
      for (const auto& d : devices)
        if (d.xaddr == dev.xaddr) dup = true;
      if (!dup) {
        NVR_LOGI("onvif discovered: %s", dev.xaddr.c_str());
        devices.push_back(dev);
      }
    }
  }
  close(fd);
  return devices;
}

// ---------------------------------------------------------------------------
// OnvifClient
// ---------------------------------------------------------------------------
OnvifClient::OnvifClient(std::string device_service, std::string user,
                         std::string pass)
    : device_service_(std::move(device_service)),
      user_(std::move(user)),
      pass_(std::move(pass)) {}

std::string OnvifClient::MediaEndpoint() const {
  // Most devices expose media at the same host, /onvif/media_service or
  // /onvif/Media. Prefer replacing device_service with media_service; fall back
  // to the device service (many devices accept media ops there too).
  std::string ep = device_service_;
  size_t pos = ep.find("device_service");
  if (pos != std::string::npos) {
    ep.replace(pos, std::string("device_service").size(), "media_service");
    return ep;
  }
  return device_service_;
}

bool OnvifClient::Post(const std::string& endpoint, const std::string& body,
                       std::string* resp, std::string* err) {
  Url u = ParseHttpUrl(endpoint);
  if (!u.ok) {
    if (err) *err = "bad endpoint url: " + endpoint;
    return false;
  }
  return HttpPostSoap(u, body, 5000, resp, err);
}

bool OnvifClient::GetProfiles(std::vector<OnvifProfile>* out, std::string* err) {
  out->clear();
  std::string header = SecurityHeader(user_, pass_);
  std::string body =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">" +
      header +
      "<s:Body><GetProfiles "
      "xmlns=\"http://www.onvif.org/ver10/media/wsdl\"/></s:Body></s:Envelope>";

  std::string resp;
  std::string ep = MediaEndpoint();
  if (!Post(ep, body, &resp, err)) {
    // Fall back to the device service endpoint.
    if (ep != device_service_ && Post(device_service_, body, &resp, err)) {
      // ok
    } else {
      return false;
    }
  }

  // Walk each <Profiles ...> element, collecting token + name + stream uri.
  size_t pos = 0;
  while (true) {
    size_t lt = resp.find("Profiles", pos);
    if (lt == std::string::npos) break;
    size_t tag_open = resp.rfind('<', lt);
    if (tag_open == std::string::npos) break;
    size_t tag_close = resp.find('>', lt);
    if (tag_close == std::string::npos) break;
    std::string tag = resp.substr(tag_open, tag_close - tag_open + 1);
    if (tag.find('/') == 1) {  // closing tag </...
      pos = tag_close + 1;
      continue;
    }
    OnvifProfile p;
    // token attribute lives on the Profiles element itself.
    std::string key = "token=\"";
    size_t a = tag.find(key);
    if (a != std::string::npos) {
      a += key.size();
      size_t e = tag.find('"', a);
      if (e != std::string::npos) p.token = tag.substr(a, e - a);
    }
    // Name is a child element; grab the first Name after this tag.
    p.name = TagText(resp, "Name", tag_close);
    if (!p.token.empty()) out->push_back(p);
    pos = tag_close + 1;
  }

  if (out->empty()) {
    if (err && err->empty()) *err = "no profiles parsed (auth fail?)";
    return false;
  }

  // Resolve stream URI per profile.
  for (size_t i = 0; i < out->size(); ++i) {
    std::string uri;
    std::string e2;
    if (GetStreamUri(static_cast<int>(i), &uri, &e2))
      (*out)[i].rtsp_uri = uri;
  }
  return true;
}

bool OnvifClient::GetStreamUri(int profile_index, std::string* rtsp_uri,
                               std::string* err) {
  std::vector<OnvifProfile> profiles;
  std::string token;
  // If caller already has profiles cached we would reuse them; here we re-query
  // to keep the API simple. To avoid infinite recursion, query tokens directly.
  {
    std::string header = SecurityHeader(user_, pass_);
    std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">" +
        header +
        "<s:Body><GetProfiles "
        "xmlns=\"http://www.onvif.org/ver10/media/wsdl\"/></s:Body>"
        "</s:Envelope>";
    std::string resp;
    std::string ep = MediaEndpoint();
    if (!Post(ep, body, &resp, err) &&
        !(ep != device_service_ && Post(device_service_, body, &resp, err)))
      return false;
    // collect tokens
    size_t pos = 0;
    std::vector<std::string> tokens;
    while (true) {
      size_t lt = resp.find("token=\"", pos);
      if (lt == std::string::npos) break;
      size_t a = lt + std::string("token=\"").size();
      size_t e = resp.find('"', a);
      if (e == std::string::npos) break;
      tokens.push_back(resp.substr(a, e - a));
      pos = e + 1;
    }
    if (profile_index < 0 || profile_index >= static_cast<int>(tokens.size())) {
      if (err) *err = "profile index out of range";
      return false;
    }
    token = tokens[profile_index];
  }

  std::string header = SecurityHeader(user_, pass_);
  std::ostringstream body;
  body << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
          "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
       << header
       << "<s:Body><GetStreamUri "
          "xmlns=\"http://www.onvif.org/ver10/media/wsdl\">"
          "<StreamSetup><Stream "
          "xmlns=\"http://www.onvif.org/ver10/schema\">RTP-Unicast</Stream>"
          "<Transport xmlns=\"http://www.onvif.org/ver10/schema\">"
          "<Protocol>RTSP</Protocol></Transport></StreamSetup>"
          "<ProfileToken>"
       << token << "</ProfileToken></GetStreamUri></s:Body></s:Envelope>";

  std::string resp;
  std::string ep = MediaEndpoint();
  if (!Post(ep, body.str(), &resp, err) &&
      !(ep != device_service_ && Post(device_service_, body.str(), &resp, err)))
    return false;

  std::string uri = TagText(resp, "Uri");
  if (uri.empty()) {
    if (err) *err = "no Uri in GetStreamUri response";
    return false;
  }
  *rtsp_uri = uri;
  return true;
}

}  // namespace nvr
