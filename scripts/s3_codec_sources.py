"""Apply local S3 adaptations to build copies, never to the frozen library.

Both firmware and host validation call this function so they test the same math.
Exact anchors deliberately fail when a future codec import changes the kernel.
"""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1]

def transform(source: Path, text: str) -> str:
    if source.name == 'mbe_tone_fit.c':
        start = text.index('/* Each lane follows one residue class')
        end = text.index('\ndouble\nMBE_KFN(mbe_tone_fit)', start)
        kernel = (ROOT/'src/s3_tone_accumulate.h').read_text(encoding='utf-8')
        return '#include <stdint.h>\n#include <string.h>\n#include "mbe_compiler.h"\n' + text[:start] + kernel + text[end:]
    if source.name == 'mbelib.c':
        anchor = "/* A model's decoded amplitudes and phases, kept while it is rendered. */"
        if text.count(anchor) != 1:
            raise RuntimeError('Frozen render layout changed')
        text = text.replace(anchor, (ROOT/'src/s3_render_cache.h').read_text(encoding='utf-8')+'\n'+anchor)
        start = text.index('mbe_render_prepare(mbe_parms*')
        end = text.index('\nstatic void\nmbe_render_restore', start)
        render = text[start:end]
        render = render.replace('    const float hz_per_harmonic',
            '    struct s3_render_cache_entry* cache = s3_render_cache(mp->w0);\n    const float hz_per_harmonic', 1)
        render = render.replace('mbe_low_band_gain(hz)', 's3_render_gain(cache, l, hz)')
        render = render.replace('mbe_low_band_phase(hz)', 's3_render_phase(cache, l, hz)')
        render = render.replace('mbe_low_band_phase(hz_per_harmonic * (float)l)',
                                's3_render_phase(cache, l, hz_per_harmonic * (float)l)')
        return text[:start] + render + text[end:]
    if source.name == 'mbe_tone_detect.c':
        old = 'double w = 0.5 - (0.5 * cos(2.0 * M_PI * (double)(i + 1) / (double)(MBE_TONE_SPAN + 1)));'
        if text.count(old) != 1:
            raise RuntimeError('Frozen Hann window expression changed')
        # Keep double coefficients/multiplication to preserve the original final
        # float rounding. Hex literals reproduce the precomputed bits exactly.
        values = [0.5 - 0.5*math.cos(2.0*math.pi*(i+1)/161.0) for i in range(160)]
        table = '\nstatic const double s3_tone_hann[160] = {\n' + ',\n'.join(
            '    '+', '.join(x.hex() for x in values[i:i+4]) for i in range(0,160,4)) + '\n};\n'
        anchor = '#define TONE_BINS ((MBE_FFT_SIZE / 2) + 1)'
        if text.count(anchor) != 1:
            raise RuntimeError('Frozen tone spectrum declaration changed')
        text = text.replace(anchor, anchor + '\n_Static_assert(MBE_TONE_SPAN == 160, "S3 Hann table size");\n'+table)
        return text.replace(old, 'const double w = s3_tone_hann[i];')
    return text
