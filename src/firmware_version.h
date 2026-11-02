#pragma once
#include "build_features.h"

// Application identity is separate from mbe_versionString() in the codec.
#if !REFLECTORMIC_DIAGNOSTICS
static constexpr const char *REFLECTORMIC_FIRMWARE = "ReflectorMic 2026-11-01.2 lean";
#elif defined(REFLECTORMIC_ENCODER_PROFILE)
static constexpr const char *REFLECTORMIC_FIRMWARE = "ReflectorMic 2026-11-01.2 profile";
#else
static constexpr const char *REFLECTORMIC_FIRMWARE = "ReflectorMic 2026-11-01.2";
#endif
