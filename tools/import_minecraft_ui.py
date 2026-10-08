"""Import an immutable local Minecraft GUI export into the native UE player.

The module's validators deliberately do not import Unreal, so package preflight
and checksum tests work outside the editor. Existing saved UI assignments change
only after every referenced asset has imported and the palette has saved.
"""
import hashlib
import json
import math
import pathlib
import re
import struct
import zlib

MAX_UI_BYTES = 512 * 1024 * 1024
MAX_PNG_BYTES = 32 * 1024 * 1024
RESOURCE_ID = re.compile(r"[a-z0-9_.-]+:[a-z0-9_./-]+")


def _finite(value, lower, upper):
    return type(value) in (int, float) and math.isfinite(value) and lower <= value <= upper


def _asset_source(directory, entry, budget):
    if not isinstance(entry, dict) or not isinstance(entry.get("file"), str) or not isinstance(entry.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
        raise ValueError("Invalid native UI asset path/checksum")
    relative = pathlib.PurePosixPath(entry["file"])
    if relative.is_absolute() or ".." in relative.parts or "\\" in entry["file"] or ":" in entry["file"] or relative.suffix != ".png":
        raise ValueError("Native UI asset path must be a relative PNG inside the export")
    source = (directory / entry["file"]).resolve()
    if not source.is_relative_to(directory) or not source.is_file() or not 1 <= source.stat().st_size <= MAX_PNG_BYTES:
        raise ValueError("Missing/oversized or escaping native UI PNG")
    data = source.read_bytes()
    budget[0] += len(data)
    if budget[0] > MAX_UI_BYTES or hashlib.sha256(data).hexdigest() != entry["sha256"] or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Native UI PNG checksum/budget mismatch")
    cursor, dimensions, ended = 8, None, False
    while cursor + 12 <= len(data):
        length = struct.unpack_from(">I", data, cursor)[0]
        kind = data[cursor + 4:cursor + 8]
        end = cursor + length + 12
        if end > len(data) or zlib.crc32(data[cursor + 4:cursor + 8 + length]) & 0xffffffff != struct.unpack_from(">I", data, cursor + 8 + length)[0]:
            raise ValueError("Invalid native UI PNG chunk")
        if dimensions is None:
            if kind != b"IHDR" or length != 13:
                raise ValueError("Missing native UI PNG header")
            dimensions = struct.unpack_from(">II", data, cursor + 8)
        if kind == b"IEND":
            if length != 0 or end != len(data):
                raise ValueError("Invalid native UI PNG end")
            ended = True
            break
        cursor = end
    if not ended or any(type(entry.get(key)) is not int for key in ("width", "height")) or dimensions != (entry["width"], entry["height"]) or any(not 1 <= value <= 8192 for value in dimensions):
        raise ValueError("Native UI PNG dimensions/end mismatch")
    entry["source"] = str(source)


def load_ui_manifest(filename):
    path = pathlib.Path(filename).expanduser().resolve()
    if not path.is_file() or not 1 <= path.stat().st_size <= 32 * 1024 * 1024:
        raise ValueError("Missing/oversized native UI manifest")
    manifest = json.loads(path.read_bytes())
    if not isinstance(manifest, dict) or manifest.get("kind") != "native-ui" or type(manifest.get("version")) is not int or manifest["version"] != 1:
        raise ValueError("Unsupported native UI manifest")
    sprites, items = manifest.get("sprites"), manifest.get("items")
    if not isinstance(sprites, dict) or len(sprites) > 4096 or not isinstance(items, list) or not 1 <= len(items) <= 8192:
        raise ValueError("Invalid native UI sprite/item registry")
    budget, seen, sources = [0], set(), {}
    for key, entry in sprites.items():
        if not isinstance(key, str) or not re.fullmatch(r"(?:[a-z0-9_.-]+:)?[a-z0-9_./-]+", key) or ".." in key:
            raise ValueError("Invalid native UI sprite ID")
        _asset_source(path.parent, entry, budget)
    for item in items:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str) or not RESOURCE_ID.fullmatch(item["id"]) or ".." in item["id"] or item["id"] in seen:
            raise ValueError("Invalid/duplicate native UI item ID")
        seen.add(item["id"])
        if not isinstance(item.get("name"), str) or not 1 <= len(item["name"]) <= 512 or type(item.get("maxCount")) is not int or not 1 <= item["maxCount"] <= 99:
            raise ValueError("Invalid native UI item name/stack size")
        if "attackDamage" in item or "attackSpeed" in item:
            if not _finite(item.get("attackDamage"), 0, 2048) or not _finite(item.get("attackSpeed"), .01, 1024):
                raise ValueError("Invalid native UI weapon attributes")
        for field in ("block", "modelKey", "spawnType"):
            value = item.get(field, "")
            if not isinstance(value, str) or len(value) > 512 or ".." in value:
                raise ValueError("Invalid native UI item model/block")
        if item.get("block") and not RESOURCE_ID.fullmatch(item["block"]):
            raise ValueError("Invalid native UI block ID")
        if item.get("spawnType") and not RESOURCE_ID.fullmatch(item["spawnType"]):
            raise ValueError("Invalid native UI spawn entity ID")
        icon = {key: item.get(key) for key in ("sha256", "width", "height")}
        icon["file"] = item.get("icon")
        _asset_source(path.parent, icon, budget)
        item["source"] = icon["source"]
    font = manifest.get("font")
    if font is not None:
        _asset_source(path.parent, font, budget)
        glyphs = font.get("glyphs")
        if not isinstance(glyphs, list) or len(glyphs) > 65536:
            raise ValueError("Invalid native UI glyph registry")
        glyph_ids = set()
        for glyph in glyphs:
            if not isinstance(glyph, dict) or type(glyph.get("codepoint")) is not int or not 0 <= glyph["codepoint"] <= 0x10ffff or 0xd800 <= glyph["codepoint"] <= 0xdfff or glyph["codepoint"] in glyph_ids:
                raise ValueError("Invalid/duplicate Unicode glyph")
            glyph_ids.add(glyph["codepoint"])
            for key in ("x", "y", "width", "height"):
                if type(glyph.get(key)) is not int or not 0 <= glyph[key] <= 8192:
                    raise ValueError("Invalid native UI glyph coordinates")
            if glyph["x"] + glyph["width"] > font["width"] or glyph["y"] + glyph["height"] > font["height"]:
                raise ValueError("Native UI glyph exceeds atlas bounds")
            if not _finite(glyph.get("advance"), 0, 256):
                raise ValueError("Invalid native UI glyph advance")
            for key in ("drawWidth", "drawHeight", "ascent"):
                if key in glyph and not _finite(glyph[key], -256 if key == "ascent" else 0, 256):
                    raise ValueError("Invalid native UI glyph draw metrics")
    manifest["sourceManifest"] = str(path)
    return manifest


def import_minecraft_ui(filename):
    manifest = load_ui_manifest(filename)
    import unreal
    palette_class = getattr(unreal, "BridgeNativeUiPalette", None)
    item_class = getattr(unreal, "BridgeNativeUiItem", None)
    glyph_class = getattr(unreal, "BridgeNativeGlyph", None)
    if palette_class is None or item_class is None or glyph_class is None:
        raise RuntimeError("Build the native-play UE source before importing Minecraft UI")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None or unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Stop Play and save the current level before importing Minecraft UI")
    receivers = [actor for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(actor, unreal.BridgeReceiver)]
    if len(receivers) != 1:
        raise RuntimeError("The saved level needs exactly one BridgeReceiver")
    receiver = receivers[0]
    previous = receiver.get_editor_property("native_ui_palette")
    assets, tools = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools()
    root = "/Game/Bridge/Minecraft/NativeUI"
    imported = {}
    def texture_for(key, source):
        identity = source["sha256"]
        if identity in imported:
            return imported[identity]
        label = re.sub(r"[^A-Za-z0-9_]", "_", key)[:72]
        name = "T_" + label + "_" + identity[:16]
        target = root + "/" + name
        texture = unreal.load_asset(target) if assets.does_asset_exist(target) else None
        if texture is None:
            task = unreal.AssetImportTask()
            for field, value in dict(filename=source["source"], destination_path=root, destination_name=name, automated=True, save=True, replace_existing=False).items():
                task.set_editor_property(field, value)
            tools.import_asset_tasks([task])
            texture = unreal.load_asset(target)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("Cannot import Minecraft UI texture: " + key)
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        if not assets.save_loaded_asset(texture, False):
            raise RuntimeError("Cannot save Minecraft UI texture: " + key)
        imported[identity] = texture
        return texture
    count = len(manifest["sprites"]) + len(manifest["items"]) + int(manifest.get("font") is not None)
    with unreal.ScopedSlowTask(count, "Import Minecraft HUD and item icons") as progress:
        progress.make_dialog(True)
        def advance(label):
            if progress.should_cancel():
                raise RuntimeError("Native UI import cancelled; previous UI assignment retained")
            progress.enter_progress_frame(1, label)
        sprites = {}
        for key, entry in manifest["sprites"].items():
            advance(key)
            sprites[key] = texture_for(key, entry)
        items = []
        for source in manifest["items"]:
            advance(source["id"])
            icon = dict(source, file=source["icon"])
            entry = item_class()
            for field, value in dict(item_id=source["id"], display_name=source["name"], icon=texture_for("item_" + source["id"], icon), max_count=source["maxCount"], attack_damage=source.get("attackDamage", 1), attack_speed=source.get("attackSpeed", 0), block_id=source.get("block", ""), model_key=source.get("modelKey", ""), spawn_type=source.get("spawnType", "")).items():
                entry.set_editor_property(field, value)
            items.append(entry)
        font_texture, glyphs = None, []
        if manifest.get("font") is not None:
            source = manifest["font"]
            advance("Minecraft bitmap font")
            font_texture = texture_for("font", source)
            for source_glyph in source["glyphs"]:
                glyph = glyph_class()
                fields = {key: source_glyph[key] for key in ("codepoint", "x", "y", "width", "height", "advance")}
                fields.update(draw_width=source_glyph.get("drawWidth", source_glyph["width"]), draw_height=source_glyph.get("drawHeight", source_glyph["height"]), ascent=source_glyph.get("ascent", 7))
                for field, value in fields.items():
                    glyph.set_editor_property(field, value)
                glyphs.append(glyph)
    digest = hashlib.sha256(pathlib.Path(manifest["sourceManifest"]).read_bytes()).hexdigest()
    name = "DA_NativeUI_" + digest[:20]
    palette = unreal.load_asset(root + "/" + name)
    if palette is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", palette_class)
        palette = tools.create_asset(name, root, palette_class, factory)
    if not isinstance(palette, palette_class):
        raise RuntimeError("Cannot create Minecraft native UI palette")
    for field, value in dict(sprites=sprites, items=items, font_atlas=font_texture, glyphs=glyphs, language=manifest.get("language", ""), export_id=manifest.get("exportId", digest[:20])).items():
        palette.set_editor_property(field, value)
    if not assets.save_loaded_asset(palette, False):
        raise RuntimeError("Cannot save Minecraft native UI palette")
    try:
        with unreal.ScopedEditorTransaction("Assign native Minecraft HUD"):
            receiver.set_editor_property("native_ui_palette", palette)
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level() or receiver.get_editor_property("native_ui_palette") != palette:
            raise RuntimeError("Cannot save/verify native UI assignment")
    except Exception:
        receiver.set_editor_property("native_ui_palette", previous)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        raise
    missing = [key for key in ("hud/hotbar", "hud/hotbar_selection", "hud/crosshair", "container/inventory") if key not in sprites]
    if missing:
        unreal.log_warning("Native UI sprites not supplied by this resource pack: " + ", ".join(missing))
    if font_texture is None:
        unreal.log_warning("No supported bitmap font export; native HUD uses the Unreal fallback font")
    excluded = manifest.get("excludedItems", {})
    if excluded:
        unreal.log_warning("Native UI unsupported item icons: " + str(len(excluded)) + "; see the export manifest")
    unreal.log(f"Minecraft native UI ready: {len(items)} item icons, {len(sprites)} sprites, {len(glyphs)} glyphs. Saved Receiver UI palette assigned.")
    return palette


def setup_minecraft_ui(game_dir):
    directory = pathlib.Path(game_dir).expanduser().resolve() / "uebridge-export"
    candidates = list(directory.glob("native-*/ui/manifest.json"))
    if not candidates:
        raise RuntimeError("No native-play UI export found; run /uebridge native export in Minecraft first")
    return import_minecraft_ui(str(max(candidates, key=lambda path: path.stat().st_mtime_ns)))
