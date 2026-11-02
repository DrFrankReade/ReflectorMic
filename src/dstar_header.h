#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace dstar {

constexpr size_t HEADER_BYTES = 41;

uint16_t crc(const uint8_t *data, size_t length = 39);
void buildHeader(uint8_t out[HEADER_BYTES], const String &rpt2,
                 const String &rpt1, const String &yourCall,
                 const String &myCall, const String &suffix);
String readField(const uint8_t *field, size_t width);
bool headerTargets(const uint8_t header[HEADER_BYTES], const String &reflector,
                   const String &module);

}  // namespace dstar

