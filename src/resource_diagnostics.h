#pragma once
#include "build_features.h"
#include <Arduino.h>

// All calls run on the Arduino/network core. No codec probes or shared locks.
namespace ResourceDiagnostics {
#if REFLECTORMIC_DIAGNOSTICS
void begin();
void beginLoop();
void endLoop();
String snapshotJson();
#else
inline void begin() {}
inline void beginLoop() {}
inline void endLoop() {}
#endif
} // namespace ResourceDiagnostics
