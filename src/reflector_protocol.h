#pragma once
#include <cstddef>
#include <cstdint>

// Stateless wire formats; no sockets, allocation, codec work or timing here.
namespace reflector_wire {
enum class Protocol : uint8_t { DPlus, DExtra, DCS };
enum class Control : uint8_t { Unknown, Poll, Ack, Nak, Unlink };
bool supportedName(const char* name);
Protocol protocolFor(const char* name);  // XLX exposes a DCS-compatible port.
uint16_t port(Protocol protocol);
size_t connect(uint8_t* out, size_t capacity, Protocol protocol,
               const char* station8, const char* target8, bool link);
size_t poll(uint8_t* out, size_t capacity, Protocol protocol,
            const char* station8, const char* target8);
Control control(const uint8_t* packet, size_t length, Protocol protocol,
                const char* station8, const char* target8);
size_t header(uint8_t* out, size_t capacity, Protocol protocol,
              const uint8_t* rfHeader, uint16_t stream);
size_t voice(uint8_t* out, size_t capacity, Protocol protocol,
             const uint8_t* rfHeader, const uint8_t* frame12, uint16_t stream,
             uint8_t sequence, uint32_t counter, bool end);
struct Media {
  bool hasHeader = false, hasVoice = false, end = false;
  uint16_t stream = 0;
  uint8_t sequence = 0;
  uint8_t rfHeader[41]{};
  uint8_t frame[12]{};
};
bool readMedia(const uint8_t* packet, size_t length, Protocol protocol,
               Media& out);
}  // namespace reflector_wire
