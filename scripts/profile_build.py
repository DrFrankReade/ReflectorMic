"""Instrument generated build copies only; never edit the frozen codec tree."""
from pathlib import Path
import re
import sys
Import('env')

ROOT = Path(env.subst('$PROJECT_DIR'))
OUT = Path(env.subst('$BUILD_DIR')) / 'profile_sources'
sys.path.insert(0, str(ROOT/'scripts'))
from s3_codec_sources import transform
FLAGS = env.subst('$BUILD_FLAGS')
OPTIMIZE = 'REFLECTORMIC_S3_TONE_F32' in FLAGS
PROFILE = 'REFLECTORMIC_ENCODER_PROFILE' in FLAGS
NAMES = [
    'mbe_encodeAmbe2400ParmsShort', 'mbe_encoder_frontend_analyze',
    'mbe_analysis_frame', 'pitch_autocorrelation', 'pitch_lowpass',
    'pitch_errors', 'track_pitch', 'analysis_spectrum', 'refine_pitch',
    'harmonic_measures', 'harmonic_periodicity', 'energy_factor',
    'ambe2400_encode_tone', 'mbe_tone_track', 'tone_detect_part',
    'tone_active_span', 'tone_peaks', 'tone_power_spectrum',
    'tone_refine', 'tone_pair_from_predictor', 'MBE_KFN(mbe_tone_fit)',
    'tone_accumulate_one', 'tone_accumulate_two', 'tone_solve',
    'ambe2400_encode_voice', 'ambe_enc_quantize_gain', 'ambe_enc_prediction',
    'ambe_enc_quantize_residual', 'ambe_enc_nearest_impl',
    'mbe_fft_forward_real', 'mbe_acf_compute',
]

def instrument(build_env, node):
    source = Path(node.srcnode().abspath)
    if 'MbelibNeo' not in source.parts or source.suffix != '.c': return node
    text = source.read_text(encoding='utf-8')
    original = text
    if OPTIMIZE:
        text = transform(source, text)
    changed = text != original
    for index, name in enumerate(NAMES if PROFILE else []):
        # Definitions start at column zero. Calls and declarations cannot match.
        pattern = r'(?m)^' + re.escape(name) + r'\([^;{}]*\)\s*\{'
        text, count = re.subn(pattern, lambda m: m[0] +
            '\n    reflector_profile_scope rm_probe __attribute__((cleanup(reflector_profile_leave))) = '
            f'reflector_profile_enter({index});', text)
        if count > 1: raise RuntimeError('Ambiguous profile function: '+name)
        changed |= count > 0
    if not changed: return node
    OUT.mkdir(parents=True, exist_ok=True)
    generated = OUT / source.name
    header = (ROOT/'src/encoder_profile_hooks.h').as_posix()
    generated.write_text(f'#include "{header}"\n#line 1 "{source.as_posix()}"\n'+text, encoding='utf-8')
    return build_env.Object(str(OUT/(source.stem+'.o')), str(generated))

env.AddBuildMiddleware(instrument)
