"""Check production native math against independently stepped Minecraft rules."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='uebridge-combat-movement-') as tmp:
    output = pathlib.Path(tmp) / 'combat-movement'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-I', str(root / 'unreal/UEBridge/Source/UEBridge'), str(root / 'bridge/tests/combat_movement_math_test.cpp'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True)
