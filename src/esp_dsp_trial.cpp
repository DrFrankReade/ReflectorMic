#if defined(REFLECTORMIC_DSP_TRIAL)
#include "esp_dsp_trial.h"
#include "esp_dsp_fft_layout.h"
#include "dstar_codec.h"
#include "../lib/MbelibNeo/pffft/include/pffft/pffft.h"
#include <dsps_fft2r.h>
#include <cmath>
#include <cstring>

extern "C" {
PFFFT_Setup *__real_pffft_new_setup(int, pffft_transform_t);
void __real_pffft_destroy_setup(PFFFT_Setup *);
void __real_pffft_transform_ordered(const PFFFT_Setup *, const float *, float *,
                                    float *, pffft_direction_t);
}

namespace {
// One codec worker owns these buffers and the FFT table. They must not be used
// from the web/PTT task. Explicit alignment is mandatory for S3 wide loads.
alignas(16) float scratch[1024];
alignas(16) float twiddles[512];
struct Plan { const PFFFT_Setup *setup; int size; };
Plan plans[16]{};
bool ready = false;
bool enabled = false;
struct Stats { uint32_t calls = 0, us = 0, accelerated = 0, failures = 0; } stats;
char result[512] = "S3 DSP trial awaiting on-device validation";
portMUX_TYPE resultMux = portMUX_INITIALIZER_UNLOCKED;

void publish(const String &text) {
  portENTER_CRITICAL(&resultMux);
  strlcpy(result, text.c_str(), sizeof(result));
  portEXIT_CRITICAL(&resultMux);
}

bool complexFft(float *data, int size) {
  // Explicit aes3 call prevents a build-option change from silently selecting
  // ANSI C. This installed kernel uses S3 PIE loads/stores and scalar FPU MACs.
  return dsps_fft2r_fc32_aes3_(data, size, dsps_fft_w_table_fc32) == ESP_OK &&
         dsps_bit_rev_fc32_ansi(data, size) == ESP_OK;
}

int planSize(const PFFFT_Setup *setup) {
  for (const auto &plan : plans) if (plan.setup == setup) return plan.size;
  return 0;
}

bool compare(const float *a, const float *b, int size) {
  float peak = 0.0f, error = 0.0f;
  for (int i = 0; i < size; ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i])) return false;
    peak = max(peak, fabsf(a[i]));
    error = max(error, fabsf(a[i] - b[i]));
  }
  return error <= 0.001f + 0.000005f * peak;
}

bool validateTransforms() {
  // These arrays use < 10 KB of the existing 48 KB codec worker stack.
  alignas(16) float input[512], reference[512], candidate[512], work[512];
  for (int size : {256, 512}) {
    PFFFT_Setup *plan = __real_pffft_new_setup(size, PFFFT_REAL);
    if (!plan) return false;
    bool ok = true;
    for (int mode = 0; mode < 6 && ok; ++mode) {
      uint32_t random = 12345;
      for (int i = 0; i < size; ++i) {
        random = random * 1664525u + 1013904223u;
        input[i] = mode == 0 ? 0.0f : mode == 1 ? (i == 1 ? 1.0f : 0.0f) :
            mode == 2 ? 1000.0f : mode == 3 ? (i & 1 ? -1000.0f : 1000.0f) :
            mode == 4 ? 2000.0f * sinf(i * 0.173f) :
            static_cast<int16_t>(random >> 16) * 0.25f;
      }
      __real_pffft_transform_ordered(plan, input, reference, work, PFFFT_FORWARD);
      ok = reflector_dsp::realTransform(input, candidate, size, true, scratch, complexFft) &&
           compare(reference, candidate, size);
      // Compare inverse implementations on IDENTICAL packed input as well.
      __real_pffft_transform_ordered(plan, reference, input, work, PFFFT_BACKWARD);
      ok = ok && reflector_dsp::realTransform(reference, candidate, size, false,
                                             scratch, complexFft) &&
           compare(input, candidate, size);
      vTaskDelay(1);
    }
    __real_pffft_destroy_setup(plan);
    if (!ok) return false;
  }
  return true;
}

constexpr int MODES = 6, BLOCKS = 16, FRAMES = MODES * BLOCKS;
void makePcm(int mode, int block, uint32_t &random, int16_t *pcm) {
  for (int n = 0; n < 160; ++n) {
    random = random * 1664525u + 1013904223u;
    const float noise = (static_cast<int32_t>(random >> 16) - 32768) * 0.15f;
    const float t = (block * 160 + n) / 8000.0f;
    const float fundamental = mode == 2 ? 310.0f : 85.0f;
    float voice = 0.0f;
    for (int h = 1; h <= 12; ++h)
      voice += (8000.0f / h) * sinf(6.28318530718f * fundamental * h * t);
    const float sample = mode == 0 ? 0.0f : mode == 3 ? noise :
        mode == 4 ? voice * 0.4f + noise : mode == 5 && (block & 1) ? noise : voice;
    pcm[n] = static_cast<int16_t>(constrain(sample, -32768.0f, 32767.0f));
  }
}
}  // namespace

// GNU linker wrapping leaves every frozen MBELIB/PFFFT source byte unchanged.
// Unsupported plans, disabled trials and backend errors use the original FFT.
extern "C" PFFFT_Setup *__wrap_pffft_new_setup(int size, pffft_transform_t type) {
  auto *setup = __real_pffft_new_setup(size, type);
  if (setup && type == PFFFT_REAL && (size == 256 || size == 512)) {
    for (auto &plan : plans) if (!plan.setup) { plan = {setup, size}; break; }
  }
  return setup;
}
extern "C" void __wrap_pffft_destroy_setup(PFFFT_Setup *setup) {
  for (auto &plan : plans) if (plan.setup == setup) plan = {};
  __real_pffft_destroy_setup(setup);
}
extern "C" void __wrap_pffft_transform_ordered(const PFFFT_Setup *setup,
    const float *input, float *output, float *work, pffft_direction_t direction) {
  const uint32_t started = micros();
  const int size = enabled && ready ? planSize(setup) : 0;
  if (size && reflector_dsp::realTransform(input, output, size,
                         direction == PFFFT_FORWARD, scratch, complexFft)) {
    ++stats.accelerated;
  } else {
    if (size) { ++stats.failures; enabled = false; }
    __real_pffft_transform_ordered(setup, input, output, work, direction);
  }
  ++stats.calls;
  stats.us += micros() - started;
}

void runEspDspTrial(DStarCodec &codec) {
  enabled = false;
  publish("S3 DSP trial running; original FFT remains the fallback");
  // Supply our own aligned internal-RAM table; avoid dependence on the SDK's
  // internal allocator or alignment of a static table in another component.
  ready = dsps_fft2r_init_fc32(twiddles, 512) == ESP_OK;
  if (!ready || !validateTransforms()) {
    publish("S3 DSP validation failed; scalar PFFFT retained");
    return;
  }
  uint8_t baseline[FRAMES][9];
  uint32_t encodeUs[2]{}, decodeUs[2]{};
  Stats measured[2];
  uint32_t mismatches = 0, failures = 0;
  // Warm tables/caches before either timing pass, excluding first-use cost.
  int16_t pcm[160]{}, decoded[160]; uint8_t frame[9];
  codec.encode(pcm, frame); codec.decode(frame, decoded);
  for (int pass = 0; pass < 2; ++pass) {
    enabled = pass == 1;
    stats = {};
    uint32_t random = 0x64e59u;
    for (int mode = 0; mode < MODES; ++mode) {
      codec.resetEncoder(); codec.resetDecoder();
      for (int block = 0; block < BLOCKS; ++block) {
        const int index = mode * BLOCKS + block;
        makePcm(mode, block, random, pcm);
        uint32_t start = micros();
        const bool encoded = codec.encode(pcm, frame);
        encodeUs[pass] += micros() - start;
        if (!encoded) ++failures;
        if (!pass) memcpy(baseline[index], frame, 9);
        else if (memcmp(baseline[index], frame, 9)) ++mismatches;
        start = micros();
        if (!codec.decode(frame, decoded)) ++failures;
        decodeUs[pass] += micros() - start;
        vTaskDelay(1);
      }
    }
    measured[pass] = stats;
  }
  // A speedup confined to FFTs may barely change total encode time. Require
  // a 5% encode improvement AND byte-identical AMBE on this sampled corpus.
  enabled = ready && !mismatches && !failures && !measured[1].failures &&
            measured[1].accelerated && measured[1].calls == measured[0].calls &&
            encodeUs[1] < encodeUs[0] * 0.95f;
  codec.resetEncoder(); codec.resetDecoder();
  publish(String(enabled ? "S3 DSP active" : "scalar PFFFT retained") +
      "; A/B encode avg " + String(encodeUs[0] / FRAMES) + "/" +
      String(encodeUs[1] / FRAMES) + " us, decode " + String(decodeUs[0] / FRAMES) +
      "/" + String(decodeUs[1] / FRAMES) + " us; FFT total " +
      String(measured[0].us) + "/" + String(measured[1].us) +
      " us, accelerated " + String(measured[1].accelerated) +
      "; AMBE mismatches " + String(mismatches) + "/" + String(FRAMES) +
      ", failures " + String(failures + measured[1].failures));
  Serial.printf("[dsp] %s\n", result);
}

String espDspDiagnostic() {
  char copy[sizeof(result)];
  portENTER_CRITICAL(&resultMux);
  memcpy(copy, result, sizeof(copy));
  portEXIT_CRITICAL(&resultMux);
  return String(copy);
}
#endif
