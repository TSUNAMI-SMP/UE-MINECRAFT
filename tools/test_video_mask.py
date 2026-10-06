#!/usr/bin/env python3
"""Compile and verify the production video opacity encoder without building Unreal Engine."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which("c++") or shutil.which("g++")
if not compiler:
    raise SystemExit("A C++17 compiler is required")
with tempfile.TemporaryDirectory(prefix="uebridge-video-mask-") as temporary:
    executable = Path(temporary) / "video_mask_test"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I",
                    str(root / "unreal/UEBridge/Source/UEBridge"), str(root / "bridge/tests/video_mask_test.cpp"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
