#include "dplus_protocol.h"

#include <cstring>

namespace dplus {
namespace {

constexpr uint8_t END_SEQUENCE_FLAG = 0x40;
constexpr uint8_t NULL_AMBE_FRAME[9] = {
    0x9e, 0x8d, 0x32, 0x88, 0x26, 0x1a, 0x3f, 0x61, 0xe8};
constexpr uint8_t END_PATTERN[6] = {0x55, 0x55, 0x55, 0x55, 0xc8, 0x7a};

bool hasDsvtSignature(const uint8_t *packet) {
  return memcmp(packet + 2, "DSVT", 4) == 0;
}

void initializePacket(uint8_t *packet, size_t capacity, uint8_t lengthByte,
                      uint8_t type, uint16_t streamId) {
  memset(packet, 0, capacity);
  packet[0] = lengthByte;
  packet[1] = 0x80;
  memcpy(packet + 2, "DSVT", 4);
  packet[6] = type;
  packet[10] = 0x20;
  packet[11] = 0x00;
  packet[12] = 0x02;
  packet[13] = 0x01;
  packet[14] = static_cast<uint8_t>(streamId & 0xff);
  packet[15] = static_cast<uint8_t>(streamId >> 8);
}

}  // namespace

size_t buildHeaderPacket(uint8_t *packet, size_t capacity,
                         const uint8_t *rfHeader, uint16_t streamId) {
  if (!packet || !rfHeader || capacity < HEADER_PACKET_SIZE) return 0;
  initializePacket(packet, HEADER_PACKET_SIZE, 0x3a, 0x10, streamId);
  packet[16] = 0x80;
  memcpy(packet + 17, rfHeader, RF_HEADER_SIZE);
  return HEADER_PACKET_SIZE;
}

size_t buildFramePacket(uint8_t *packet, size_t capacity,
                        const uint8_t *frame, uint8_t sequence,
                        uint16_t streamId, bool endFrame) {
  const size_t required = endFrame ? END_PACKET_SIZE : VOICE_PACKET_SIZE;
  if (!packet || capacity < required || (!endFrame && !frame)) return 0;
  initializePacket(packet, required, endFrame ? 0x20 : 0x1d, 0x20, streamId);
  packet[16] = static_cast<uint8_t>((sequence % 21) |
                                    (endFrame ? END_SEQUENCE_FLAG : 0));
  if (endFrame) {
    memcpy(packet + 17, NULL_AMBE_FRAME, sizeof(NULL_AMBE_FRAME));
    memcpy(packet + 26, END_PATTERN, sizeof(END_PATTERN));
  } else {
    // MBELIB-neo's LSB-first frame bytes already match the D-Plus payload.
    memcpy(packet + 17, frame, DV_FRAME_SIZE);
  }
  return required;
}

bool readHeader(const uint8_t *packet, size_t length,
                uint8_t *rfHeader, uint16_t &streamId) {
  if (!packet || !rfHeader || length < HEADER_PACKET_SIZE) return false;
  if (packet[0] != 0x3a || packet[1] != 0x80 || !hasDsvtSignature(packet)) return false;
  streamId = static_cast<uint16_t>(packet[14]) |
             (static_cast<uint16_t>(packet[15]) << 8);
  if (streamId == 0) return false;
  memcpy(rfHeader, packet + 17, RF_HEADER_SIZE);
  return true;
}

bool readFrame(const uint8_t *packet, size_t length,
               uint8_t *frame, uint16_t &streamId,
               uint8_t &sequence, bool &endFrame) {
  if (!packet || length < VOICE_PACKET_SIZE) return false;
  if ((packet[0] != 0x1d && packet[0] != 0x20) || packet[1] != 0x80 ||
      !hasDsvtSignature(packet)) return false;
  streamId = static_cast<uint16_t>(packet[14]) |
             (static_cast<uint16_t>(packet[15]) << 8);
  sequence = packet[16] & 0x1f;
  if (streamId == 0 || sequence >= 21) return false;
  endFrame = packet[0] == 0x20 || (packet[16] & END_SEQUENCE_FLAG) != 0;
  if (!endFrame) {
    if (!frame) return false;
    memcpy(frame, packet + 17, DV_FRAME_SIZE);
  }
  return true;
}

uint8_t nextSequence(uint8_t sequence) {
  return sequence >= 20 ? 0 : sequence + 1;
}

uint8_t sequenceDistance(uint8_t from, uint8_t to) {
  return static_cast<uint8_t>((to + 21U - from) % 21U);
}

}  // namespace dplus

