#!/usr/bin/env python3
"""Make a source-only UE update with local build helper, preserving existing levels."""
import argparse
import hashlib
import pathlib
import re
import subprocess
import zipfile
from ue_package_files import UE_LAUNCHERS, UE_PYTHON_HELPERS, require_ue_package_files

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--ue-version", help="UE-only update version; does not change or package the MOD")
args = parser.parse_args()
if subprocess.run(["git", "diff", "--quiet"], cwd=root).returncode != 0:
    raise SystemExit("Stage intended source changes before packaging")
match = re.search(r"^mod_version=([0-9]+\.[0-9]+\.[0-9]+)$", (root / "minecraft-mod/gradle.properties").read_text(), re.M)
if not match:
    raise SystemExit("Missing mod version")
version = args.ue_version or match.group(1)
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
    raise SystemExit("UE update version must be major.minor.patch")
require_ue_package_files(root)
instructions = root / f"docs/UPGRADE_{version}.md"
if not instructions.is_file():
    raise SystemExit("Missing update instructions for " + version)
tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
output = root / f"downloads/UEBridge-update-{version}.zip"
output.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED) as archive:
    prefix = "unreal/UEBridge/"
    for path in sorted(tracked):
        if path.startswith(prefix + "Source/") or path in tuple(prefix + name for name in UE_LAUNCHERS):
            archive.write(root / path, path[len(prefix):])
    for helper in UE_PYTHON_HELPERS:
        archive.write(root / "tools" / helper, helper)
    archive.write(instructions, "UPDATE_INSTRUCTIONS.md")
    archive.write(instructions, instructions.name)
    native_instructions = root / "docs/NATIVE_PLAY.md"
    if native_instructions.is_file():
        archive.write(native_instructions, "NATIVE_PLAY.md")
    cloud_instructions = root / "docs/CLOUD_SETUP.md"
    if cloud_instructions.is_file():
        archive.write(cloud_instructions, "CLOUD_SETUP.md")
with zipfile.ZipFile(output) as archive:
    if archive.testzip() is not None:
        raise SystemExit("UE update CRC check failed")
digest = hashlib.sha256(output.read_bytes()).hexdigest()
output.with_suffix(".zip.sha256").write_text(digest + "  " + output.name + "\n")
print(output.name, "SHA-256:", digest)
