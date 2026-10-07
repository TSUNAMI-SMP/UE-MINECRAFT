"""Exercise production native-shape edge extraction without UE; no rendering claim."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='uebridge-outline-') as tmp:
    executable = pathlib.Path(tmp) / 'outline'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-I', str(root / 'unreal/UEBridge/Source/UEBridge'), str(root / 'bridge/tests/outline_math_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
