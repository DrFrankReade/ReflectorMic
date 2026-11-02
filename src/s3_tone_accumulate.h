// SPDX-License-Identifier: GPL-2.0-or-later
// Local S3 backend for the frozen R13 tone fitter. Included into a generated
// copy of mbe_tone_fit.c only. The public ABI and small double Cholesky solve
// stay unchanged; the per-sample oscillators and projections use the S3 FPU.
// Do not use fast-math: candidate selection depends on small energy differences.
struct tone_oscillator { float c, s, cw, sw; };

static struct tone_oscillator tone_oscillator_init(double hz) {
    const float w = (float)(2.0 * M_PI * hz / MBE_TONE_FS);
    // Coordinate search often changes only one frequency of a tone pair.
    // Reuse exactly the same sinf/cosf results for repeated float arguments.
    // The ESP32 wrapper owns a single codec task; host builds retain TLS.
    static MBE_THREAD_LOCAL struct { uint32_t key; float c, s; int valid; } cache[8];
    static MBE_THREAD_LOCAL unsigned next;
    uint32_t key;
    memcpy(&key, &w, sizeof(key));
    for (unsigned i = 0; i < 8; ++i) {
        if (cache[i].valid && cache[i].key == key) {
            struct tone_oscillator o = {1.0f, 0.0f, cache[i].c, cache[i].s};
            return o;
        }
    }
    struct tone_oscillator o = {1.0f, 0.0f, cosf(w), sinf(w)};
    cache[next].key = key; cache[next].c = o.cw; cache[next].s = o.sw;
    cache[next].valid = 1;
    next = (next + 1) % 8;
    return o;
}

static void tone_oscillator_step(struct tone_oscillator* o) {
    const float next = o->c * o->cw - o->s * o->sw;
    o->s = o->s * o->cw + o->c * o->sw;
    o->c = next;
}

static void
tone_accumulate_one(const float* x, int n, double hz, double gram[4][4], double rhs[4]) {
    struct tone_oscillator o = tone_oscillator_init(hz);
    float cc = 0.0f, cs = 0.0f, ss = 0.0f, cx = 0.0f, sx = 0.0f;
    for (int i = 0; i < n; ++i) {
        cc += o.c * o.c;
        cs += o.c * o.s;
        ss += o.s * o.s;
        cx += o.c * x[i];
        sx += o.s * x[i];
        tone_oscillator_step(&o);
    }
    gram[0][0] = cc; gram[1][0] = cs; gram[1][1] = ss;
    rhs[0] = cx; rhs[1] = sx;
}

static void
tone_accumulate_two(const float* x, int n, const double hz[2], double gram[4][4], double rhs[4]) {
    struct tone_oscillator a = tone_oscillator_init(hz[0]), b = tone_oscillator_init(hz[1]);
    float g00=0.0f, g10=0.0f, g11=0.0f, g20=0.0f, g21=0.0f, g22=0.0f;
    float g30=0.0f, g31=0.0f, g32=0.0f, g33=0.0f;
    float r0=0.0f, r1=0.0f, r2=0.0f, r3=0.0f;
    for (int i = 0; i < n; ++i) {
        const float v = x[i];
        g00 += a.c*a.c; g10 += a.s*a.c; g11 += a.s*a.s;
        g20 += b.c*a.c; g21 += b.c*a.s; g22 += b.c*b.c;
        g30 += b.s*a.c; g31 += b.s*a.s; g32 += b.s*b.c; g33 += b.s*b.s;
        r0 += a.c*v; r1 += a.s*v; r2 += b.c*v; r3 += b.s*v;
        tone_oscillator_step(&a);
        tone_oscillator_step(&b);
    }
    gram[0][0]=g00; gram[1][0]=g10; gram[1][1]=g11;
    gram[2][0]=g20; gram[2][1]=g21; gram[2][2]=g22;
    gram[3][0]=g30; gram[3][1]=g31; gram[3][2]=g32; gram[3][3]=g33;
    rhs[0]=r0; rhs[1]=r1; rhs[2]=r2; rhs[3]=r3;
}
