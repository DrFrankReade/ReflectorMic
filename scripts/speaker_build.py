"""Compile a generated speaker adaptation without editing the pinned library."""
from pathlib import Path
import sys

Import('env')
root = Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0, str(root / 'scripts'))
from speaker_sources import transform


def adapt_speaker(build_env, node):
    source = Path(node.srcnode().abspath)
    if 'M5Unified' not in source.parts or source.name != 'Speaker_Class.cpp':
        return node
    out = Path(env.subst('$BUILD_DIR')) / 'speaker_sources'
    out.mkdir(parents=True, exist_ok=True)
    generated = out / source.name
    content = '#line 1 "' + source.as_posix() + '"\n' + transform(source.read_text(encoding='utf-8'))
    if not generated.exists() or generated.read_text(encoding='utf-8') != content:
        generated.write_text(content, encoding='utf-8')
    local = build_env.Clone()
    local.Prepend(CPPPATH=[str(source.parent)])
    return local.Object(str(out / 'Speaker_Class.o'), str(generated))[0]


env.AddBuildMiddleware(adapt_speaker)
