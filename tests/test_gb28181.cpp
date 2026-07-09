// Unit tests for GB28181 building blocks: SIP message codec + PS depacketiser.

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "nvr/media/ps_depacketizer.h"
#include "nvr/media/sip.h"

using namespace nvr;

static int g_checks = 0;
#define CHECK(cond)                                              \
  do {                                                           \
    ++g_checks;                                                  \
    if (!(cond)) {                                               \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, \
                   __FILE__, __LINE__);                          \
      return 1;                                                  \
    }                                                            \
  } while (0)

static int TestSip() {
  std::string raw =
      "INVITE sip:34020000001320000001@10.0.0.9:5060 SIP/2.0\r\n"
      "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bKabc123\r\n"
      "From: <sip:34020000002000000001@10.0.0.1>;tag=fromtag\r\n"
      "To: <sip:34020000001320000001@10.0.0.9>\r\n"
      "Call-ID: callid-42\r\n"
      "CSeq: 1 INVITE\r\n"
      "Content-Type: application/sdp\r\n"
      "Content-Length: 3\r\n"
      "\r\n"
      "v=0";
  SipMessage m = SipMessage::Parse(raw);
  CHECK(m.is_request);
  CHECK(m.method == "INVITE");
  CHECK(m.uri == "sip:34020000001320000001@10.0.0.9:5060");
  CHECK(m.Get("Call-ID") == "callid-42");
  CHECK(m.Get("call-id") == "callid-42");  // case-insensitive
  CHECK(m.body == "v=0");
  CHECK(SipParam(m.Get("Via"), "branch") == "z9hG4bKabc123");
  CHECK(SipParam(m.Get("From"), "tag") == "fromtag");

  // Response parse
  SipMessage r = SipMessage::Parse(
      "SIP/2.0 200 OK\r\nTo: <sip:x@y>;tag=remote99\r\n"
      "CSeq: 1 INVITE\r\n\r\n");
  CHECK(!r.is_request);
  CHECK(r.status_code == 200);
  CHECK(r.reason == "OK");
  CHECK(SipParam(r.Get("To"), "tag") == "remote99");

  // Build + reparse roundtrip
  SipMessage b;
  b.is_request = true;
  b.method = "BYE";
  b.uri = "sip:dev@host";
  b.Add("Call-ID", "xyz");
  b.Add("CSeq", "2 BYE");
  b.body = "";
  SipMessage b2 = SipMessage::Parse(b.Serialize());
  CHECK(b2.method == "BYE");
  CHECK(b2.Get("CSeq") == "2 BYE");
  CHECK(b2.Get("Content-Length") == "0");
  return 0;
}

// Append a PS unit (start code 00 00 01 <id> + payload) to `out`.
static void PutStart(std::vector<uint8_t>& out, uint8_t id) {
  out.push_back(0);
  out.push_back(0);
  out.push_back(1);
  out.push_back(id);
}

static void PutBytes(std::vector<uint8_t>& out,
                     std::initializer_list<uint8_t> b) {
  for (uint8_t x : b) out.push_back(x);
}

// Build a video PES with optional PTS and given ES payload.
static void PutVideoPes(std::vector<uint8_t>& out, bool pts,
                        const std::vector<uint8_t>& es) {
  PutStart(out, 0xE0);
  out.push_back(0);  // PES_packet_length hi (0 = unbounded)
  out.push_back(0);  // PES_packet_length lo
  out.push_back(0x80);              // '10' marker
  out.push_back(pts ? 0x80 : 0x00);  // PTS_DTS_flags = 10 if pts
  if (pts) {
    out.push_back(5);  // PES_header_data_length
    PutBytes(out, {0x21, 0x00, 0x01, 0x00, 0x01});  // PTS = 0
  } else {
    out.push_back(0);  // no header data
  }
  for (uint8_t x : es) out.push_back(x);
}

static int TestPsDepacketizer() {
  std::vector<uint8_t> ps;

  // Pack header (skipped) + PSM declaring E0 = H.264 (stream_type 0x1B).
  PutStart(ps, 0xBA);
  PutBytes(ps, {0x44, 0, 0, 0, 0, 0, 0, 0, 0, 0});  // 10 pack bytes
  PutStart(ps, 0xBC);                                // PSM
  PutBytes(ps, {0x00, 0x12});                        // (length; not used)
  PutBytes(ps, {0xE1, 0xFF});                        // current_next + marker
  PutBytes(ps, {0x00, 0x00});                        // program_stream_info_length=0
  PutBytes(ps, {0x00, 0x04});                        // es_map_length = 4
  PutBytes(ps, {0x1B, 0xE0, 0x00, 0x00});            // H.264 on stream E0
  PutBytes(ps, {0x00, 0x00, 0x00, 0x00});            // CRC (ignored)

  // Frame 1 (key): SPS(0x67) + IDR(0x65) with PTS.
  std::vector<uint8_t> au1 = {0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1e,
                              0, 0, 0, 1, 0x65, 0x88, 0x84, 0x00};
  PutVideoPes(ps, /*pts=*/true, au1);

  // Frame 2 (non-key): non-IDR slice(0x41). PTS present -> flushes frame 1.
  std::vector<uint8_t> au2 = {0, 0, 0, 1, 0x41, 0x9a, 0x00};
  PutVideoPes(ps, /*pts=*/true, au2);

  // ---- feed all at once ----
  std::vector<Frame> frames;
  {
    PsDepacketizer dp(3, [&](const Frame& f) { frames.push_back(f); });
    dp.Feed(ps.data(), ps.size());
    dp.Flush();
  }
  CHECK(frames.size() == 2);
  CHECK(frames[0].channel == 3);
  CHECK(frames[0].codec == Codec::kH264);
  CHECK(frames[0].frame_type == FrameType::kI);
  CHECK(frames[1].frame_type == FrameType::kP);
  CHECK(!frames[0].data.empty());

  // ---- feed one byte at a time (reassembly across chunk boundaries) ----
  std::vector<Frame> frames2;
  {
    PsDepacketizer dp(3, [&](const Frame& f) { frames2.push_back(f); });
    for (uint8_t b : ps) dp.Feed(&b, 1);
    dp.Flush();
  }
  CHECK(frames2.size() == 2);
  CHECK(frames2[0].frame_type == FrameType::kI);
  CHECK(frames2[1].frame_type == FrameType::kP);
  return 0;
}

int main() {
  struct { const char* name; int (*fn)(); } tests[] = {
      {"Sip", TestSip},
      {"PsDepacketizer", TestPsDepacketizer},
  };
  for (auto& t : tests) {
    std::printf("running %s ...\n", t.name);
    if (t.fn() != 0) {
      std::fprintf(stderr, "TEST FAILED: %s\n", t.name);
      return 1;
    }
  }
  std::printf("all gb28181 tests passed (%d checks)\n", g_checks);
  return 0;
}
