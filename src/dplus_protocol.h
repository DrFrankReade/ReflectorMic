#pragma once

#include <cstddef>
#include <cstdint>

namespace dplus {

constexpr size_t RF_HEADER_SIZE = 41;
constexpr size_t DV_FRAME_SIZE = 12;
constexpr size_t HEADER_PACKET_SIZE = 58;
constexpr size_t VOICE_PACKET_SIZE = 29;
constexpr size_t END_PACKET_SIZE = 32;

size_t buildHeaderPacket(uint8_t *packet, size_t capacity,
                         const uint8_t *rfHeader, uint16_t streamId);
size_t buildFramePacket(uint8_t *packet, size_t capacity,
                        const uint8_t *frame, uint8_t sequence,
                        uint16_t streamId, bool endFrame);
bool readHeader(const uint8_t *packet, size_t length,
                uint8_t *rfHeader, uint16_t &streamId);
bool readFrame(const uint8_t *packet, size_t length,
               uint8_t *frame, uint16_t &streamId,
               uint8_t &sequence, bool &endFrame);
uint8_t nextSequence(uint8_t sequence);
uint8_t sequenceDistance(uint8_t from, uint8_t to);

}  // namespace dplus

