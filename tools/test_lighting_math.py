"""Exercise production voxel propagation and lightmap math without UE rendering."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='uebridge-lighting-') as tmp:
    executable = pathlib.Path(tmp) / 'lighting'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-I',
                    str(root / 'unreal/UEBridge/Source/UEBridge'),
                    str(root / 'bridge/tests/lighting_math_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
