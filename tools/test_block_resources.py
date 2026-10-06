"""Exercise production block exporter with the installed local vanilla client JAR.

All extracted assets live in a temporary directory and are deleted after validation.
The test intentionally tries dedicated/special models too; excluded IDs are reported,
not confused with the gameplay scope in BlockGeometryCapture.
"""
import argparse
import pathlib
import subprocess
import tempfile
import os
import sys

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--jar", type=pathlib.Path)
args = parser.parse_args()
cache = pathlib.Path(os.environ.get("GRADLE_USER_HOME", "/workspace/gradle-cache"))
jars = list(cache.glob("caches/fabric-loom/1.21.11/minecraft-client.jar"))
client = args.jar or (jars[0] if jars else None)
gsons = list(cache.glob("caches/modules-2/files-2.1/com.google.code.gson/gson/*/*/gson-*.jar"))
jdks = list(pathlib.Path("/workspace/toolchains").glob("jdk-21*/bin/javac"))
javac = pathlib.Path(os.environ["JAVA_HOME"]) / "bin/javac" if os.environ.get("JAVA_HOME") else (jdks[0] if jdks else pathlib.Path("javac"))
java = str(javac).removesuffix("javac") + "java"
if client is None or not client.is_file() or not gsons:
    sys.exit("Build the MOD once to populate the local Minecraft/Gson cache, or pass --jar.")
with tempfile.TemporaryDirectory(prefix="uebridge-block-assets-") as temporary:
    output = pathlib.Path(temporary)
    subprocess.run([str(javac), "-cp", str(gsons[0]), "-d", str(output),
        str(root / "minecraft-mod/src/client/java/dev/tsunami/bridge/TextureExport.java"),
        str(root / "bridge/tests/TextureModelCoverage.java")], check=True)
    subprocess.run([java, "-cp", str(output) + os.pathsep + str(gsons[0]), "TextureModelCoverage", str(client), str(output / "assets-export")], check=True)
    sys.path.insert(0, str(root / "tools"))
    from import_minecraft_textures import load_texture_manifest
    manifest = load_texture_manifest(output / "assets-export/manifest.json")
    print("Local exported model/texture manifest validation passed:", len(manifest["blocks"]), "block IDs")
