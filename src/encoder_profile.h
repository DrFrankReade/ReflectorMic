#pragma once
#include <Arduino.h>
#if defined(REFLECTORMIC_ENCODER_PROFILE)
void encoderProfileReset();
void encoderProfileMode(int mode);
String encoderProfileJson();
#else
inline void encoderProfileReset() {}
inline void encoderProfileMode(int) {}
inline String encoderProfileJson() { return "{\"enabled\":false}"; }
#endif
