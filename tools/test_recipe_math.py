"""Exercise production UE-independent matching against deliberately ambiguous recipes."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="uebridge-recipes-") as directory:
    executable=pathlib.Path(directory)/"recipes"
    subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-pedantic","-I",str(root/"unreal/UEBridge/Source/UEBridge"),str(root/"bridge/tests/recipe_math_test.cpp"),"-o",str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
