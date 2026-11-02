#pragma once

// One switch for optional telemetry, frame recording and benchmarks. Keep
// recovery, timeout enforcement and error reporting in every build.
#ifndef REFLECTORMIC_DIAGNOSTICS
#define REFLECTORMIC_DIAGNOSTICS 1
#endif
#if REFLECTORMIC_DIAGNOSTICS != 0 && REFLECTORMIC_DIAGNOSTICS != 1
#error "REFLECTORMIC_DIAGNOSTICS must be 0 or 1"
#endif
#if !REFLECTORMIC_DIAGNOSTICS && (defined(REFLECTORMIC_ENCODER_PROFILE) || defined(REFLECTORMIC_DSP_TRIAL))
#error "Codec profiling/FFT trials require diagnostics"
#endif
