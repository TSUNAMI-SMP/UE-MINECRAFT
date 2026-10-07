#!/usr/bin/env python3
"""Make a source-only UE update with local build helper, preserving existing levels."""
import hashlib
import pathlib
import re
import subprocess
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
if subprocess.run(["git", "diff", "--quiet"], cwd=root).returncode != 0:
    raise SystemExit("Stage intended source changes before packaging")
version = re.search(r"^mod_version=([0-9]+\.[0-9]+\.[0-9]+)$", (root / "minecraft-mod/gradle.properties").read_text(), re.M).group(1)
tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
output = root / f"downloads/UEBridge-update-{version}.zip"
with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED) as archive:
    prefix = "unreal/UEBridge/"
    for path in sorted(tracked):
        if path.startswith(prefix + "Source/") or path in (prefix + "Build-UEBridge.cmd", prefix + "Build-UEBridge.ps1"):
            archive.write(root / path, path[len(prefix):])
    archive.write(root / "tools/setup_world_bridge.py", "setup_world_bridge.py")
    if (root / "tools/import_minecraft_textures.py").is_file():
        archive.write(root / "tools/import_minecraft_textures.py", "import_minecraft_textures.py")
    for helper in ("import_minecraft_player.py", "import_minecraft_mobs.py", "import_minecraft_items.py", "setup_vanilla_effects.py", "setup_bridge_rendering.py"):
        if (root / "tools" / helper).is_file():
            archive.write(root / "tools" / helper, helper)
    archive.write(root / f"docs/UPGRADE_{version}.md", "UPDATE_INSTRUCTIONS.md")
with zipfile.ZipFile(output) as archive:
    if archive.testzip() is not None:
        raise SystemExit("UE update CRC check failed")
digest = hashlib.sha256(output.read_bytes()).hexdigest()
output.with_suffix(".zip.sha256").write_text(digest + "  " + output.name + "\n")
print(output.name, "SHA-256:", digest)
