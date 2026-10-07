"""Compile and exercise production terrain packing, independent of unavailable UE."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="uebridge-meshing-") as temporary:
    executable = pathlib.Path(temporary) / "meshing"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I", str(root / "unreal/UEBridge/Source/UEBridge"), str(root / "bridge/tests/meshing_math_test.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
