#pragma once
#include <Arduino.h>
class DStarCodec;

// Optional trial environment only. The normal firmware keeps the original
// codec and PFFFT linkage; no frozen library files are patched by this adapter.
#if defined(REFLECTORMIC_DSP_TRIAL)
void runEspDspTrial(DStarCodec &codec);
String espDspDiagnostic();
#else
inline void runEspDspTrial(DStarCodec &) {}
inline String espDspDiagnostic() { return "scalar PFFFT; S3 DSP trial disabled"; }
#endif
