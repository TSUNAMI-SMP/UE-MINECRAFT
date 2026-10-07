"""Files that must sit beside UEBridge.uproject in both downloadable bundles."""

UE_LAUNCHERS = (
    "Build-UEBridge.cmd", "Build-UEBridge.ps1",
    "Launch-UEBridge-GPU.cmd", "Launch-UEBridge-GPU.ps1",
    "Play-Native.cmd", "Play-Native.ps1",
)

UE_PYTHON_HELPERS = (
    "setup_world_bridge.py",
    "import_minecraft_textures.py",
    "import_minecraft_player.py",
    "import_minecraft_mobs.py",
    "import_minecraft_items.py",
    "setup_vanilla_effects.py",
    "setup_bridge_rendering.py",
    "bridge_lighting_materials.py",
    "import_minecraft_atlas.py",
    "import_native_play.py",
    "native_world_format.py",
    "import_minecraft_ui.py",
    "import_minecraft_sounds.py",
    "setup_native_explosion.py",
)


def require_ue_package_files(root):
    """Fail before opening a ZIP if either bundle would miss a runtime helper."""
    files = [root / "tools" / name for name in UE_PYTHON_HELPERS]
    files.extend(root / "unreal/UEBridge" / name for name in UE_LAUNCHERS)
    missing = [str(path.relative_to(root)) for path in files if not path.is_file()]
    if missing:
        raise SystemExit("Missing required UE package files: " + ", ".join(missing))
