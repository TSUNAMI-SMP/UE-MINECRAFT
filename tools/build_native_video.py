#!/usr/bin/env python3
"""Cross-build the self-contained Windows x64 JNI video bridge using MinGW.

On Windows, native/video-gpu/Build-NativeVideo.cmd also supports MSVC.
The resulting DLL imports only Windows system libraries (no external VC/MinGW runtime).
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--toolchain-root", type=Path, help="Extracted MinGW package root (contains usr/bin)")
parser.add_argument("--jdk", type=Path, default=Path(os.environ.get("JAVA_HOME", "/workspace/toolchains/jdk-21.0.12.1")))
args = parser.parse_args()
compiler = shutil.which("x86_64-w64-mingw32-g++") or shutil.which("x86_64-w64-mingw32-g++-posix")
extra = []
if args.toolchain_root:
    compiler = str(args.toolchain_root / "usr/bin/x86_64-w64-mingw32-g++-posix")
    extra = ["-I" + str(args.toolchain_root / "usr/x86_64-w64-mingw32/include"),
             "-L" + str(args.toolchain_root / "usr/x86_64-w64-mingw32/lib")]
if not compiler or not Path(compiler).is_file():
    raise SystemExit("Install/extract MinGW x86_64 POSIX C++ compiler or use the MSVC .cmd helper on Windows")
if not (args.jdk / "include/jni.h").is_file():
    raise SystemExit("--jdk must point to a JDK with include/jni.h")
output = ROOT / "minecraft-mod/src/client/resources/native/win64/uebridge_gpu.dll"
output.parent.mkdir(parents=True, exist_ok=True)
temporary = output.with_suffix(".tmp.dll")
# Also compile the UE producer's native portion; this does NOT compile UE headers or the UE module.
check = output.parent / "producer-check.obj"
try:
    subprocess.run([compiler, "-std=c++17", "-O2", "-s", "-static", "-static-libgcc", "-static-libstdc++",
                    *extra, "-I" + str(args.jdk / "include"), "-I" + str(ROOT / "native/video-gpu"),
                    "-shared", "-Wl,--no-insert-timestamp", str(ROOT / "native/video-gpu/uebridge_gpu.cpp"), "-o", str(temporary),
                    "-ld3d11", "-ldxgi", "-lopengl32"], check=True)
    subprocess.run([compiler, "-std=c++17", "-O2", "-DUEBRIDGE_NATIVE_CHECK", *extra, "-c",
                    str(ROOT / "unreal/UEBridge/Source/UEBridge/BridgeSharedGpu.cpp"), "-o", str(check)], check=True)
    temporary.replace(output)
finally:
    check.unlink(missing_ok=True)
    temporary.unlink(missing_ok=True)
print(output.relative_to(ROOT), output.stat().st_size, "bytes; SHA256", hashlib.sha256(output.read_bytes()).hexdigest())
