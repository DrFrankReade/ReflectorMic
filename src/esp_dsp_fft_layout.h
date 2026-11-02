#pragma once

// PFFFT real transforms pack DC, Nyquist, then real/imaginary pairs. ESP-DSP
// operates on a full interleaved complex spectrum. Keep layout/scaling in a
// portable adapter, independently tested against a DFT. No codec math changes.
namespace reflector_dsp {
using ComplexFft = bool (*)(float *data, int size);

inline bool realTransform(const float *input, float *output, int size,
                          bool forward, float *scratch, ComplexFft fft) {
  if (forward) {
    for (int i = 0; i < size; ++i) {
      scratch[2 * i] = input[i];
      scratch[2 * i + 1] = 0.0f;
    }
  } else {
    // A forward FFT of the conjugate Hermitian spectrum gives the real,
    // unnormalized inverse required by PFFFT (the codec applies 1/N itself).
    scratch[0] = input[0];
    scratch[1] = 0.0f;
    scratch[size] = input[1];
    scratch[size + 1] = 0.0f;
    for (int k = 1; k < size / 2; ++k) {
      scratch[2 * k] = scratch[2 * (size - k)] = input[2 * k];
      scratch[2 * k + 1] = -input[2 * k + 1];
      scratch[2 * (size - k) + 1] = input[2 * k + 1];
    }
  }
  if (!fft(scratch, size)) return false;
  if (forward) {
    output[0] = scratch[0];
    output[1] = scratch[size];
    for (int k = 2; k < size; ++k) output[k] = scratch[k];
  } else {
    for (int i = 0; i < size; ++i) output[i] = scratch[2 * i];
  }
  return true;
}
}  // namespace reflector_dsp
