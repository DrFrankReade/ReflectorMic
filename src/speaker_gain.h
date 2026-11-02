#pragma once

#include <cmath>
#include <cstdint>

namespace speaker_gain {
// ES8311 register 0x32 is a signed gain encoded with a bias, NOT a linear
// 0..255 volume: 0xBF = 0 dB, 0xFF = +32 dB. Never boost the DAC input.
constexpr uint8_t UNITY = 0xBF;

inline uint8_t dacRegister(uint8_t volume) {
  if (!volume) return 0;  // Also hardware-mute the amplifier at zero.
  // Preserve the slider's quadratic loudness curve, now AFTER I2S reception.
  // Round toward more attenuation; the maximum quantization error is 0.5 dB.
  const int halfDb = static_cast<int>(std::ceil(
      -80.0f * std::log10(static_cast<float>(volume) / 255.0f)));
  return halfDb >= UNITY ? 0 : static_cast<uint8_t>(UNITY - halfDb);
}
}  // namespace speaker_gain
