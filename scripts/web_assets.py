"""PlatformIO/SCons pre-script: generate assets before source dependency scanning."""
from pathlib import Path
import runpy

Import('env')

root = Path(env.subst('$PROJECT_DIR'))
generated_dir = Path(env.subst('$BUILD_DIR')) / 'generated'
generator = runpy.run_path(str(root / 'scripts' / 'build_web.py'))
generator['build_assets'](root / 'web', generated_dir / 'reflectormic_web_assets.h')
env.Prepend(CPPPATH=[str(generated_dir)])
