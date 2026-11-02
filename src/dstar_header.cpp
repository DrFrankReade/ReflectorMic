#include "dstar_header.h"

#include <cstring>

namespace dstar {
namespace {

void writeField(uint8_t *dest, size_t width, String value) {
  value.trim();
  value.toUpperCase();
  memset(dest, ' ', width);
  memcpy(dest, value.c_str(), min(width, value.length()));
}

}  // namespace

uint16_t crc(const uint8_t *data, size_t length) {
  uint16_t value = 0xffff;
  for (size_t j = 0; j < length; ++j) {
    uint8_t current = data[j];
    for (uint8_t i = 0; i < 8; ++i) {
      const bool differs = ((value ^ current) & 1U) != 0;
      value >>= 1;
      if (differs) value ^= 0x8408;
      current >>= 1;
    }
  }
  return static_cast<uint16_t>(~value);
}

void buildHeader(uint8_t out[HEADER_BYTES], const String &rpt2,
                 const String &rpt1, const String &yourCall,
                 const String &myCall, const String &suffix) {
  memset(out, 0, HEADER_BYTES);
  writeField(out + 3, 8, rpt2);
  writeField(out + 11, 8, rpt1);
  writeField(out + 19, 8, yourCall);
  writeField(out + 27, 8, myCall);
  writeField(out + 35, 4, suffix);
  const uint16_t checksum = crc(out);
  out[39] = static_cast<uint8_t>(checksum & 0xff);
  out[40] = static_cast<uint8_t>(checksum >> 8);
}

String readField(const uint8_t *field, size_t width) {
  String value;
  value.reserve(width);
  for (size_t i = 0; i < width; ++i) {
    const uint8_t c = field[i];
    value += (c >= 0x20 && c <= 0x7e) ? static_cast<char>(c) : ' ';
  }
  value.trim();
  return value;
}

bool headerTargets(const uint8_t header[HEADER_BYTES], const String &reflector,
                   const String &module) {
  String target = reflector;
  target.trim();
  target.toUpperCase();
  while (target.length() < 6) target += ' ';
  target = target.substring(0, 6) + " " + module.substring(0, 1);
  return readField(header + 3, 8) == target || readField(header + 11, 8) == target;
}

}  // namespace dstar

