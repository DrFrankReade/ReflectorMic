#pragma once

#include <Arduino.h>
#include "build_features.h"

// Stored in RTC memory so the web UI can report which audio operation was in
// progress if a panic, watchdog, brownout, or software reset disconnects USB.
enum class RuntimeStage : uint32_t {
  Unknown = 0,
  Booting,
  Ready,
  StartingMicrophone,
  MicrophoneActive,
  QueueingCapture,
  Capturing,
  StartingSpeaker,
  SpeakerActive,
  QueueingTone,
  QueueingPlayback,
  StoppingAudio,
  SpeakerStopTimeout,
};

namespace RuntimeDiagnostics {

#if REFLECTORMIC_DIAGNOSTICS
void beginBoot();
void mark(RuntimeStage stage);
String summary();
#else
inline void beginBoot() {}
inline void mark(RuntimeStage) {}
inline String summary() { return "diagnostics disabled"; }
#endif

}  // namespace RuntimeDiagnostics
