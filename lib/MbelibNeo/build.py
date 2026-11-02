"""Keep private codec/FFT headers out of the application include namespace."""
from pathlib import Path
Import("env")
root = Path(env.Dir(".").srcnode().abspath)
env.Append(CPPPATH=[str(root / "src/internal"), str(root / "pffft/include/pffft")])
