"""Run portable count-conservation cases against production vanilla QUICK_CRAFT math."""
import pathlib
import shutil
import subprocess
import tempfile


def main():
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("C++ compiler required for native inventory distribution checks")
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "bridge/tests/native_inventory_math_test.cpp"
    with tempfile.TemporaryDirectory(prefix="bridge-inventory-math-") as directory:
        executable = pathlib.Path(directory) / "inventory-test"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print("Vanilla inventory count-conservation checks passed")


if __name__ == "__main__":
    main()
