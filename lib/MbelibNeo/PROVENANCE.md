# MBELIB-neo R13

This directory contains the D-Star subset of MBELIB-neo 2.6.2 with the R13 speech-analysis implementation. The base source commit is `a3c7bbdae9eb21cbfab57006c1a5daca7bdcba7f`.

The R13 implementation snapshots have these SHA-256 hashes:

- `src/core/mbe_speech_analysis.c`: `c0e41864217594060cdd4577bdf45fb1c9c0737f163a271a6e70aaf25ed7a874`
- `src/internal/mbe_speech_analysis.h`: `a2e0669eeed69c2c5d807e72e58a112ab3d65e77ff4f870bf947dd98df542460`

The wrapper uses the AMBE 3600x2400/D-Star APIs with signed 16-bit PCM: 160 samples at 8 kHz per nine-byte frame. Encoder history and decoder history are independent.

## Port adaptations

- `mbe_compiler.h` uses process-local storage in place of thread-local storage on ESP32. Library calls must remain on the single codec worker; initialization completes before that worker starts.
- `library.json` applies library-scoped optimization, static linkage, and scalar PFFFT configuration.
- `include/mbelib-neo/version.h` identifies the included R13 version.
- The application's source adapters generate ESP32-S3 tone-fitting, window, and cache adaptations during compilation, leaving these codec source files intact.

`source-manifest.json` records source checksums. Original source notices, the GPL license texts, and PFFFT's license are retained in this directory.
