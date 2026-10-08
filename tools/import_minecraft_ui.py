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
        if any(field in item for field in ("equipmentSlot", "armor", "armorToughness", "armorKnockbackResistance")):
            if type(item.get("equipmentSlot")) is not int or not 0 <= item["equipmentSlot"] <= 4:
                raise ValueError("Invalid native UI equipment slot")
            for field, upper in (("armor", 1024), ("armorToughness", 1024), ("armorKnockbackResistance", 1)):
                if not _finite(item.get(field, 0), 0, upper):
                    raise ValueError("Invalid native UI equipment attributes")
        if "glint" in item and type(item["glint"]) is not bool: raise ValueError("Invalid native item glint flag")
        if "armorSprite" in item and item["armorSprite"] not in sprites: raise ValueError("Missing native armor sprite")
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
    groups = manifest.get("groups", [])
    if not isinstance(groups, list) or len(groups) > 64:
        raise ValueError("Invalid native UI creative groups")
    group_ids, positions = set(), set()
    for group in groups:
        if not isinstance(group, dict) or not isinstance(group.get("id"), str) or not RESOURCE_ID.fullmatch(group["id"]) or group["id"] in group_ids:
            raise ValueError("Invalid/duplicate native UI creative group ID")
        group_ids.add(group["id"])
        if not isinstance(group.get("name"), str) or not 1 <= len(group["name"]) <= 512 or group.get("type") not in ("category", "search", "inventory"):
            raise ValueError("Invalid native UI creative group name/type")
        if type(group.get("row")) is not int or group["row"] not in (0, 1) or type(group.get("column")) is not int or not 0 <= group["column"] <= 6:
            raise ValueError("Invalid native UI creative group position")
        position = (group["row"], group["column"])
        if position in positions:
            raise ValueError("Duplicate native UI creative group position")
        positions.add(position)
        if any(type(group.get(field)) is not bool for field in ("special", "scrollbar", "renderName")):
            raise ValueError("Invalid native UI creative group flags")
        if not isinstance(group.get("icon"), str) or not RESOURCE_ID.fullmatch(group["icon"]) or not isinstance(group.get("texture"), str) or group["texture"] not in sprites:
            raise ValueError("Missing native UI creative group icon/texture")
        order = group.get("items")
        if not isinstance(order, list) or len(order) > 8192 or any(not isinstance(item, str) or item not in seen for item in order) or len(order) != len(set(order)):
            raise ValueError("Invalid native UI creative group item order")
    manifest["groups"] = groups
    poof = manifest.get("deathPoofFrames", [])
    if not isinstance(poof, list) or len(poof) > 256 or any(not isinstance(key, str) or key not in sprites for key in poof):
        raise ValueError("Invalid native UI death poof sprite frames")
    manifest["deathPoofFrames"] = poof
    particle_frames=manifest.get("particleFrames",{})
    if not isinstance(particle_frames,dict) or len(particle_frames)>256:
        raise ValueError("Invalid native particle frame registry")
    for key,frames in particle_frames.items():
        if not RESOURCE_ID.fullmatch(key) or not isinstance(frames,list) or len(frames)>256 or any(not isinstance(frame,str) or frame not in sprites for frame in frames):
            raise ValueError("Invalid native particle frames")
    manifest["particleFrames"]=particle_frames
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
    gameplay = manifest.get("gameplay", {})
    if not isinstance(gameplay, dict) or (gameplay and (type(gameplay.get("version")) is not int or gameplay["version"] != 1)):
        raise ValueError("Invalid native gameplay version")
    if "randomTickSpeed" in gameplay and (type(gameplay["randomTickSpeed"]) is not int or not 0<=gameplay["randomTickSpeed"]<=4096):
        raise ValueError("Invalid native random tick speed")
    if "tickRate" in gameplay and (type(gameplay["tickRate"]) is not int or gameplay["tickRate"]!=20):
        raise ValueError("Invalid native simulation tick rate")
    recipes = gameplay.get("recipes", [])
    if not isinstance(recipes, list) or len(recipes) > 32768:
        raise ValueError("Invalid native recipe registry")
    recipe_ids = set()
    for recipe in recipes:
        if not isinstance(recipe, dict) or not RESOURCE_ID.fullmatch(str(recipe.get("id", ""))) or recipe["id"] in recipe_ids or not RESOURCE_ID.fullmatch(str(recipe.get("type", ""))):
            raise ValueError("Invalid/duplicate native recipe")
        recipe_ids.add(recipe["id"])
        ingredients = recipe.get("ingredients", [])
        if not isinstance(ingredients, list) or len(ingredients) > 9 or any(not isinstance(values, list) or len(values) > 8192 or any(not isinstance(item, str) or not RESOURCE_ID.fullmatch(item) for item in values) for values in ingredients):
            raise ValueError("Invalid native recipe ingredients")
        if "result" in recipe and (not RESOURCE_ID.fullmatch(str(recipe["result"])) or type(recipe.get("count")) is not int or not 1 <= recipe["count"] <= 99):
            raise ValueError("Invalid native recipe result")
        for field in ("width", "height"):
            if field in recipe and (type(recipe[field]) is not int or not 1 <= recipe[field] <= 3):
                raise ValueError("Invalid native recipe dimensions")
        if "ticks" in recipe and (type(recipe["ticks"]) is not int or not 1 <= recipe["ticks"] <= 1000000):
            raise ValueError("Invalid native recipe cooking time")
        if "result" in recipe:
            if recipe["type"]=="minecraft:crafting_shaped" and (recipe.get("width",0)*recipe.get("height",0)!=len(ingredients) or not ingredients):
                raise ValueError("Invalid native shaped recipe grid")
            if recipe["type"]=="minecraft:crafting_shapeless" and not ingredients:
                raise ValueError("Invalid native shapeless recipe grid")
            if recipe["type"] in ("minecraft:smelting","minecraft:blasting","minecraft:smoking","minecraft:campfire_cooking","minecraft:stonecutting") and len(ingredients)!=1:
                raise ValueError("Invalid native single recipe input")
    for field in ("fuels", "remainders"):
        table = gameplay.get(field, {})
        if not isinstance(table, dict) or len(table) > 8192 or any(not RESOURCE_ID.fullmatch(str(item)) for item in table):
            raise ValueError("Invalid native gameplay table")
        for value in table.values():
            if field == "fuels" and (type(value) is not int or not 1 <= value <= 1000000) or field == "remainders" and (not isinstance(value, str) or not RESOURCE_ID.fullmatch(value)):
                raise ValueError("Invalid native fuel/remainder")
    containers = gameplay.get("containers", [])
    if not isinstance(containers, list) or len(containers) > 4096: raise ValueError("Invalid native source containers")
    container_keys=set()
    for container in containers:
        if not isinstance(container, dict) or not isinstance(container.get("key"), str) or not re.fullmatch(r"-?[0-9]{1,8},-?[0-9]{1,8},-?[0-9]{1,8}", container["key"]) or container["key"] in container_keys or container.get("kind") not in ("chest", "furnace", "blast_furnace", "smoker", "hopper", "dropper", "dispenser"):
            raise ValueError("Invalid/duplicate native container")
        container_keys.add(container["key"])
        if any(abs(int(value))>30000000 for value in container["key"].split(',')):
            raise ValueError("Native container outside world bounds")
        slots=container.get("slots")
        sizes={"chest":27,"hopper":5,"dropper":9,"dispenser":9}
        if not isinstance(slots, list) or len(slots)!=sizes.get(container["kind"],3): raise ValueError("Invalid native container slots")
        for stack in slots:
            if not isinstance(stack, dict) or type(stack.get("count")) is not int or not 0<=stack["count"]<=99 or not isinstance(stack.get("item"), str) or (stack["count"]>0 and not RESOURCE_ID.fullmatch(stack["item"])) or (stack["count"]==0 and stack["item"]):
                raise ValueError("Invalid native container stack")
        for field in ("burn", "burnTotal", "cook"):
            if type(container.get(field)) is not int or not 0<=container[field]<=1000000: raise ValueError("Invalid native source cooking state")
    manifest["gameplay"] = gameplay
    manifest["sourceManifest"] = str(path)
    return manifest


def import_minecraft_ui(filename):
    manifest = load_ui_manifest(filename)
    import unreal
    palette_class = getattr(unreal, "BridgeNativeUiPalette", None)
    item_class = getattr(unreal, "BridgeNativeUiItem", None)
    glyph_class = getattr(unreal, "BridgeNativeGlyph", None)
    group_class = getattr(unreal, "BridgeNativeUiGroup", None)
    if palette_class is None or item_class is None or glyph_class is None or (manifest["groups"] and group_class is None):
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
            for field, value in dict(item_id=source["id"], display_name=source["name"], icon=texture_for("item_" + source["id"], icon), max_count=source["maxCount"], glint=source.get("glint", False), armor_sprite=source.get("armorSprite", ""), equipment_slot=source.get("equipmentSlot", 0), armor=source.get("armor", 0), armor_toughness=source.get("armorToughness", 0), armor_knockback_resistance=source.get("armorKnockbackResistance", 0), attack_damage=source.get("attackDamage", 1), attack_speed=source.get("attackSpeed", 0), block_id=source.get("block", ""), model_key=source.get("modelKey", ""), spawn_type=source.get("spawnType", "")).items():
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
    groups = []
    for source_group in manifest["groups"]:
        group = group_class()
        fields = dict(group_id=source_group["id"], display_name=source_group["name"], type=source_group["type"], icon_item=source_group["icon"],
                      texture=source_group["texture"], row=source_group["row"], column=source_group["column"], special=source_group["special"],
                      scrollbar=source_group["scrollbar"], render_name=source_group["renderName"], items=source_group["items"])
        for field, value in fields.items():
            group.set_editor_property(field, value)
        groups.append(group)
    digest = hashlib.sha256(pathlib.Path(manifest["sourceManifest"]).read_bytes()).hexdigest()
    name = "DA_NativeUI_" + digest[:20]
    palette = unreal.load_asset(root + "/" + name)
    if palette is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", palette_class)
        palette = tools.create_asset(name, root, palette_class, factory)
    if not isinstance(palette, palette_class):
        raise RuntimeError("Cannot create Minecraft native UI palette")
    from import_minecraft_textures import _lighting_functions
    glint_material=_lighting_functions(unreal)["ensure_native_icon_glint_material"](unreal,assets,unreal.MaterialEditingLibrary)
    for field, value in dict(icon_glint_material=glint_material, particle_frames_data=json.dumps(manifest["particleFrames"], separators=(",", ":")), gameplay_data=json.dumps(manifest["gameplay"], ensure_ascii=False, separators=(",", ":")), sprites=sprites, items=items, groups=groups, death_poof_frames=[sprites[key] for key in manifest["deathPoofFrames"]], font_atlas=font_texture, glyphs=glyphs, language=manifest.get("language", ""), export_id=manifest.get("exportId", digest[:20])).items():
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
