"""Verify the receive loss marker with the actual locally optimized codec.
Only generated files under .pio are written; codec/upstream sources are read only.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import shutil
import subprocess
from s3_codec_sources import transform

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "lib/MbelibNeo"
OUT = ROOT / ".pio/rx-concealment-tests"

def main():
    cc = os.environ.get("CC") or shutil.which("clang") or r"C:\Program Files\LLVM\bin\clang.exe"
    cxx = os.environ.get("CXX") or str(Path(cc).with_name("clang++.exe" if os.name == "nt" else "clang++"))
    OUT.mkdir(parents=True, exist_ok=True)
    flags = ["-O3", "-fno-builtin-sinf", "-fno-builtin-cosf", "-D_USE_MATH_DEFINES",
             "-DMBE_STATIC=1", "-DPFFFT_STATIC_DEFINE", "-DPFFFT_SIMD_DISABLE=1",
             "-D_CRT_SECURE_NO_WARNINGS", "-I"+str(LIB/"include"),
             "-I"+str(LIB/"src/internal"), "-I"+str(LIB/"pffft/include/pffft")]
    def compile_source(source):
        generated = OUT/source.name
        generated.write_text(transform(source, source.read_text(encoding="utf-8")), encoding="utf-8")
        obj = generated.with_suffix(".obj")
        subprocess.run([cc, *flags, "-c", str(generated), "-o", str(obj)], check=True)
        return str(obj)
    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_source, sorted((LIB/"src").rglob("*.c"))))
    exe = OUT/"rx-concealment.exe"
    subprocess.run([cxx, *flags, "-std=c++17", "-I"+str(ROOT/"src"),
                    str(ROOT/"tests/rx_concealment_codec_tests.cpp"),
                    str(ROOT/"src/dstar_codec.cpp"), *objects, "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    main()
