#include "dstar_codec.h"

DStarCodec::~DStarCodec() {
  if (encoder_) mbe_ambe2400EncoderFree(encoder_);
}

bool DStarCodec::begin() {
  encoder_ = mbe_ambe2400EncoderAlloc();
  if (!encoder_) return false;
  resetEncoder();
  resetDecoder();
  return true;
}

void DStarCodec::resetEncoder() {
  if (encoder_) mbe_ambe2400EncoderReset(encoder_);
  mbe_initMbeParms(&txCurrent_, &txPrevious_, &txEnhanced_);
}

void DStarCodec::resetDecoder() {
  mbe_setThreadRngSeed(0x9e3779b9u);
  mbe_initMbeParms(&rxCurrent_, &rxPrevious_, &rxEnhanced_);
}

bool DStarCodec::encode(const int16_t *pcm, uint8_t *ambe) {
  static_assert(sizeof(short) == sizeof(int16_t), "MBE PCM requires 16-bit short");
  char bits[49], planes[4][24];
  if (!encoder_ || mbe_encodeAmbe2400ParmsShort(encoder_, pcm, bits,
                                               &txCurrent_, &txPrevious_) < 0 ||
      mbe_encodeAmbe3600x2400Frame(bits, planes) < 0 ||
      mbe_encodeDStarDVData(planes, ambe) < 0) return false;
  // The parameter encoder does not advance prediction history itself.
  mbe_moveMbeParms(&txCurrent_, &txPrevious_);
  return true;
}

bool DStarCodec::decode(const uint8_t *ambe, int16_t *pcm, mbe_process_result* diagnostic) {
  char bits[49], planes[4][24];
  mbe_process_result result{};
  if (diagnostic) *diagnostic = result;
  if (mbe_decodeDStarDVData(ambe, planes) < 0) return false;
  // This full frame API performs FEC, synthesis and RX history advancement.
  const bool ok = mbe_processAmbe3600x2400Frame(pcm, &result, planes, bits,
                                     &rxCurrent_, &rxPrevious_, &rxEnhanced_) >= 0;
  if (diagnostic) *diagnostic = result;
  return ok;
}

const char *DStarCodec::version() const {
  return mbe_versionString();
}
