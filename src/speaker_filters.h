#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

// Speaker-only processing at 8 kHz. No codec state or packet data enters here.
struct SpeakerFilterSettings {
  constexpr SpeakerFilterSettings(bool on = false, uint16_t low = 200,
                                  uint16_t high = 3200, uint8_t slope = 6)
      : enabled(on), lowCutHz(low), highCutHz(high), slopeDb(slope) {}
  bool enabled = false;
  uint16_t lowCutHz = 200;
  uint16_t highCutHz = 3200;
  uint8_t slopeDb = 6;
  bool valid() const {
    return lowCutHz >= 50 && lowCutHz <= 800 &&
           highCutHz >= 1000 && highCutHz <= 3800 &&
           (slopeDb == 6 || slopeDb == 12);
  }
  constexpr uint32_t packed() const {
    return uint32_t(lowCutHz) | (uint32_t(highCutHz) << 10) |
           (uint32_t(enabled) << 22) | (uint32_t(slopeDb == 12) << 23);
  }
  static SpeakerFilterSettings unpack(uint32_t value) {
    return {bool(value & (1U << 22)), uint16_t(value & 1023),
            uint16_t((value >> 10) & 4095), uint8_t(value & (1U << 23) ? 12 : 6)};
  }
};

class SpeakerFilters {
 public:
  void reset() {
    hpInput_ = hpOutput_ = lpInput_ = lpOutput_ = mix_ = 0;
    hpInput2_ = hpOutput2_ = lpInput2_ = lpOutput2_ = slopeMix_ = 0;
  }
  void process(int16_t* pcm, size_t count, uint32_t settings) {
    const auto s = SpeakerFilterSettings::unpack(settings);
    if (!s.valid()) return;
    if (settings != settings_) {
      settings_ = settings;
      // Bilinear first-order HP and LP. Trigonometry runs only on changes;
      // interpolate stable pole coefficients during playback, not filter state.
      targetHp_ = pole(s.lowCutHz);
      targetLp_ = pole(s.highCutHz);
      if (mix_ == 0) { hp_ = targetHp_; lp_ = targetLp_; }
    }
    if (!s.enabled && mix_ == 0) { reset(); return; } // Bit-exact bypass.
    for (size_t i = 0; i < count; ++i) {
      hp_ += (targetHp_ - hp_) * (1.0f / 160.0f);
      lp_ += (targetLp_ - lp_) * (1.0f / 160.0f);
      const float x = pcm[i];
      const float h = (1 + hp_) * 0.5f * (x - hpInput_) + hp_ * hpOutput_;
      const float y = (1 - lp_) * 0.5f * (h + lpInput_) + lp_ * lpOutput_;
      hpInput_ = x; hpOutput_ = h; lpInput_ = h; lpOutput_ = y;
      // Keep the second stage warm even at 6 dB/octave. Changing slopes
      // crossfades existing outputs instead of resetting a live filter.
      // Cascading identical stages gives 12 dB/octave, with -6 dB at each
      // individual corner (rather than a resonant or Butterworth response).
      const float h2 = (1 + hp_) * 0.5f * (y - hpInput2_) + hp_ * hpOutput2_;
      const float y2 = (1 - lp_) * 0.5f * (h2 + lpInput2_) + lp_ * lpOutput2_;
      hpInput2_ = y; hpOutput2_ = h2; lpInput2_ = h2; lpOutput2_ = y2;
      slopeMix_ += s.slopeDb == 12 ? 1.0f / 320.0f : -1.0f / 320.0f;
      if (slopeMix_ < 0) slopeMix_ = 0;
      if (slopeMix_ > 1) slopeMix_ = 1;
      const float filtered = y + slopeMix_ * (y2 - y);
      // 40 ms wet/dry crossfade; toggling midway reverses smoothly.
      mix_ += s.enabled ? 1.0f / 320.0f : -1.0f / 320.0f;
      if (mix_ < 0) mix_ = 0;
      if (mix_ > 1) mix_ = 1;
      const float out = x + mix_ * (filtered - x);
      pcm[i] = out >= 32767 ? 32767 : out <= -32768 ? -32768 :
               static_cast<int16_t>(std::round(out));
    }
  }
 private:
  static float pole(uint16_t hz) {
    const float k = std::tan(3.14159265358979323846f * hz / 8000.0f);
    return (1 - k) / (1 + k);
  }
  uint32_t settings_ = UINT32_MAX;
  float hp_ = 0, lp_ = 0, targetHp_ = 0, targetLp_ = 0, mix_ = 0;
  float hpInput_ = 0, hpOutput_ = 0, lpInput_ = 0, lpOutput_ = 0;
  float hpInput2_ = 0, hpOutput2_ = 0, lpInput2_ = 0, lpOutput2_ = 0, slopeMix_ = 0;
};
