#!/usr/bin/env python3
"""Check portable movement rules derived from the user-provided MC 1.21.11 reference.

Does not execute Unreal capsule sweeps or compile the Unreal module.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="uebridge-movement-parity-") as temporary:
    executable = Path(temporary) / "movement_parity"
    subprocess.run([
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-I", str(root / "unreal/UEBridge/Source/UEBridge"),
        str(root / "bridge/tests/movement_parity_math_test.cpp"), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
