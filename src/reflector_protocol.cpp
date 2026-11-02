#include "reflector_protocol.h"

#include <cstring>

#include "dplus_protocol.h"

namespace reflector_wire {
namespace {
constexpr uint8_t silence[12] = {0x9e, 0x8d, 0x32, 0x88, 0x26, 0x1a,
                                 0x3f, 0x61, 0xe8, 0x55, 0x55, 0x55};
bool stationMatches(const uint8_t* p, const char* station) {
  return std::memcmp(p, station, 7) == 0 && p[8] == station[7];
}
}  // namespace

bool supportedName(const char* name) {
  if (!name || std::strlen(name) != 6) return false;
  if (std::strncmp(name, "REF", 3) && std::strncmp(name, "XRF", 3) &&
      std::strncmp(name, "DCS", 3) && std::strncmp(name, "XLX", 3))
    return false;
  for (unsigned i = 3; i < 6; ++i)
    if (!((name[i] >= '0' && name[i] <= '9') ||
          (name[i] >= 'A' && name[i] <= 'Z')))
      return false;
  return true;
}
Protocol protocolFor(const char* name) {
  if (std::strncmp(name, "XRF", 3) == 0) return Protocol::DExtra;
  if (std::strncmp(name, "DCS", 3) == 0 || std::strncmp(name, "XLX", 3) == 0)
    return Protocol::DCS;
  return Protocol::DPlus;
}
uint16_t port(Protocol p) {
  return p == Protocol::DPlus ? 20001 : p == Protocol::DExtra ? 30001 : 30051;
}
size_t connect(uint8_t* out, size_t cap, Protocol p, const char* station,
               const char* target, bool link) {
  const size_t size = p == Protocol::DPlus    ? 5
                      : p == Protocol::DExtra ? 11
                      : link                  ? 519
                                              : 19;
  if (!out || cap < size) return 0;
  std::memset(out, 0, size);
  if (p == Protocol::DPlus) {
    out[0] = 5;
    out[2] = 0x18;
    out[4] = link;
    return size;
  }
  std::memcpy(out, station, 7);
  out[7] = ' ';
  out[8] = station[7];
  out[9] = link ? target[7] : ' ';
  if (p == Protocol::DCS) {
    std::memcpy(out + 11, target, 7);
    out[18] = ' ';
    constexpr char description[] = "<div>ReflectorMic</div>";
    if (link) std::memcpy(out + 19, description, sizeof(description) - 1);
  }
  return size;
}
size_t poll(uint8_t* out, size_t cap, Protocol p, const char* station,
            const char* target) {
  const size_t size = p == Protocol::DPlus ? 3 : p == Protocol::DExtra ? 9 : 17;
  if (!out || cap < size) return 0;
  std::memset(out, 0, size);
  if (p == Protocol::DPlus) {
    out[0] = 3;
    out[1] = 0x60;
  } else {
    std::memcpy(out, station, 8);
    if (p == Protocol::DCS)
      std::memcpy(out + 9, target, 8);
    else
      out[7] =
          ' ';  // DExtra polls identify the base callsign, without its module.
  }
  return size;
}
Control control(const uint8_t* p, size_t n, Protocol protocol,
                const char* station, const char* target) {
  if (!p) return Control::Unknown;
  if (protocol == Protocol::DPlus)
    return n == 3 && p[0] == 3 && p[1] == 0x60 && p[2] == 0 ? Control::Poll
                                                            : Control::Unknown;
  if (n == 14 && stationMatches(p, station)) {
    if (p[9] == target[7] && !std::memcmp(p + 10, "ACK", 3))
      return Control::Ack;
    if ((p[9] == target[7] || p[9] == ' ') && !std::memcmp(p + 10, "NAK", 3))
      return Control::Nak;
  }
  if ((n == 11 || (protocol == Protocol::DCS && n == 19)) &&
      stationMatches(p, station) && p[9] == ' ' && p[10] == 0)
    return Control::Unlink;
  if (protocol == Protocol::DExtra && n == 9 && !std::memcmp(p, target, 6))
    return Control::Poll;
  if (protocol == Protocol::DCS) {
    // XLX/native DCS sends 22-byte server polls; some versions send only its
    // callsign.
    if ((n == 8 || n == 9) && !std::memcmp(p, target, 6)) return Control::Poll;
    if (n == 22 && !std::memcmp(p, target, 8) &&
        !std::memcmp(p + 9, station, 7) && p[17] == station[7])
      return Control::Poll;
  }
  return Control::Unknown;
}
size_t header(uint8_t* out, size_t cap, Protocol p, const uint8_t* rf,
              uint16_t stream) {
  if (p == Protocol::DPlus)
    return dplus::buildHeaderPacket(out, cap, rf, stream);
  if (p == Protocol::DCS || !out || cap < 56)
    return 0;  // DCS repeats the header in every voice datagram.
  uint8_t packet[58];
  if (!dplus::buildHeaderPacket(packet, sizeof(packet), rf, stream)) return 0;
  std::memcpy(out, packet + 2, 56);
  return 56;
}
size_t voice(uint8_t* out, size_t cap, Protocol p, const uint8_t* rf,
             const uint8_t* frame, uint16_t stream, uint8_t sequence,
             uint32_t counter, bool end) {
  if (p == Protocol::DPlus)
    return dplus::buildFramePacket(out, cap, frame, sequence, stream, end);
  if (!out || !stream || sequence >= 21 || (!end && !frame)) return 0;
  if (p == Protocol::DExtra) {
    if (cap < 27) return 0;
    uint8_t packet[32];
    dplus::buildFramePacket(packet, sizeof(packet), end ? silence : frame,
                            sequence, stream, false);
    std::memcpy(out, packet + 2, 27);
    if (end) out[14] |= 0x40;
    return 27;
  }
  if (cap < 100 || !rf) return 0;
  std::memset(out, 0, 100);
  std::memcpy(out, "0001", 4);
  std::memcpy(out + 4, rf, 39);  // DCS omits the RF header CRC.
  out[43] = stream & 255;
  out[44] = stream >> 8;
  out[45] = sequence | (end ? 0x40 : 0);
  std::memcpy(out + 46, end ? silence : frame, 12);
  out[58] = counter & 255;
  out[59] = (counter >> 8) & 255;
  out[60] = (counter >> 16) & 255;
  out[61] = 1;
  out[63] = 0x21;
  std::memset(out + 64, ' ', 20);
  return 100;
}
bool readMedia(const uint8_t* p, size_t n, Protocol protocol, Media& out) {
  out = Media();
  if (!p) return false;
  if (protocol == Protocol::DCS) {
    if (n != 100 || std::memcmp(p, "0001", 4)) return false;
    out.stream = p[43] | (uint16_t(p[44]) << 8);
    out.sequence = p[45] & 0x1f;
    if (!out.stream || out.sequence >= 21 || (p[45] & 0xa0)) return false;
    out.hasHeader = out.hasVoice = true;
    out.end = p[45] & 0x40;
    std::memcpy(out.rfHeader, p + 4, 39);
    std::memcpy(out.frame, p + 46, 12);
    return true;
  }
  if (protocol != Protocol::DExtra || (n != 56 && n != 27) ||
      std::memcmp(p, "DSVT", 4) || p[8] != 0x20)
    return false;
  out.stream = p[12] | (uint16_t(p[13]) << 8);
  if (!out.stream) return false;
  if (n == 56) {
    if (p[4] != 0x10 || p[14] != 0x80) return false;
    out.hasHeader = true;
    std::memcpy(out.rfHeader, p + 15, 41);
    return true;
  }
  out.sequence = p[14] & 0x1f;
  if (p[4] != 0x20 || out.sequence >= 21 || (p[14] & 0xa0)) return false;
  out.hasVoice = true;
  out.end = p[14] & 0x40;
  std::memcpy(out.frame, p + 15, 12);
  return true;
}
}  // namespace reflector_wire
