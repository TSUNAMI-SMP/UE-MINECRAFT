#!/usr/bin/env python3
"""Package source + current verified MOD; preserve all older download bundles."""
import hashlib
import pathlib
import re
import subprocess
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
if subprocess.run(["git", "diff", "--quiet"], cwd=root).returncode != 0:
    raise SystemExit("Stage the intended source changes first; do not package unstaged modifications")
properties = (root / "minecraft-mod/gradle.properties").read_text()
match = re.search(r"^mod_version=([0-9]+\.[0-9]+\.[0-9]+)$", properties, re.M)
if not match: raise SystemExit("Missing mod version")
version = match.group(1)
jar = root / f"minecraft-mod/build/libs/minecraft-ue-bridge-{version}.jar"
if not jar.is_file(): raise SystemExit("Build the current MOD before packaging")
files = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
output = root / f"downloads/UE-Minecraft-MVP-{version}.zip"
output.parent.mkdir(exist_ok=True)
# Refuse to silently replace a previously published/downloaded bundle.
with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED) as archive:
    for relative in sorted(set(files)):
        if relative and not relative.startswith("downloads/"):
            archive.write(root / relative, "UE-MINECRAFT/" + relative)
    archive.write(jar, "UE-MINECRAFT/artifacts/" + jar.name)
    # Local UE Python helpers must also sit beside the .uproject in the full download.
    for helper in ("setup_world_bridge.py", "import_minecraft_textures.py", "import_minecraft_player.py", "import_minecraft_mobs.py", "import_minecraft_items.py", "setup_vanilla_effects.py", "setup_bridge_rendering.py", "bridge_lighting_materials.py", "import_minecraft_atlas.py"):
        archive.write(root / "tools" / helper, "UE-MINECRAFT/unreal/UEBridge/" + helper)
    digest = hashlib.sha256(jar.read_bytes()).hexdigest()
    archive.writestr("UE-MINECRAFT/artifacts/SHA256SUMS.txt", digest + "  " + jar.name + "\n")
with zipfile.ZipFile(output) as archive:
    if archive.testzip() is not None: raise SystemExit("Bundle CRC check failed")
digest = hashlib.sha256(output.read_bytes()).hexdigest()
output.with_suffix(".zip.sha256").write_text(digest + "  " + output.name + "\n")
print(output.name, "SHA-256:", digest)
