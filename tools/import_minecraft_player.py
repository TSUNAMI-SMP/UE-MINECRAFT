"""Run locally in UE Python: import_minecraft_player('C:/.../player-.../manifest.json').

Creates hash-named assets from the current user's exported skin. No network downloads,
copyrighted assets in the repository, or replacement of the user's saved level.
"""
import hashlib
import json
import pathlib
import re
import struct
import zlib


def load_player_manifest(filename):
    """Validate the complete local skin export before changing any UE asset."""
    path = pathlib.Path(filename).expanduser().resolve()
    if path.stat().st_size > 16 * 1024:
        raise ValueError("Player manifest too large")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("kind") != "player" or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise ValueError("Unsupported player manifest")
    skin, player = manifest.get("skin"), manifest.get("player")
    if not isinstance(skin, dict) or not isinstance(player, dict):
        raise ValueError("Missing skin/player metadata")
    if skin.get("model") not in ("classic", "slim") or type(skin.get("width")) is not int or type(skin.get("height")) is not int or (skin["width"], skin["height"]) != (64, 64):
        raise ValueError("Use a normalized 64x64 classic/slim Minecraft skin")
    if not isinstance(player.get("name"), str) or not 1 <= len(player["name"]) <= 64:
        raise ValueError("Invalid player name")
    if not isinstance(player.get("uuid"), str) or not re.fullmatch(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}", player["uuid"]):
        raise ValueError("Invalid player UUID")
    if not isinstance(skin.get("file"), str) or not isinstance(skin.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", skin["sha256"]):
        raise ValueError("Invalid skin path/checksum")
    relative = pathlib.PurePosixPath(skin["file"])
    if relative.is_absolute() or ".." in relative.parts or "\\" in skin["file"] or ":" in skin["file"]:
        raise ValueError("Skin path escapes export")
    source = (path.parent / relative).resolve()
    if not source.is_relative_to(path.parent) or not source.is_file() or source.stat().st_size > 1024 * 1024:
        raise ValueError("Missing/oversized/escaping skin")
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != skin["sha256"] or not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Skin checksum/PNG mismatch")
    cursor, dimensions, ended = 8, None, False
    while cursor + 12 <= len(data):
        length = struct.unpack_from(">I", data, cursor)[0]
        kind = data[cursor + 4:cursor + 8]
        end = cursor + 12 + length
        if end > len(data) or length > 1024 * 1024:
            raise ValueError("Truncated/oversized skin PNG")
        if zlib.crc32(data[cursor + 4:cursor + 8 + length]) & 0xffffffff != struct.unpack_from(">I", data, cursor + 8 + length)[0]:
            raise ValueError("Skin PNG checksum mismatch")
        if dimensions is None:
            if kind != b"IHDR" or length != 13:
                raise ValueError("Skin PNG header missing")
            dimensions = struct.unpack_from(">II", data, cursor + 8)
        if kind == b"IEND":
            if length != 0 or end != len(data):
                raise ValueError("Invalid skin PNG end")
            ended = True
            break
        cursor = end
    if dimensions != (64, 64) or not ended:
        raise ValueError("Skin must be a complete 64x64 PNG")
    skin["source"] = str(source)
    return manifest


def import_minecraft_player(filename):
    import unreal
    manifest = load_player_manifest(filename)
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / "UEBridge.uproject").is_file() or not (project / "Source/UEBridge/BridgePlayerAppearance.h").is_file():
        raise RuntimeError("Use the built, updated UEBridge project")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play before importing your player skin")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save your current level before importing your player skin")
    appearance_class = getattr(unreal, "BridgePlayerAppearance", None)
    receiver_class = getattr(unreal, "BridgeReceiver", None)
    if appearance_class is None or receiver_class is None:
        raise RuntimeError("Build the updated UEBridge before importing your player skin")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, receiver_class)]
    if len(receivers) != 1:
        raise RuntimeError("The current saved level must have exactly one BridgeReceiver")
    effects_script = project / "setup_vanilla_effects.py"
    if not effects_script.is_file() or not (project / 'bridge_lighting_materials.py').is_file():
        raise RuntimeError("Copy setup_vanilla_effects.py and bridge_lighting_materials.py next to UEBridge.uproject before importing your skin")
    assets, tools = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools()
    editing = unreal.MaterialEditingLibrary
    root = "/Game/Bridge/Minecraft/Player"
    digest = manifest["skin"]["sha256"]
    texture_name = "T_PlayerSkin_" + digest[:16]
    texture_path = root + "/" + texture_name
    texture = unreal.load_asset(texture_path) if assets.does_asset_exist(texture_path) else None
    if texture is None:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", manifest["skin"]["source"])
        task.set_editor_property("destination_path", root)
        task.set_editor_property("destination_name", texture_name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("save", True)
        tools.import_asset_tasks([task])
        texture = unreal.load_asset(texture_path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError("Cannot import player skin texture")
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("srgb", True)
    if not assets.save_loaded_asset(texture, False):
        raise RuntimeError("Cannot save player skin texture")
    parent_path = root + "/M_PlayerSkin_v1"
    parent = unreal.load_asset(parent_path) if assets.does_asset_exist(parent_path) else None
    if parent is None:
        parent = tools.create_asset("M_PlayerSkin_v1", root, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(parent, unreal.Material):
        raise RuntimeError("Player skin master path is occupied by a different asset type")
    # Repair only our generated master on every import, including a partial prior failure.
    # Existing hash-named textures/instances and unrelated materials are retained.
    editing.delete_all_material_expressions(parent)
    parent.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    parent.set_editor_property("two_sided", True)
    parent.set_editor_property("opacity_mask_clip_value", 0.1)
    sample = editing.create_material_expression(parent, unreal.MaterialExpressionTextureSampleParameter2D, -250, 0)
    if sample is None:
        raise RuntimeError("Cannot create skin texture parameter")
    sample.set_editor_property("parameter_name", "SkinTexture")
    sample.set_editor_property("texture", texture)
    import runpy
    helper = project / 'bridge_lighting_materials.py'
    if not helper.is_file():
        raise RuntimeError('Copy bridge_lighting_materials.py next to UEBridge.uproject first')
    runpy.run_path(str(helper))['wire_vanilla_lighting'](unreal, editing, parent, sample, use_vertex=False)
    if not editing.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError("Cannot connect skin color/outer-layer alpha")
    roughness = editing.create_material_expression(parent, unreal.MaterialExpressionConstant, -250, 180)
    roughness.set_editor_property("r", 0.85)
    if not editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS):
        raise RuntimeError("Cannot connect skin roughness")
    editing.recompile_material(parent)
    if not assets.save_loaded_asset(parent, False):
        raise RuntimeError("Cannot save player skin material")
    if "SkinTexture" not in {str(name) for name in editing.get_texture_parameter_names(parent)}:
        raise RuntimeError("Skin master material is incomplete")
    material_name = "MI_PlayerSkin_" + digest[:16]
    material_path = root + "/" + material_name
    material = unreal.load_asset(material_path) if assets.does_asset_exist(material_path) else None
    if material is None:
        material = tools.create_asset(material_name, root, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if not isinstance(material, unreal.MaterialInstanceConstant):
        raise RuntimeError("Cannot create skin material instance")
    editing.set_material_instance_parent(material, parent)
    material.set_editor_property("texture_parameter_values", [unreal.TextureParameterValue(
        parameter_info=unreal.MaterialParameterInfo(name="SkinTexture"), parameter_value=texture)])
    editing.update_material_instance(material)
    if editing.get_material_instance_texture_parameter_value(material, "SkinTexture") != texture or not assets.save_loaded_asset(material, False):
        raise RuntimeError("Skin texture override/save failed")
    appearance_name = "DA_PlayerSkin_" + digest[:16] + ("_Slim" if manifest["skin"]["model"] == "slim" else "_Classic")
    appearance_path = root + "/" + appearance_name
    appearance = unreal.load_asset(appearance_path) if assets.does_asset_exist(appearance_path) else None
    if appearance is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", appearance_class)
        appearance = tools.create_asset(appearance_name, root, appearance_class, factory)
    if not isinstance(appearance, appearance_class):
        raise RuntimeError("Cannot create player appearance asset")
    with unreal.ScopedEditorTransaction("Assign Minecraft player skin"):
        appearance.set_editor_property("skin_material", material)
        appearance.set_editor_property("is_slim", manifest["skin"]["model"] == "slim")
        appearance.set_editor_property("player_name", manifest["player"]["name"])
        appearance.set_editor_property("skin_hash", digest)
        if not assets.save_loaded_asset(appearance, False):
            raise RuntimeError("Cannot save player appearance")
        receivers[0].set_editor_property("player_appearance", appearance)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Cannot save current level with player skin")
    # The source-only update copies these scripts next to UEBridge.uproject.
    import runpy
    runpy.run_path(str(effects_script))["setup_vanilla_effects"]()
    unreal.log("Minecraft player ready: " + manifest["player"]["name"] + " / " + manifest["skin"]["model"] + ". Start Play; your configured perspective key controls first/rear/front view.")


def _latest_export(game_dir, prefix, command):
    """Resolve a finished export without asking users to type timestamped paths."""
    export_root = pathlib.Path(game_dir).expanduser().resolve() / "uebridge-export"
    candidates = [path for path in export_root.glob(prefix + "-*/manifest.json") if path.is_file()]
    if not candidates:
        raise RuntimeError("No " + prefix + " export found in " + str(export_root) + ". Run " + command + " in Minecraft and wait for its completion message.")
    return max(candidates, key=lambda path: path.stat().st_mtime_ns)


def setup_minecraft_visuals(game_dir):
    """Import newest local blocks, skin, items and optional mobs; preserve saved content."""
    import runpy
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    textures_script = project / "import_minecraft_textures.py"
    effects_script = project / "setup_vanilla_effects.py"
    rendering_script = project / "setup_bridge_rendering.py"
    lighting_script = project / 'bridge_lighting_materials.py'
    atlas_script = project / 'import_minecraft_atlas.py'
    if not all(p.is_file() for p in (textures_script, effects_script, rendering_script, lighting_script, atlas_script)):
        raise RuntimeError("Copy all Python helpers from UEBridge-update-0.11.0.zip next to UEBridge.uproject first")
    textures_manifest = _latest_export(game_dir, "textures", "/uebridge textures export")
    player_manifest = _latest_export(game_dir, "player", "/uebridge player export")
    textures_functions = runpy.run_path(str(textures_script))
    mobs_script = project / "import_minecraft_mobs.py"
    mob_exports = list((pathlib.Path(game_dir).expanduser().resolve() / "uebridge-export").glob("mobs-*/manifest.json"))
    mobs_functions = None
    mobs_manifest = None
    if mob_exports:
        if not mobs_script.is_file():
            raise RuntimeError("Copy import_minecraft_mobs.py next to UEBridge.uproject first")
        mobs_manifest = max(mob_exports, key=lambda path: path.stat().st_mtime_ns)
        mobs_functions = runpy.run_path(str(mobs_script))
        mobs_functions["load_mob_manifest"](str(mobs_manifest))
    # An incomplete/bad player export must not leave a half-updated texture palette.
    textures_functions["load_texture_manifest"](str(textures_manifest))
    load_player_manifest(str(player_manifest))
    unreal.log("Minecraft texture export: " + str(textures_manifest))
    unreal.log("Minecraft player export: " + str(player_manifest))
    items_manifest = _latest_export(game_dir, "items", "/uebridge items export")
    items_script = project / "import_minecraft_items.py"
    if not items_script.is_file():
        raise RuntimeError("Copy import_minecraft_items.py next to UEBridge.uproject first")
    items_functions = runpy.run_path(str(items_script))
    items_functions["load_item_manifest"](str(items_manifest))
    textures_functions["import_minecraft_textures"](str(textures_manifest))
    import_minecraft_player(str(player_manifest))
    items_functions["import_minecraft_items"](str(items_manifest))
    if mobs_functions is not None:
        mobs_functions["import_minecraft_mobs"](str(mobs_manifest))
    else:
        unreal.log("No local mob export. Run /uebridge mobs export, then import its spawn-egg templates before starting UE control.")
    runpy.run_path(str(rendering_script))["setup_bridge_rendering"]()
    unreal.log("Minecraft visuals ready. Block textures, your skin, and vanilla particles are assigned to the saved current level.")
