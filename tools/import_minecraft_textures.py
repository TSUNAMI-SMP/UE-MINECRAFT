"""UE Python: exec(open('C:/.../import_minecraft_textures.py', encoding='utf-8').read()); import_minecraft_textures('C:/.../manifest.json')

Pure manifest validation can run outside UE. Import requires a saved UEBridge level.
Assets use content hashes, are reused on repeat import, and never deleted.
"""
import hashlib
import json
import pathlib
import re
import struct
import zlib


def _identifier(value, limit=160):
    if not isinstance(value, str) or len(value) > limit or not re.fullmatch(r"[a-z0-9_.-]+:[a-z0-9_./-]+", value):
        raise ValueError("Invalid resource identifier")
    if ".." in value.split(":", 1)[1].split("/"):
        raise ValueError("Invalid resource path")


def _png_dimensions(data):
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Texture is not PNG")
    cursor, dimensions, ended = 8, None, False
    while cursor + 12 <= len(data):
        length = struct.unpack_from(">I", data, cursor)[0]
        kind = data[cursor + 4:cursor + 8]
        end = cursor + 12 + length
        if end > len(data) or length > 4 * 1024 * 1024:
            raise ValueError("Truncated/oversized PNG chunk")
        expected = struct.unpack_from(">I", data, cursor + 8 + length)[0]
        if zlib.crc32(data[cursor + 4:cursor + 8 + length]) & 0xffffffff != expected:
            raise ValueError("PNG checksum mismatch")
        if dimensions is None:
            if kind != b"IHDR" or length != 13:
                raise ValueError("PNG header missing")
            dimensions = struct.unpack_from(">II", data, cursor + 8)
        if kind == b"IEND":
            if length != 0 or end != len(data):
                raise ValueError("Invalid PNG end")
            ended = True
            break
        cursor = end
    if not ended or dimensions is None:
        raise ValueError("Incomplete PNG")
    return dimensions


def load_texture_manifest(filename):
    path = pathlib.Path(filename).expanduser().resolve()
    if path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("Manifest too large")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("format") != "uebridge-block-textures" or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise ValueError("Unsupported texture manifest")
    textures, blocks = manifest.get("textures"), manifest.get("blocks")
    if not isinstance(textures, dict) or not isinstance(blocks, dict) or not textures or not blocks or len(textures) > 4096 or len(blocks) > 4096:
        raise ValueError("Empty/invalid/oversized texture palette")
    total = 0
    for name, entry in textures.items():
        _identifier(name)
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            raise ValueError("Invalid texture entry")
        relative = pathlib.PurePosixPath(entry["file"])
        if relative.is_absolute() or ".." in relative.parts or "\\" in entry["file"] or ":" in entry["file"]:
            raise ValueError("Texture path escapes export")
        source = (path.parent / relative).resolve()
        if not source.is_relative_to(path.parent) or not source.is_file() or source.stat().st_size > 4 * 1024 * 1024:
            raise ValueError("Missing/oversized/escaping texture")
        total += source.stat().st_size
        if total > 128 * 1024 * 1024:
            raise ValueError("Texture byte budget exceeded")
        for key in ("width", "height"):
            if type(entry.get(key)) is not int or not 1 <= entry[key] <= 2048:
                raise ValueError("Invalid texture dimensions")
        if not isinstance(entry.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ValueError("Invalid texture checksum")
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"] or _png_dimensions(data) != (entry["width"], entry["height"]):
            raise ValueError("Texture content/dimensions mismatch")
        entry["source"] = str(source)
    for name, entry in blocks.items():
        _identifier(name, 128)
        if not isinstance(entry, dict):
            raise ValueError("Invalid block entry")
        for face in ("top", "side", "bottom"):
            item = entry.get(face)
            if not isinstance(item, dict) or item.get("texture") not in textures or type(item.get("tint")) is not bool:
                raise ValueError("Invalid block face/texture reference")
    return manifest


def import_minecraft_textures(filename):
    import unreal
    manifest = load_texture_manifest(filename)  # Validate all files before mutating UE assets.
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / "UEBridge.uproject").is_file() or not (project / "Source/UEBridge/BridgeBlockPalette.h").is_file():
        raise RuntimeError("Use the updated UEBridge 0.4.0 project only")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play before importing textures")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save your level first and stop Play before importing")
    receiver_class = getattr(unreal, "BridgeReceiver", None)
    palette_class = getattr(unreal, "BridgeBlockPalette", None)
    if receiver_class is None or palette_class is None:
        raise RuntimeError("Build UEBridge 0.4.0 first")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, receiver_class)]
    if len(receivers) != 1:
        raise RuntimeError("The current saved level must have exactly one BridgeReceiver")
    assets, tools = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools()
    editing = unreal.MaterialEditingLibrary
    root = "/Game/Bridge/Minecraft"

    def asset_name(prefix, identifier, digest):
        label = re.sub(r"[^a-zA-Z0-9_]", "_", identifier)[:48]
        return prefix + label + "_" + digest[:16]

    imported = {}
    with unreal.ScopedSlowTask(len(manifest["textures"]) + len(manifest["blocks"]) + 1, "Import Minecraft textures") as progress:
        progress.make_dialog(True)
        for identifier, entry in manifest["textures"].items():
            if progress.should_cancel():
                raise RuntimeError("Import cancelled; existing level and palette are unchanged, generated assets are retained")
            progress.enter_progress_frame(1, identifier)
            name = asset_name("T_", identifier, entry["sha256"])
            destination = root + "/Textures"
            target = destination + "/" + name
            texture = unreal.load_asset(target) if assets.does_asset_exist(target) else None
            if texture is None:
                task = unreal.AssetImportTask()
                task.set_editor_property("filename", entry["source"])
                task.set_editor_property("destination_path", destination)
                task.set_editor_property("destination_name", name)
                task.set_editor_property("automated", True)
                task.set_editor_property("replace_existing", False)
                task.set_editor_property("save", True)
                tools.import_asset_tasks([task])
                texture = unreal.load_asset(target)
                if not isinstance(texture, unreal.Texture2D):
                    raise RuntimeError("Texture import failed: " + identifier)
                texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
                texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
                texture.set_editor_property("srgb", True)
                if not assets.save_loaded_asset(texture, False):
                    raise RuntimeError("Cannot save imported texture")
            elif not isinstance(texture, unreal.Texture2D):
                raise RuntimeError("Texture asset name is occupied by a different type")
            imported[identifier] = texture

        parent_path = root + "/M_MinecraftFaces_v3"
        parent = unreal.load_asset(parent_path) if assets.does_asset_exist(parent_path) else None
        if parent is None:
            parent = tools.create_asset("M_MinecraftFaces_v3", root, unreal.Material, unreal.MaterialFactoryNew())
            if parent is None:
                raise RuntimeError("Cannot create texture master material")
            parent.set_editor_property("used_with_instanced_static_meshes", True)

            def node(cls):
                result = editing.create_material_expression(parent, cls, 0, 0)
                if result is None:
                    raise RuntimeError("Cannot create material expression")
                return result

            def wire(a, b, pin="", output=""):
                if not editing.connect_material_expressions(a, output, b, pin):
                    raise RuntimeError("Cannot connect " + a.get_class().get_name() + ":" + output + " -> " + b.get_class().get_name() + ":" + pin)

            color = node(unreal.MaterialExpressionVectorParameter)
            color.set_editor_property("parameter_name", "BlockColor")
            color.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
            white = node(unreal.MaterialExpressionConstant3Vector)
            white.set_editor_property("constant", unreal.LinearColor(1, 1, 1, 1))
            channels = {}
            for face in ("Top", "Side", "Bottom"):
                sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
                sample.set_editor_property("parameter_name", face + "Texture")
                sample.set_editor_property("texture", next(iter(imported.values())))
                tint = node(unreal.MaterialExpressionScalarParameter)
                tint.set_editor_property("parameter_name", face + "Tint")
                tint.set_editor_property("default_value", 0.0)
                blend = node(unreal.MaterialExpressionLinearInterpolate)
                wire(white, blend, "A"); wire(color, blend, "B"); wire(tint, blend, "Alpha")
                product = node(unreal.MaterialExpressionMultiply)
                wire(sample, product, "A", "RGB"); wire(blend, product, "B")
                channels[face] = product
            normal = node(unreal.MaterialExpressionVertexNormalWS)
            z = node(unreal.MaterialExpressionComponentMask)
            z.set_editor_property("r", False); z.set_editor_property("g", False)
            z.set_editor_property("b", True); z.set_editor_property("a", False)
            wire(normal, z)
            top = node(unreal.MaterialExpressionClamp); wire(z, top)
            negative = node(unreal.MaterialExpressionMultiply); negative.set_editor_property("const_b", -1.0); wire(z, negative, "A")
            bottom = node(unreal.MaterialExpressionClamp); wire(negative, bottom)
            first = node(unreal.MaterialExpressionLinearInterpolate)
            wire(channels["Side"], first, "A"); wire(channels["Top"], first, "B"); wire(top, first, "Alpha")
            result = node(unreal.MaterialExpressionLinearInterpolate)
            wire(first, result, "A"); wire(channels["Bottom"], result, "B"); wire(bottom, result, "Alpha")
            if not editing.connect_material_property(result, "", unreal.MaterialProperty.MP_BASE_COLOR):
                raise RuntimeError("Cannot connect material Base Color")
            roughness = node(unreal.MaterialExpressionConstant); roughness.set_editor_property("r", 0.85)
            editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
            editing.recompile_material(parent)
            if not assets.save_loaded_asset(parent, False):
                raise RuntimeError("Cannot save texture master material")
        elif not isinstance(parent, unreal.Material):
            raise RuntimeError("Master material path is occupied by a different type")
        editing.recompile_material(parent)
        required_textures = {face + "Texture" for face in ("Top", "Side", "Bottom")}
        actual_textures = {str(name) for name in editing.get_texture_parameter_names(parent)}
        if not required_textures.issubset(actual_textures):
            raise RuntimeError("Texture master is incomplete: " + str(sorted(required_textures - actual_textures)))
        progress.enter_progress_frame(1, "Master material")
        palette_materials = {}
        for identifier, entry in manifest["blocks"].items():
            if progress.should_cancel():
                raise RuntimeError("Import cancelled; palette and level unchanged")
            progress.enter_progress_frame(1, identifier)
            content = {face: {"hash": manifest["textures"][entry[face]["texture"]]["sha256"], "tint": entry[face]["tint"]} for face in ("top", "side", "bottom")}
            digest = hashlib.sha256(json.dumps({"master_version": 3, "faces": content}, sort_keys=True).encode()).hexdigest()
            name = asset_name("MI_", identifier, digest)
            folder = root + "/Materials"
            target = folder + "/" + name
            material = unreal.load_asset(target) if assets.does_asset_exist(target) else None
            if material is None:
                material = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
                if material is None:
                    raise RuntimeError("Cannot create block material")
            elif not isinstance(material, unreal.MaterialInstanceConstant):
                raise RuntimeError("Block material path is occupied by a different type")
            # A failed previous attempt may have left an instance with only some parameters.
            # Repair every instance rather than treating asset existence as import completion.
            editing.set_material_instance_parent(material, parent)
            editing.update_material_instance(material)
            for face in ("top", "side", "bottom"):
                parameter = face.title() + "Texture"
                if not editing.set_material_instance_texture_parameter_value(material, parameter, imported[entry[face]["texture"]]):
                    raise RuntimeError("Cannot set " + parameter + " for " + identifier)
                if not editing.set_material_instance_scalar_parameter_value(material, face.title() + "Tint", float(entry[face]["tint"])):
                    raise RuntimeError("Cannot set tint for " + identifier)
            editing.update_material_instance(material)
            if not assets.save_loaded_asset(material, False):
                raise RuntimeError("Cannot save block material")
            palette_materials[identifier] = material

    palette_path = root + "/DA_MinecraftPalette"
    palette = unreal.load_asset(palette_path) if assets.does_asset_exist(palette_path) else None
    if palette is None:
        factory = unreal.DataAssetFactory(); factory.set_editor_property("data_asset_class", palette_class)
        palette = tools.create_asset("DA_MinecraftPalette", root, palette_class, factory)
    if not isinstance(palette, palette_class):
        raise RuntimeError("Cannot create/resolve BridgeBlockPalette")
    materials = dict(palette.get_editor_property("materials")); materials.update(palette_materials)
    if len(materials) > 4096:
        raise RuntimeError("Combined palette exceeds the bridge limit of 4096 block IDs")
    with unreal.ScopedEditorTransaction("Assign Minecraft texture palette"):
        palette.set_editor_property("materials", materials)
        if not assets.save_loaded_asset(palette, False):
            raise RuntimeError("Cannot save texture palette")
        receivers[0].set_editor_property("texture_palette", palette)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Cannot save current level")
    unreal.log("Minecraft texture palette ready: " + str(len(palette_materials)) + " block types. Press Play and /uebridge world refresh.")
