"""Local terrain texture atlas. Standard-library PNG handling; no shipped MC images.

The atlas keeps native tile UV repetition in the shader. Tint and light remain per
vertex, so a cell normally needs one masked section rather than one per texture.
"""
import hashlib
import json
import pathlib
import struct
import zlib


def _decode_png(data):
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Atlas source is not PNG")
    width = height = depth = color = None
    compressed, palette, transparency = bytearray(), b"", b""
    cursor, ended = 8, False
    while cursor + 12 <= len(data):
        length = struct.unpack_from(">I", data, cursor)[0]
        kind = data[cursor + 4:cursor + 8]
        content = data[cursor + 8:cursor + 8 + length]
        if cursor + length + 12 > len(data) or zlib.crc32(kind + content) & 0xffffffff != struct.unpack_from(">I", data, cursor + length + 8)[0]:
            raise ValueError("Invalid atlas PNG chunk")
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", content)
            if not 1 <= width <= 2048 or not 1 <= height <= 2048 or compression or filtering or interlace:
                raise ValueError("Unsupported atlas PNG dimensions/interlace")
        elif kind == b"IDAT":
            compressed.extend(content)
        elif kind == b"PLTE":
            palette = content
        elif kind == b"tRNS":
            transparency = content
        elif kind == b"IEND":
            if length != 0 or cursor + 12 != len(data):
                raise ValueError("Invalid atlas PNG end")
            ended = True
            break
        cursor += length + 12
    if not ended or width is None:
        raise ValueError("Incomplete atlas PNG")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if channels is None or depth not in (1, 2, 4, 8, 16) or (depth < 8 and color not in (0, 3)) or (color == 3 and depth == 16):
        raise ValueError("Unsupported atlas PNG format")
    if color == 3 and (not palette or len(palette) % 3):
        raise ValueError("Atlas PNG palette missing")
    row_bytes = (width * channels * depth + 7) // 8
    bpp = max(1, (channels * depth + 7) // 8)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, (row_bytes + 1) * height + 1)
    if len(raw) != (row_bytes + 1) * height or not decoder.eof or decoder.unconsumed_tail or decoder.unused_data:
        raise ValueError("Atlas PNG decompression size mismatch")
    pixels = bytearray(width * height * 4)
    previous = bytearray(row_bytes)
    for y in range(height):
        filter_type = raw[y * (row_bytes + 1)]
        row = bytearray(raw[y * (row_bytes + 1) + 1:(y + 1) * (row_bytes + 1)])
        if filter_type > 4:
            raise ValueError("Invalid atlas PNG filter")
        for i in range(row_bytes):
            left = row[i - bpp] if i >= bpp else 0
            above = previous[i]
            upper_left = previous[i - bpp] if i >= bpp else 0
            prediction = left + above - upper_left
            distances = abs(prediction - left), abs(prediction - above), abs(prediction - upper_left)
            paeth = left if distances[0] <= distances[1] and distances[0] <= distances[2] else above if distances[1] <= distances[2] else upper_left
            row[i] = (row[i] + (0, left, above, (left + above) // 2, paeth)[filter_type]) & 255
        for x in range(width):
            if depth < 8:
                value = (row[x * depth // 8] >> (8 - depth - x * depth % 8)) & ((1 << depth) - 1)
                components = [value]
            else:
                stride = channels * (depth // 8)
                components = [row[x * stride + c * (depth // 8)] for c in range(channels)]
                value = components[0]
            if color == 0:
                gray = value * 255 // ((1 << depth) - 1) if depth < 8 else value
                alpha = 255
                if len(transparency) == 2:
                    transparent = struct.unpack(">H", transparency)[0]
                    original = struct.unpack_from(">H", row, x * 2)[0] if depth == 16 else value
                    alpha = 0 if original == transparent else 255
                rgba = (gray, gray, gray, alpha)
            elif color == 2:
                alpha = 255
                if len(transparency) == 6:
                    transparent = struct.unpack(">HHH", transparency)
                    original = tuple(struct.unpack_from(">H", row, x * 6 + c * 2)[0] for c in range(3)) if depth == 16 else tuple(components)
                    alpha = 0 if original == transparent else 255
                rgba = (*components, alpha)
            elif color == 3:
                if value * 3 + 3 > len(palette):
                    raise ValueError("Atlas palette index out of range")
                rgba = (*palette[value * 3:value * 3 + 3], transparency[value] if value < len(transparency) else 255)
            elif color == 4:
                rgba = (components[0], components[0], components[0], components[1])
            else:
                rgba = tuple(components)
            pixels[(y * width + x) * 4:(y * width + x + 1) * 4] = bytes(rgba)
        previous = row
    return width, height, pixels


def _encode_png(width, height, rgba):
    if len(rgba) != width * height * 4:
        raise ValueError("Invalid atlas output pixels")
    def chunk(kind, content):
        return struct.pack(">I", len(content)) + kind + content + struct.pack(">I", zlib.crc32(kind + content) & 0xffffffff)
    compressed = zlib.compress(b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height)), 6)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")


def build_atlases(manifest, cache_directory, size=2048):
    """Return content-addressed local atlas pages and tile rectangles; unsupported PNGs stay unbatched."""
    if size < 32 or size > 4096 or size & (size - 1):
        raise ValueError("Atlas size must be a power of two, 32..4096")
    cache = pathlib.Path(cache_directory)
    cache.mkdir(parents=True, exist_ok=True)
    images, skipped, total = [], {}, 0
    for identifier, entry in manifest["textures"].items():
        try:
            width, height, pixels = _decode_png(pathlib.Path(entry["source"]).read_bytes())
            if width + 4 > size or height + 4 > size:
                raise ValueError("Large tile kept in its original material")
            total += len(pixels)
            if total > 128 * 1024 * 1024:
                raise ValueError("Decoded atlas budget reached")
            images.append((entry.get("alphaMode", "cutout") == "translucent", height, width, identifier, pixels))
        except ValueError as error:
            skipped[identifier] = str(error)
    images.sort(key=lambda image: (image[0], -image[1], -image[2], image[3]))
    pages, tiles = [], {}
    page_pixels, page_tiles, x, y, row_height, translucent = None, {}, 0, 0, 0, None
    def finish_page():
        if page_pixels is None:
            return
        encoded = _encode_png(size, size, page_pixels)
        digest = hashlib.sha256(encoded).hexdigest()
        filename = cache / ("atlas-" + digest + ".png")
        if not filename.exists():
            filename.write_bytes(encoded)
        elif hashlib.sha256(filename.read_bytes()).hexdigest() != digest:
            raise ValueError("Atlas cache checksum mismatch")
        page_id = digest + ("-translucent" if translucent else "-masked")
        pages.append({"id": page_id, "source": str(filename), "sha256": digest, "translucent": translucent})
        for identifier, rectangle in page_tiles.items():
            tiles[identifier] = {"page": page_id, "rect": rectangle}
    for is_translucent, height, width, identifier, pixels in images:
        if page_pixels is not None and (is_translucent != translucent or (x + width + 4 > size and y + row_height + height + 4 > size)):
            finish_page()
            page_pixels = None
        if page_pixels is None:
            if len(pages) >= 16:
                skipped[identifier] = "Atlas page budget reached"
                continue
            page_pixels, page_tiles, x, y, row_height, translucent = bytearray(size * size * 4), {}, 0, 0, 0, is_translucent
        if x + width + 4 > size:
            y += row_height
            x, row_height = 0, 0
        # Duplicate edge texels into a 2-pixel gutter, keeping repeated tiles isolated.
        for py in range(-2, height + 2):
            sy = min(height - 1, max(0, py))
            for px in range(-2, width + 2):
                sx = min(width - 1, max(0, px))
                destination = ((y + 2 + py) * size + x + 2 + px) * 4
                source = (sy * width + sx) * 4
                page_pixels[destination:destination + 4] = pixels[source:source + 4]
        page_tiles[identifier] = [(x + 2) / size, (y + 2) / size, width / size, height / size]
        x += width + 4
        row_height = max(row_height, height + 4)
    finish_page()
    return {"pages": pages, "tiles": tiles, "skipped": skipped}


def _atlas_master(unreal, assets, tools, editing, root, texture, translucent):
    name = "M_MinecraftAtlas_" + ("Translucent" if translucent else "Masked") + "_v1"
    path = root + "/" + name
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Cannot create atlas master")
    # This generated graph has no user edits. Rebuilding reuses asset references safely.
    editing.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.1)
    material.set_editor_property("two_sided", True)
    if translucent:
        material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    def node(cls):
        result = editing.create_material_expression(material, cls, 0, 0)
        if result is None:
            raise RuntimeError("Cannot create atlas material node")
        return result
    def wire(a, b, pin="", output=""):
        if not editing.connect_material_expressions(a, output, b, pin):
            raise RuntimeError("Cannot connect atlas material " + pin)
    uv = [node(unreal.MaterialExpressionTextureCoordinate) for unused in range(4)]
    for i, coordinate in enumerate(uv):
        coordinate.set_editor_property("coordinate_index", i)
    repeat = node(unreal.MaterialExpressionFrac); wire(uv[0], repeat)
    scale = node(unreal.MaterialExpressionMultiply); wire(repeat, scale, "A"); wire(uv[2], scale, "B")
    offset = node(unreal.MaterialExpressionAdd); wire(scale, offset, "A"); wire(uv[1], offset, "B")
    sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
    sample.set_editor_property("parameter_name", "AtlasTexture"); sample.set_editor_property("texture", texture)
    if not editing.connect_material_expressions(offset, "", sample, "UVs") and not editing.connect_material_expressions(offset, "", sample, "Coordinates"):
        raise RuntimeError("Cannot connect atlas texture UVs")
    vertex = node(unreal.MaterialExpressionVertexColor)
    tint = node(unreal.MaterialExpressionAppendVector); wire(uv[3], tint, "A"); wire(vertex, tint, "B", "A")
    colored = node(unreal.MaterialExpressionMultiply); wire(sample, colored, "A", "RGB"); wire(tint, colored, "B")
    import runpy
    wire_vanilla_lighting = runpy.run_path(str(pathlib.Path(__file__).with_name("bridge_lighting_materials.py")))["wire_vanilla_lighting"]
    wire_vanilla_lighting(unreal, editing, material, colored, vertex, use_vertex=True, vertex_output="")
    if not editing.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY if translucent else unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError("Cannot connect atlas opacity")
    roughness = node(unreal.MaterialExpressionConstant); roughness.set_editor_property("r", 0.85)
    if not editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS):
        raise RuntimeError("Cannot connect atlas roughness")
    editing.recompile_material(material)
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError("Cannot save atlas master")
    return material


def import_minecraft_atlas(unreal, manifest, palette, root="/Game/Bridge/Minecraft"):
    """Called by the texture importer after all native textures/manifests are validated."""
    if not manifest.get("textures") or not manifest.get("models"):
        return
    source = pathlib.Path(next(iter(manifest["textures"].values()))["source"])
    # Cache beside the export manifest, never inside Content or version-controlled source.
    cache = source.parent / ".uebridge-atlas-cache"
    atlas = build_atlases(manifest, cache)
    assets, tools, editing = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools(), unreal.MaterialEditingLibrary
    materials, parents = {}, {}
    for page in atlas["pages"]:
        name = "T_Atlas_" + page["sha256"][:24]
        destination = root + "/TerrainAtlases"
        texture_path = destination + "/" + name
        texture = unreal.load_asset(texture_path) if assets.does_asset_exist(texture_path) else None
        if texture is None:
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", page["source"]);task.set_editor_property("destination_path", destination)
            task.set_editor_property("destination_name", name);task.set_editor_property("automated", True);task.set_editor_property("replace_existing", False);task.set_editor_property("save", True)
            tools.import_asset_tasks([task]);texture = unreal.load_asset(texture_path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("Cannot import terrain atlas texture")
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("srgb", True)
        if not assets.save_loaded_asset(texture, False):
            raise RuntimeError("Cannot save terrain atlas texture")
        parent = parents.get(page["translucent"])
        if parent is None:
            parent = _atlas_master(unreal, assets, tools, editing, root, texture, page["translucent"])
            parents[page["translucent"]] = parent
        instance_name = "MI_Atlas_" + page["sha256"][:24] + ("_T" if page["translucent"] else "_M")
        path = destination + "/" + instance_name
        instance = unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(instance_name, destination, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError("Cannot create atlas material instance")
        editing.set_material_instance_parent(instance, parent)
        instance.set_editor_property("texture_parameter_values", [unreal.TextureParameterValue(parameter_info=unreal.MaterialParameterInfo(name="AtlasTexture"), parameter_value=texture)])
        editing.update_material_instance(instance)
        if not assets.save_loaded_asset(instance, False):
            raise RuntimeError("Cannot save atlas material instance")
        materials[page["id"]] = instance
    palette.set_editor_property("atlas_rects", {identifier: unreal.Vector4(*entry["rect"]) for identifier, entry in atlas["tiles"].items()})
    palette.set_editor_property("atlas_pages", {identifier: entry["page"] for identifier, entry in atlas["tiles"].items()})
    palette.set_editor_property("atlas_materials", materials)
    unreal.log("Minecraft terrain atlas: " + str(len(atlas["tiles"])) + " textures / " + str(len(materials)) + " pages; fallback=" + str(len(atlas["skipped"])))
