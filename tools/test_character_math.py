#!/usr/bin/env python3
"""Compile and run the production character math without requiring UE.

This checks scalar/rigid-transform math only. It does not build the UE module,
exercise CharacterMovement, or confirm the displayed skin/item geometry.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=shutil.which("c++") or shutil.which("g++"))
    args = parser.parse_args()
    if not args.compiler:
        parser.error("A C++17 compiler is required; pass --compiler /path/to/c++")
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="uebridge-character-math-") as temporary:
        executable = Path(temporary) / "character_math_test"
        subprocess.run([
            args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(root / "unreal/UEBridge/Source/UEBridge"),
            str(root / "bridge/tests/character_math_test.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
