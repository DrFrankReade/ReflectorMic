#pragma once

#include <cstdint>
#include <mbelib-neo/mbelib.h>

constexpr unsigned DSTAR_PCM_SAMPLES = 160;
constexpr unsigned DSTAR_AMBE_BYTES = 9;

// All encode/decode/reset operations belong to the single audio worker.
// TX and RX prediction states must remain independent, including for echo.
class DStarCodec {
 public:
  ~DStarCodec();
  bool begin();
  void resetEncoder();
  void resetDecoder();
  bool encode(const int16_t pcm[DSTAR_PCM_SAMPLES], uint8_t ambe[DSTAR_AMBE_BYTES]);
  bool decode(const uint8_t ambe[DSTAR_AMBE_BYTES], int16_t pcm[DSTAR_PCM_SAMPLES],
              mbe_process_result* diagnostic = nullptr);
  const char *version() const;
 private:
  mbe_ambe2400_encoder *encoder_ = nullptr;
  mbe_parms txCurrent_{}, txPrevious_{}, txEnhanced_{};
  mbe_parms rxCurrent_{}, rxPrevious_{}, rxEnhanced_{};
};
