"""Import nearby mobs captured from the user's active Minecraft models/resources locally.

UE Python: setup_minecraft_mobs('C:/UEBridgeTest/MC-Test') after /uebridge mobs export.
Only generated assets and the current level's single BridgeReceiver assignment are changed.
"""
import hashlib
import json
import math
import pathlib
import re
import struct
import zlib


def _number(value, limit=4096):
    if type(value) not in (int, float) or not math.isfinite(value) or abs(value) > limit:
        raise ValueError("Invalid/nonfinite/out-of-range mob geometry")
    return value


def _array(value, size, limit=4096):
    if not isinstance(value, list) or len(value) != size:
        raise ValueError("Invalid mob vector/transform length")
    return [_number(v, limit) for v in value]


def _transform(value):
    result = _array(value, 9)
    if any(abs(v) > 128 for v in result[3:6]) or any(not 0 <= v <= 64 for v in result[6:]):
        raise ValueError("Invalid mob part rotation/scale")
    return result


def _png(filename, expected_hash, width, height):
    if not filename.is_file() or filename.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("Missing/oversized mob texture")
    data = filename.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected_hash or not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Mob texture checksum/PNG mismatch")
    cursor, dimensions, ended = 8, None, False
    while cursor + 12 <= len(data):
        length = struct.unpack_from(">I", data, cursor)[0]
        kind = data[cursor + 4:cursor + 8]
        end = cursor + 12 + length
        if end > len(data) or length > 4 * 1024 * 1024:
            raise ValueError("Truncated mob PNG")
        if zlib.crc32(data[cursor + 4:cursor + 8 + length]) & 0xffffffff != struct.unpack_from(">I", data, cursor + 8 + length)[0]:
            raise ValueError("Mob PNG chunk checksum mismatch")
        if dimensions is None:
            if kind != b"IHDR" or length != 13:
                raise ValueError("Missing mob PNG header")
            dimensions = struct.unpack_from(">II", data, cursor + 8)
        if kind == b"IEND":
            if length or end != len(data):
                raise ValueError("Invalid mob PNG end")
            ended = True
            break
        cursor = end
    if dimensions != (width, height) or not ended:
        raise ValueError("Mob texture dimensions/end mismatch")


def load_mob_manifest(filename):
    """Validate all assets/geometry before loading Unreal or modifying any asset."""
    path = pathlib.Path(filename).expanduser().resolve()
    if not path.is_file() or path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("Missing/oversized mob manifest")
    raw = path.read_bytes()
    manifest = json.loads(raw)
    if not isinstance(manifest, dict) or manifest.get("kind") != "mobs" or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise ValueError("Unsupported mob manifest")
    appearances = manifest.get("appearances")
    if not isinstance(appearances, dict) or not 1 <= len(appearances) <= 128:
        raise ValueError("Invalid mob appearance count")
    vertices = 0
    texture_bytes = 0
    validated_textures = set()
    for key, appearance in appearances.items():
        if not re.fullmatch(r"[0-9a-f]{64}", key) or not isinstance(appearance, dict):
            raise ValueError("Invalid appearance key")
        if not isinstance(appearance.get("type"), str) or not re.fullmatch(r"minecraft:[a-z0-9_]+", appearance["type"]):
            raise ValueError("Only vanilla mob registry IDs are supported")
        scale = _array(appearance.get("rendererScale"), 3, 64)
        if any(v <= 0 for v in scale):
            raise ValueError("Mob renderer scale must be positive")
        _array(appearance.get("rendererOffset"), 3, 64)
        digest, relative = appearance.get("textureHash"), appearance.get("texture")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest) or not isinstance(relative, str):
            raise ValueError("Invalid mob texture metadata")
        pure = pathlib.PurePosixPath(relative)
        if pure.is_absolute() or ".." in pure.parts or "\\" in relative or ":" in relative:
            raise ValueError("Mob texture escapes export")
        source = (path.parent / pure).resolve()
        if not source.is_relative_to(path.parent):
            raise ValueError("Mob texture symlink escapes export")
        width, height = appearance.get("textureWidth"), appearance.get("textureHeight")
        if type(width) is not int or type(height) is not int or not 1 <= width <= 2048 or not 1 <= height <= 2048:
            raise ValueError("Invalid mob texture size")
        if (source, digest) not in validated_textures:
            _png(source, digest, width, height)
            texture_bytes += source.stat().st_size
            validated_textures.add((source, digest))
        if texture_bytes > 128 * 1024 * 1024:
            raise ValueError("Mob texture budget exceeded")
        appearance["source"] = str(source)
        parts, frames = appearance.get("parts"), appearance.get("walkFrames")
        if not isinstance(parts, list) or not 1 <= len(parts) <= 256 or not isinstance(frames, list) or len(frames) != 16:
            raise ValueError("Invalid mob parts/walk frame count")
        for index, part in enumerate(parts):
            if not isinstance(part, dict) or not isinstance(part.get("name"), str) or not 1 <= len(part["name"]) <= 128:
                raise ValueError("Invalid mob part")
            parent = part.get("parent")
            if type(parent) is not int or parent < -1 or parent >= index:
                raise ValueError("Mob hierarchy must reference preceding parents")
            _transform(part.get("transform"))
            quads = part.get("quads")
            if not isinstance(quads, list) or len(quads) > 4096:
                raise ValueError("Mob part geometry budget exceeded")
            for quad in quads:
                if not isinstance(quad, list) or len(quad) != 4:
                    raise ValueError("Mob geometry must contain quads")
                for vertex in quad:
                    _array(vertex, 5)
                    if any(not -16 <= uv <= 16 for uv in vertex[3:]):
                        raise ValueError("Invalid mob UV")
                vertices += 4
            if vertices > 500000:
                raise ValueError("Mob export geometry budget exceeded")
        for frame in frames:
            if not isinstance(frame, list) or len(frame) != len(parts):
                raise ValueError("Mob pose hierarchy mismatch")
            for transform in frame:
                _transform(transform)
    entities = manifest.get("entities")
    if not isinstance(entities, dict) or len(entities) > 128:
        raise ValueError("Invalid captured entity map")
    for entity, key in entities.items():
        if not re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", entity) or key not in appearances:
            raise ValueError("Invalid captured entity/appearance reference")
    manifest["manifestHash"] = hashlib.sha256(raw).hexdigest()
    templates = manifest.get("templates", {})
    if not isinstance(templates, dict) or len(templates) > 128:
        raise ValueError("Invalid mob templates")
    for species, key in templates.items():
        if not isinstance(species, str) or not re.fullmatch(r"minecraft:[a-z0-9_]+", species) or not isinstance(key, str) or key not in appearances or appearances[key]["type"] != species or "stats" not in appearances[key]:
            raise ValueError("Invalid mob template appearance")
    for appearance in appearances.values():
        stats = appearance.get("stats")
        if stats is None:
            continue  # Earlier exports support imported individuals, but not new spawn eggs.
        if not isinstance(stats, dict):
            raise ValueError("Invalid mob template stats")
        for key, low, high in (("width", .1, 20), ("height", .1, 20), ("maxHealth", .1, 1000), ("speed", 0, 2), ("damage", 0, 100)):
            value = _number(stats.get(key))
            if not low <= value <= high:
                raise ValueError("Invalid mob template stat: " + key)
        if type(stats.get("hostile")) is not bool or type(stats.get("baby")) is not bool:
            raise ValueError("Invalid mob template flags")
    return manifest


def minecraft_part_transform(values):
    """Native model pixels/ZYX radians -> UE cm/quaternion/scale. Pure, testable math."""
    x, y, z, pitch, yaw, roll, sx, sy, sz = _transform(values)
    cx, cy, cz = math.cos(pitch/2), math.cos(yaw/2), math.cos(roll/2)
    ax, ay, az = math.sin(pitch/2), math.sin(yaw/2), math.sin(roll/2)
    # Native quaternion qz*qy*qx, then reflection basis C(-z,-x,-y).
    qx = ax*cy*cz - cx*ay*az
    qy = cx*ay*cz + ax*cy*az
    qz = cx*cy*az - ax*ay*cz
    qw = cx*cy*cz + ax*ay*az
    return ((-z*6.25, -x*6.25, -y*6.25), (qz, qx, qy, qw), (sz, sx, sy))


def import_minecraft_mobs(filename):
    manifest = load_mob_manifest(filename)
    import unreal
    palette_class = getattr(unreal, "BridgeMobPalette", None)
    if palette_class is None or not hasattr(unreal, "BridgeMobPart") or not hasattr(unreal, "BridgeMobAppearance"):
        raise RuntimeError("Build the updated UEBridge before importing mobs")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play before importing mobs")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save the current level before importing mobs")
    receivers = [actor for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(actor, unreal.BridgeReceiver)]
    if len(receivers) != 1:
        raise RuntimeError("The current saved level must have exactly one BridgeReceiver")
    assets, tools = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools()
    editing = unreal.MaterialEditingLibrary
    root = "/Game/Bridge/Minecraft/Mobs"
    parent = unreal.load_asset(root + "/M_MinecraftMob_v1")
    if parent is None:
        parent = tools.create_asset("M_MinecraftMob_v1", root, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(parent, unreal.Material):
        raise RuntimeError("Mob master material path is occupied")
    editing.delete_all_material_expressions(parent)
    parent.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    parent.set_editor_property("two_sided", True)
    parent.set_editor_property("opacity_mask_clip_value", 0.1)
    first = next(iter(manifest["appearances"].values()))
    imported_textures = {}
    def texture_for(appearance):
        digest = appearance["textureHash"]
        if digest in imported_textures:
            return imported_textures[digest]
        name = "T_Mob_" + digest[:20]
        texture = unreal.load_asset(root + "/" + name)
        if texture is None:
            task = unreal.AssetImportTask()
            for key, value in {"filename": appearance["source"], "destination_path": root, "destination_name": name, "automated": True, "replace_existing": False, "save": True}.items():
                task.set_editor_property(key, value)
            tools.import_asset_tasks([task])
            texture = unreal.load_asset(root + "/" + name)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("Cannot import mob texture")
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        texture.set_editor_property("srgb", True)
        if not assets.save_loaded_asset(texture, False):
            raise RuntimeError("Cannot save mob texture")
        imported_textures[digest] = texture
        return texture
    sample = editing.create_material_expression(parent, unreal.MaterialExpressionTextureSampleParameter2D, -250, 0)
    if sample is None:
        raise RuntimeError("Cannot create mob texture parameter")
    sample.set_editor_property("parameter_name", "MobTexture")
    sample.set_editor_property("texture", texture_for(first))
    if not editing.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR) or not editing.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError("Cannot connect mob texture color/alpha")
    roughness = editing.create_material_expression(parent, unreal.MaterialExpressionConstant, -250, 180)
    roughness.set_editor_property("r", 0.85)
    if not editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS):
        raise RuntimeError("Cannot connect mob roughness")
    editing.recompile_material(parent)
    if not assets.save_loaded_asset(parent, False):
        raise RuntimeError("Cannot save mob master material")
    def transform_for(values):
        position, rotation, scale = minecraft_part_transform(values)
        return unreal.Transform(location=unreal.Vector(*position), rotation=unreal.Quat(*rotation), scale=unreal.Vector(*scale))
    appearances = []
    for key, source in manifest["appearances"].items():
        material_name = "MI_Mob_" + source["textureHash"][:20]
        material = unreal.load_asset(root + "/" + material_name)
        if material is None:
            material = tools.create_asset(material_name, root, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if not isinstance(material, unreal.MaterialInstanceConstant):
            raise RuntimeError("Cannot create mob material instance")
        editing.set_material_instance_parent(material, parent)
        texture = texture_for(source)
        material.set_editor_property("texture_parameter_values", [unreal.TextureParameterValue(parameter_info=unreal.MaterialParameterInfo(name="MobTexture"), parameter_value=texture)])
        editing.update_material_instance(material)
        if editing.get_material_instance_texture_parameter_value(material, "MobTexture") != texture or not assets.save_loaded_asset(material, False):
            raise RuntimeError("Cannot set/save mob texture override")
        parts = []
        for index, part in enumerate(source["parts"]):
            entry = unreal.BridgeMobPart()
            entry.set_editor_property("name", part["name"])
            entry.set_editor_property("parent", part["parent"])
            entry.set_editor_property("rest", transform_for(part["transform"]))
            entry.set_editor_property("walk_frames", [transform_for(frame[index]) for frame in source["walkFrames"]])
            vertices = [vertex for quad in part["quads"] for vertex in quad]
            entry.set_editor_property("vertices", [unreal.Vector(-v[2]*6.25, -v[0]*6.25, -v[1]*6.25) for v in vertices])
            entry.set_editor_property("texcoords", [unreal.Vector2D(v[3], v[4]) for v in vertices])
            parts.append(entry)
        appearance = unreal.BridgeMobAppearance()
        if "stats" in source:
            for field in ("width", "height", "speed", "damage", "hostile", "baby"):
                appearance.set_editor_property(field, source["stats"][field])
            appearance.set_editor_property("max_health", source["stats"]["maxHealth"])
        appearance.set_editor_property("key", key)
        appearance.set_editor_property("type", source["type"])
        appearance.set_editor_property("material", material)
        appearance.set_editor_property("render_scale", unreal.Vector(source["rendererScale"][2], source["rendererScale"][0], source["rendererScale"][1]))
        appearance.set_editor_property("render_offset", unreal.Vector(-source["rendererOffset"][2]*100, -source["rendererOffset"][0]*100, -source["rendererOffset"][1]*100))
        appearance.set_editor_property("parts", parts)
        appearances.append(appearance)
    palette_name = "DA_Mobs_" + manifest["manifestHash"][:20]
    palette = unreal.load_asset(root + "/" + palette_name)
    if palette is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", palette_class)
        palette = tools.create_asset(palette_name, root, palette_class, factory)
    if not isinstance(palette, palette_class):
        raise RuntimeError("Cannot create mob palette")
    with unreal.ScopedEditorTransaction("Assign Minecraft mob palette"):
        palette.set_editor_property("appearances", appearances)
        palette.set_editor_property("templates", manifest.get("templates", {}))
        if not assets.save_loaded_asset(palette, False):
            raise RuntimeError("Cannot save mob palette")
        receivers[0].set_editor_property("mob_palette", palette)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Cannot save level with mob palette")
    unreal.log("Minecraft mob body models ready: " + str(len(appearances)) + "; ground movement/AI; features and species-specific behavior are not implemented")


def setup_minecraft_mobs(game_dir):
    root = pathlib.Path(game_dir).expanduser().resolve() / "uebridge-export"
    exports = sorted(p / "manifest.json" for p in root.glob("mobs-*") if (p / "manifest.json").is_file())
    if not exports:
        raise FileNotFoundError("Run /uebridge mobs export near the desired mobs in Minecraft first")
    return import_minecraft_mobs(str(exports[-1]))
