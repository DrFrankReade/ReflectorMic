#pragma once
#include <cstdint>
namespace audio_settings {
constexpr uint32_t SAMPLE_RATE = 8000;
constexpr uint8_t RX_QUEUE_FRAMES = 32;
constexpr uint8_t TX_QUEUE_FRAMES = 16;
constexpr uint8_t AUDIO_CODEC_CORE = 0;
constexpr uint8_t AUDIO_CODEC_PRIORITY = 2;
// Retain stack headroom until the R13 target stack reports are checked.
constexpr uint32_t AUDIO_CODEC_STACK_BYTES = 48U * 1024U;
constexpr uint8_t PLAYBACK_CHUNK_FRAMES = 2;
constexpr uint32_t ECHO_CAPTURE_FRAMES = 500;
constexpr uint32_t LIVE_CAPTURE_FRAMES = 20;
constexpr uint32_t LIVE_CAPTURE_MAX_AGE_MS = 400;

}
