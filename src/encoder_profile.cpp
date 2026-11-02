#if defined(REFLECTORMIC_ENCODER_PROFILE)
#include "encoder_profile.h"
#include "encoder_profile_hooks.h"
#include <cstring>
#include <cstdlib>

namespace {
constexpr int COUNT = 31, BUCKETS = 7;
const char *names[COUNT] = {
  "encode_parameters", "frontend", "analysis", "pitch_autocorrelation", "pitch_lowpass",
  "pitch_errors", "track_pitch", "analysis_spectrum", "refine_pitch", "harmonic_measures",
  "harmonic_periodicity", "energy_factor", "encode_tone", "tone_track", "tone_detect_part",
  "tone_active_span", "tone_peaks", "tone_power_spectrum", "tone_refine", "tone_predictor",
  "tone_fit", "tone_accumulate_one", "tone_accumulate_two", "tone_solve", "encode_voice",
  "quantize_gain", "prediction", "quantize_residual", "codebook_search", "fft_forward", "acf"
};
const char *modes[BUCKETS] = {"silence", "low_voiced", "high_voiced", "noise", "mixed", "alternating", "microphone"};
struct Stat { uint64_t inclusive = 0, self = 0; uint32_t calls = 0, maximum = 0; };
Stat counters[BUCKETS][COUNT], published[BUCKETS][COUNT];
// Nesting is tracked separately from C scope addresses (returning a struct
// from enter() means its address is not stable until assigned in the caller).
uint32_t children[32]{};
int depth = 0, mode = 6;
uint32_t nestingErrors = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
}

extern "C" reflector_profile_scope reflector_profile_enter(int id) {
  if ((!depth && id != 0) || depth >= 32) {
    if (depth >= 32) ++nestingErrors;
    return {0, 0, -1, mode};
  }
  children[depth++] = 0;
  return {micros(), 0, id, mode};
}
extern "C" void reflector_profile_leave(reflector_profile_scope *scope) {
  if (scope->id < 0) return;
  const uint32_t elapsed = micros() - scope->start;
  const uint32_t child = children[--depth];
  auto &stat = counters[scope->bucket][scope->id];
  ++stat.calls;
  stat.inclusive += elapsed;
  stat.self += elapsed >= child ? elapsed - child : 0;
  stat.maximum = max(stat.maximum, elapsed);
  if (depth) children[depth-1] += elapsed;
  else {
    portENTER_CRITICAL(&mux);
    memcpy(published[scope->bucket], counters[scope->bucket], sizeof(published[0]));
    portEXIT_CRITICAL(&mux);
  }
}
void encoderProfileReset() {
  memset(counters, 0, sizeof(counters));
  portENTER_CRITICAL(&mux);
  memset(published, 0, sizeof(published));
  portEXIT_CRITICAL(&mux);
  depth = 0; nestingErrors = 0;
}
void encoderProfileMode(int value) { mode = constrain(value, 0, 6); }
String encoderProfileJson() {
  auto *copy = static_cast<Stat *>(malloc(sizeof(published)));
  if (!copy) return "{\"error\":\"profile snapshot allocation failed\"}";
  portENTER_CRITICAL(&mux);
  memcpy(copy, published, sizeof(published));
  portEXIT_CRITICAL(&mux);
  char prefix[200];
  snprintf(prefix, sizeof(prefix), "{\"enabled\":true,\"units\":\"microseconds\",\"timing\":\"elapsed; includes preemption and probe overhead\",\"nestingErrors\":%lu,\"groups\":[", static_cast<unsigned long>(nestingErrors));
  String out;
  out.reserve(28000);  // Avoid repeated reallocations while assembling the snapshot.
  out += prefix;
  for (int bucket = 0; bucket < BUCKETS; ++bucket) {
    if (bucket) out += ',';
    out += String("{\"mode\":\"") + modes[bucket] + "\",\"stages\":[";
    for (int id = 0; id < COUNT; ++id) {
      if (id) out += ',';
      const auto &s = copy[bucket*COUNT+id];
      char row[240];
      snprintf(row, sizeof(row), "{\"name\":\"%s\",\"calls\":%lu,\"inclusiveUs\":%llu,\"selfUs\":%llu,\"maxUs\":%lu}", names[id],
               static_cast<unsigned long>(s.calls), static_cast<unsigned long long>(s.inclusive),
               static_cast<unsigned long long>(s.self), static_cast<unsigned long>(s.maximum));
      out += row;
    }
    out += "]}";
  }
  free(copy);
  return out + "]}";
}
#endif
