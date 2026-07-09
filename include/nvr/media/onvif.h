#pragma once

#include <string>
#include <vector>

namespace nvr {

// A device found on the LAN via WS-Discovery.
struct OnvifDevice {
  std::string xaddr;    // device service URL, e.g. http://192.168.1.64/onvif/device_service
  std::string types;    // reported wsdd Types (e.g. "dn:NetworkVideoTransmitter")
  std::string scopes;   // raw scopes string (may contain name/hardware/location)
  std::string uuid;     // EndpointReference address (urn:uuid:...)
};

// WS-Discovery: multicast Probe to 239.255.255.250:3702 and collect responses.
// timeout_ms bounds how long we listen for ProbeMatch replies.
std::vector<OnvifDevice> OnvifProbe(int timeout_ms = 2000);

// A media profile + its RTSP stream URI, resolved from a device.
struct OnvifProfile {
  std::string token;    // profile token
  std::string name;     // profile name
  std::string rtsp_uri; // RTSP URL for this profile's stream
};

// ONVIF Media client. Talks SOAP over HTTP to a device service URL, using
// WS-Security UsernameToken (PasswordDigest) auth. Resolves media profiles and
// their RTSP stream URIs (GetProfiles + GetStreamUri).
class OnvifClient {
 public:
  OnvifClient(std::string device_service, std::string user, std::string pass);

  // Resolve all media profiles and their RTSP URIs. Returns false on transport
  // or auth failure; err (if non-null) gets a short reason.
  bool GetProfiles(std::vector<OnvifProfile>* out, std::string* err = nullptr);

  // Convenience: RTSP URI for a given profile index (0 = main, 1 = sub, ...).
  bool GetStreamUri(int profile_index, std::string* rtsp_uri,
                    std::string* err = nullptr);

 private:
  // Media service endpoint; derived from device_service unless GetCapabilities
  // is later added. For most devices the media service shares the host path.
  std::string MediaEndpoint() const;
  bool Post(const std::string& endpoint, const std::string& body,
            std::string* resp, std::string* err);

  std::string device_service_;
  std::string user_;
  std::string pass_;
};

}  // namespace nvr
