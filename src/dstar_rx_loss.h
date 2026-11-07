#pragma once
#include <cstdint>

namespace dstar {
// Locally generated AMBE-2400 lost-frame indication (b0=125, other parameter
// bits zero), with valid FEC/interleaving. Feed only to the receive decoder:
// its existing erasure path repeats short losses and fades/mutes longer runs.
// Never transmit this marker or replace real received silence with it.
// The codec and its packet format are unchanged; tests regenerate these bytes
// with the codec encoder and verify the erasure/repeat/mute result flags.
constexpr uint8_t RX_LOST_AMBE[9] = {
    0xb2, 0x4a, 0x3c, 0x91, 0x02, 0x04, 0x60, 0x42, 0x0d};
} // namespace dstar
