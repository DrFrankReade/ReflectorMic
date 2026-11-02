#include "audio_engine.h"
#include "audio_settings.h"
#include "runtime_diagnostics.h"
#include <cstring>
using namespace audio_settings;

#include <cmath>
#include <esp_heap_caps.h>
#include "esp_dsp_trial.h"
#include "encoder_profile.h"
#if REFLECTORMIC_DIAGNOSTICS

void AudioEngine::recordReceivedFrame(const RxItem& item, const int16_t* pcm,
                                     bool decoded, uint32_t elapsedUs,
                                     const mbe_process_result& result) {
  lastDecodeUs_ = elapsedUs;
  if (elapsedUs > maxDecodeUs_) maxDecodeUs_ = elapsedUs;
  uint16_t peak = 0;
  if (decoded) {
    for (unsigned i = 0; i < DSTAR_PCM_SAMPLES; ++i) {
      const uint16_t magnitude = pcm[i] == INT16_MIN ? 32768U : abs(pcm[i]);
      if (magnitude > peak) peak = magnitude;
    }
    lastPlaybackPeak_ = peak;
    if (peak > maxPlaybackPeak_) maxPlaybackPeak_ = peak;
  }
  RxTraceFrame trace{};
  trace.timestampMs = millis();
  trace.stream = rxTraceStream_;
  trace.frame = rxTraceFrame_++;
  trace.decodeUs = elapsedUs;
  trace.flags = result.flags;
  trace.correctedErrors = result.total_errors;
  trace.decoded = decoded;
  trace.peak = peak;
  memcpy(trace.ambe, item.ambe, sizeof(trace.ambe));
  if (decoded) memcpy(trace.pcm, pcm, sizeof(trace.pcm));
  rxTrace_.push(trace);
  if (result.total_errors > 0) rxCorrectedErrors_ += result.total_errors;
  if (result.flags & MBE_PROCESS_FLAG_MUTE) ++rxMutedFrames_;
  if (result.flags & MBE_PROCESS_FLAG_REPEAT) ++rxRepeatedFrames_;
}

bool AudioEngine::requestCodecTest() {
  if (!task_ || codecTestBusy() || txRequested_ || captureBusy_ ||
      playbackBusy()) return false;
  codecTestState_ = 1;
  return true;
}

String AudioEngine::codecDiagnostic() const {
  const uint8_t state = codecTestState_;
  if (state == 1) return "MBELIB-neo R13: codec timing test queued";
  if (state == 2) return "MBELIB-neo R13: codec timing test running";
  String result = "MBELIB-neo R13: encode max " + String(testEncodeMaxUs_) +
      " us, decode max " + String(testDecodeMaxUs_) + " us";
  if (testFrames_) result += ", averages " +
      String(testEncodeSumUs_ / testFrames_) + "/" +
      String(testDecodeSumUs_ / testFrames_) + " us";
  result += ", " + String(testFrames_) + " test frames";
  if (testFailures_) result += ", failures " + String(testFailures_);
  result += testEncodeMaxUs_ < 20000 && testDecodeMaxUs_ < 20000 &&
                    testFrames_ && !testFailures_
      ? "; sampled frames fit 20 ms" : "; exceeds 20 ms budget";
  return result;
}

void AudioEngine::runCodecTest() {
  codecTestState_ = 2;
  runEspDspTrial(codec_);
  // Warm one frame before resetting profiler counters, excluding cold tables.
#if defined(REFLECTORMIC_ENCODER_PROFILE)
  int16_t warm[160]{}; uint8_t warmAmbe[9];
  codec_.encode(warm, warmAmbe);
  encoderProfileReset();
#endif
  testEncodeMaxUs_ = testDecodeMaxUs_ = 0;
  testEncodeSumUs_ = testDecodeSumUs_ = testFrames_ = testFailures_ = 0;
  int16_t pcm[DSTAR_PCM_SAMPLES], decoded[DSTAR_PCM_SAMPLES];
  uint8_t frame[DSTAR_AMBE_BYTES];
  uint32_t random = 0x64e59u;
  // Silence, low/high voiced combs, noise, mixed HNR, alternating voicing.
  for (int mode = 0; mode < 6; ++mode) {
    encoderProfileMode(mode);
    codec_.resetEncoder();
    codec_.resetDecoder();
    for (int block = 0; block < 8; ++block) {
      for (int n = 0; n < DSTAR_PCM_SAMPLES; ++n) {
        random = random * 1664525u + 1013904223u;
        const float noise = (static_cast<int32_t>(random >> 16) - 32768) * 0.15f;
        const float t = (block * DSTAR_PCM_SAMPLES + n) / 8000.0f;
        const float frequency = mode == 2 ? 310.0f : 85.0f;
        float voiced = 0.0f;
        for (int h = 1; h <= 12; ++h)
          voiced += (8000.0f / h) * sinf(6.28318530718f * frequency * h * t);
        const float value = mode == 0 ? 0.0f : mode == 3 ? noise :
            mode == 4 ? voiced * 0.4f + noise :
            mode == 5 && (block & 1) ? noise : voiced;
        pcm[n] = static_cast<int16_t>(constrain(value, -32768.0f, 32767.0f));
      }
      uint32_t started = micros();
      const bool encoded = codec_.encode(pcm, frame);
      const uint32_t encodeUs = micros() - started;
      started = micros();
      const bool ok = encoded && codec_.decode(frame, decoded);
      const uint32_t decodeUs = micros() - started;
      testEncodeSumUs_ += encodeUs;
      testDecodeSumUs_ += decodeUs;
      testEncodeMaxUs_ = max(testEncodeMaxUs_, encodeUs);
      testDecodeMaxUs_ = max(testDecodeMaxUs_, decodeUs);
      ++testFrames_;
      if (!ok) ++testFailures_;
      vTaskDelay(1);
    }
  }
  codec_.resetEncoder();
  codec_.resetDecoder();
  codecTestState_ = 3;
  encoderProfileMode(6);  // Subsequent echo/PTT frames form the microphone group.
  Serial.printf("[codec] %s\n", codecDiagnostic().c_str());
}


void AudioEngine::initializeTrace() {
  auto* traceRing = static_cast<RxTraceFrame*>(heap_caps_calloc(
      128, sizeof(RxTraceFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* traceSnapshot = static_cast<RxTraceSnapshot*>(heap_caps_calloc(
      1, sizeof(RxTraceSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (traceRing && traceSnapshot) rxTrace_.begin(traceRing, traceSnapshot);
  else {
    // Diagnostic allocation failure must not disable normal audio.
    heap_caps_free(traceRing);
    heap_caps_free(traceSnapshot);
  }
}
#else
bool AudioEngine::requestCodecTest() { return false; }
String AudioEngine::codecDiagnostic() const { return "diagnostics disabled"; }
#endif
