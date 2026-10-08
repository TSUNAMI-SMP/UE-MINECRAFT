"""UE Python: exec(open('C:/.../import_minecraft_textures.py', encoding='utf-8').read()); import_minecraft_textures('C:/.../manifest.json')

Pure manifest validation can run outside UE. Import requires a saved UEBridge level.
Assets use content hashes, are reused on repeat import, and never deleted.
"""
import hashlib
import json
import math
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


def _number(value, low, high):
    return type(value) in (int, float) and math.isfinite(value) and low <= value <= high


def _state_key(value):
    if not isinstance(value, str) or len(value) > 512:
        raise ValueError("Invalid block state key")
    if not value:
        return
    parts = value.split(",")
    if parts != sorted(parts) or len({p.split("=", 1)[0] for p in parts}) != len(parts):
        raise ValueError("State properties must be unique and sorted")
    if any(not re.fullmatch(r"[a-z0-9_]+=[a-z0-9_]+", part) for part in parts):
        raise ValueError("Invalid state property")


def _validate_models(manifest):
    blocks, textures = manifest["blocks"], manifest["textures"]
    definitions, models = manifest.get("blockstates"), manifest.get("models")
    if not isinstance(definitions, dict) or not isinstance(models, dict) or len(models) > 16384 or not models:
        raise ValueError("Missing/oversized baked model palette")
    if set(definitions) != set(blocks):
        raise ValueError("Every exported block needs a blockstate definition")
    state_budget = 0
    for identifier, entry in blocks.items():
        try:
            _state_key(entry.get("defaultState"))
        except ValueError as error:
            raise ValueError(f"{identifier}: defaultState={entry.get('defaultState')!r}: {error}. Update the Bridge MOD and re-export textures.") from error
        states = entry.get("states")
        offset = entry.get("modelOffset", [0, 0])
        if not isinstance(offset, list) or len(offset) != 2 or not all(_number(v, 0, 0.5) for v in offset):
            raise ValueError("Invalid vegetation model offset")
        if not isinstance(states, dict) or not 1 <= len(states) <= 8192 or entry["defaultState"] not in states:
            raise ValueError("Missing/oversized native block states")
        state_budget += len(states)
        if state_budget > 131072:
            raise ValueError("Native block state budget exceeded")
        for key, state in states.items():
            try:
                _state_key(key)
            except ValueError as error:
                raise ValueError(f"{identifier}: state={key!r}: {error}. Update the Bridge MOD and re-export textures.") from error
            if not isinstance(state, dict):
                raise ValueError("Invalid native state")
            for lighting_key in ('emission', 'opacity'):
                if lighting_key in state and (type(state[lighting_key]) is not int or not 0 <= state[lighting_key] <= 15):
                    raise ValueError('Invalid native state lighting')
            if 'opaqueFullCube' in state and type(state['opaqueFullCube']) is not bool:
                raise ValueError('Invalid native opaque cube')
            if type(state.get("cannotConnect", False)) is not bool:
                raise ValueError("Invalid native connection flag")
            solid = state.get("solidFaces")
            if solid is not None and (not isinstance(solid, list) or len(solid) > 6 or any(not isinstance(face, str) for face in solid) or len(set(solid)) != len(solid)
                    or any(face not in ("north", "south", "east", "west", "up", "down") for face in solid)):
                raise ValueError("Invalid native solid faces")
            for kind in ("collision", "outline"):
                boxes = state.get(kind)
                if not isinstance(boxes, list) or len(boxes) > 64:
                    raise ValueError("Invalid native shape")
                for box in boxes:
                    if (not isinstance(box, list) or len(box) != 6 or not all(_number(v, -4, 4) for v in box)
                            or any(box[i] >= box[i + 3] for i in range(3))):
                        raise ValueError("Invalid native shape bounds")
    for identifier, model in models.items():
        _identifier(identifier)
        if not isinstance(model, dict) or not isinstance(model.get("elements"), list) or len(model["elements"]) > 512:
            raise ValueError("Invalid model elements")
        for element in model["elements"]:
            if not isinstance(element, dict):
                raise ValueError("Invalid model element")
            for kind in ("from", "to"):
                vector = element.get(kind)
                if not isinstance(vector, list) or len(vector) != 3 or not all(_number(v, -64, 80) for v in vector):
                    raise ValueError("Invalid model cuboid coordinates")
            # Vanilla's *_inner_faces models deliberately invert X to draw interior
            # surfaces. Keep signed extents; physics boxes are validated separately.
            rotation = element.get("rotation")
            if rotation is not None:
                if (not isinstance(rotation, dict) or rotation.get("axis") not in ("x", "y", "z")
                        or rotation.get("angle") not in (-45, -22.5, 0, 22.5, 45)
                        or type(rotation.get("rescale", False)) is not bool
                        or not isinstance(rotation.get("origin"), list) or len(rotation["origin"]) != 3
                        or not all(_number(v, -64, 80) for v in rotation["origin"])):
                    raise ValueError("Invalid element rotation")
            faces = element.get("faces")
            if not isinstance(faces, dict) or not faces or not set(faces).issubset({"up", "down", "north", "south", "east", "west"}):
                raise ValueError("Invalid model faces")
            for face in faces.values():
                if not isinstance(face, dict) or face.get("texture") not in textures:
                    raise ValueError("Unresolved model texture")
                uv = face.get("uv")
                if uv is not None and (not isinstance(uv, list) or len(uv) != 4 or not all(_number(v, -64, 80) for v in uv)):
                    raise ValueError("Invalid model UV")
                if (type(face.get("rotation", 0)) is not int or face.get("rotation", 0) not in (0, 90, 180, 270)
                        or type(face.get("tintindex", -1)) is not int or not -1 <= face.get("tintindex", -1) <= 255):
                    raise ValueError("Invalid model face rotation/tint")
    def application(value):
        options = value if isinstance(value, list) else [value]
        if not 1 <= len(options) <= 256:
            raise ValueError("Invalid weighted model list")
        for item in options:
            if not isinstance(item, dict) or not isinstance(item.get("model"), str):
                raise ValueError("Invalid model application")
            identifier = item["model"] if ":" in item["model"] else "minecraft:" + item["model"]
            if identifier not in models:
                raise ValueError("Missing referenced model")
            for axis in ("x", "y"):
                if type(item.get(axis, 0)) is not int or item.get(axis, 0) not in (0, 90, 180, 270):
                    raise ValueError("Invalid blockstate model rotation")
            if type(item.get("uvlock", False)) is not bool or type(item.get("weight", 1)) is not int or not 1 <= item.get("weight", 1) <= 65536:
                raise ValueError("Invalid model UV lock/weight")
    def condition(value, depth=0):
        if not isinstance(value, dict) or depth > 8 or len(value) > 32:
            raise ValueError("Invalid multipart condition")
        for key, item in value.items():
            if key in ("OR", "AND"):
                if not isinstance(item, list) or not 1 <= len(item) <= 64:
                    raise ValueError("Invalid multipart boolean expression")
                for term in item:
                    condition(term, depth + 1)
            elif not re.fullmatch(r"[a-z0-9_]+", key) or not isinstance(item, str) or not re.fullmatch(r"[a-z0-9_]+(?:\|[a-z0-9_]+)*", item):
                raise ValueError("Invalid multipart state selector")
    for identifier, definition in definitions.items():
        if not isinstance(definition, dict) or ("variants" in definition) == ("multipart" in definition):
            raise ValueError("Blockstate needs variants or multipart")
        if "variants" in definition:
            variants = definition["variants"]
            if not isinstance(variants, dict) or not 1 <= len(variants) <= 8192:
                raise ValueError("Invalid variant palette")
            for selector, value in variants.items():
                if not isinstance(selector, str) or len(selector) > 512:
                    raise ValueError("Invalid variant selector")
                # Vanilla selectors can use alternative values and are not necessarily sorted.
                if selector and any(not re.fullmatch(r"[a-z0-9_]+=[a-z0-9_]+(?:\|[a-z0-9_]+)*", p) for p in selector.split(",")):
                    raise ValueError("Invalid variant selector")
                application(value)
        else:
            parts = definition["multipart"]
            if not isinstance(parts, list) or not 1 <= len(parts) <= 256:
                raise ValueError("Invalid multipart palette")
            for part in parts:
                if not isinstance(part, dict) or "apply" not in part:
                    raise ValueError("Invalid multipart part")
                if "when" in part:
                    condition(part["when"])
                application(part["apply"])


def load_texture_manifest(filename):
    path = pathlib.Path(filename).expanduser().resolve()
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("Manifest too large")
    manifest = json.loads(path.read_text(encoding="utf-8"), parse_constant=lambda value: (_ for _ in ()).throw(ValueError("Nonfinite JSON number")))
    if not isinstance(manifest, dict) or manifest.get("format") != "uebridge-block-textures" or type(manifest.get("version")) is not int or manifest["version"] not in (1, 2):
        raise ValueError("Unsupported texture manifest")
    textures, blocks = manifest.get("textures"), manifest.get("blocks")
    if not isinstance(textures, dict) or not isinstance(blocks, dict) or not textures or not blocks or len(textures) > 4096 or len(blocks) > 4096:
        raise ValueError("Empty/invalid/oversized texture palette")
    total = 0
    for name, entry in textures.items():
        _identifier(name)
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            raise ValueError("Invalid texture entry")
        for key, limit in (('animationFrames', 16384), ('animationFrameTime', 32767)):
            if key in entry and (type(entry[key]) is not int or not 1 <= entry[key] <= limit):
                raise ValueError('Invalid texture animation')
        if 'animationInterpolate' in entry and type(entry['animationInterpolate']) is not bool:
            raise ValueError('Invalid texture animation interpolation flag')
        if entry.get("alphaMode", "cutout") not in ("opaque", "cutout", "translucent"):
            raise ValueError("Invalid texture alpha mode")
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
            limit = 16384 if key == 'height' and entry.get('animationFrames', 1) > 1 else 2048
            if type(entry.get(key)) is not int or not 1 <= entry[key] <= limit:
                raise ValueError("Invalid texture dimensions")
        if entry['width'] * entry['height'] > 16_777_216:
            raise ValueError('Texture pixel budget exceeded')
        if entry["height"] % entry.get("animationFrames",1) != 0:
            raise ValueError("Animation frame count does not divide the texture strip")
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
        for key in ("renderTints", "tintSources"):
            data = entry.get(key)
            if data is not None and (not isinstance(data, dict) or len(data) > 256
                    or any(not isinstance(index, str) or not re.fullmatch(r"0|[1-9][0-9]{0,2}", index)
                           or int(index) > 255 for index in data)):
                raise ValueError("Invalid native render tint table")
        if any(type(color) is not int or not 0 <= color <= 0xffffff for color in entry.get("renderTints", {}).values()):
            raise ValueError("Invalid native render tint color")
        if any(source not in ("none", "grass", "foliage", "dry_foliage", "constant") for source in entry.get("tintSources", {}).values()):
            raise ValueError("Invalid native render tint source")
        for face in ("top", "side", "bottom"):
            item = entry.get(face)
            if not isinstance(item, dict) or item.get("texture") not in textures or type(item.get("tint")) is not bool:
                raise ValueError("Invalid block face/texture reference")
        particle = entry.get("particle")
        if particle is not None:
            if (not isinstance(particle, dict) or particle.get("texture") not in textures
                    or type(particle.get("tint")) is not bool
                    or type(particle.get("color")) is not int or not 0 <= particle["color"] <= 0xffffff):
                raise ValueError("Invalid block particle texture/tint")
    if manifest["version"] == 2:
        _validate_models(manifest)
    return manifest


def _lighting_functions(unreal):
    import runpy
    source = globals().get('__file__')
    script = pathlib.Path(source).with_name('bridge_lighting_materials.py') if source else pathlib.Path(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'bridge_lighting_materials.py'
    if not script.is_file():
        raise RuntimeError('Copy bridge_lighting_materials.py next to UEBridge.uproject first')
    return runpy.run_path(str(script))


def _model_parent(unreal, assets, tools, editing, root, sample_texture, alpha_mode):
    """Rebuild our generated master only, retaining existing texture instances."""
    translucent = alpha_mode == 'translucent'
    name = 'M_MinecraftModel_' + ('Translucent' if translucent else 'Masked') + '_v3'
    path = root + '/' + name
    parent = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if parent is None:
        parent = tools.create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(parent, unreal.Material):
        raise RuntimeError('Model master path has another asset type')
    scalar_names = {str(value) for value in editing.get_scalar_parameter_names(parent)}
    texture_names = {str(value) for value in editing.get_texture_parameter_names(parent)}
    use_vertex = not root.endswith('/Items')
    required_scalars = {'FaceTint', 'BridgeUnlit', 'BridgeSpecular', 'BridgeLightingRevision_v5', 'AnimationFrames', 'AnimationFrameTime', 'AnimationInterpolate', 'BridgeAnimationRevision_v1', 'BridgeGlint'}
    if use_vertex:
        required_scalars.add('BridgeUseVertexLight')
    if required_scalars.issubset(scalar_names) and 'FaceTexture' in texture_names:
        # All texture instances share a master: do not rebuild/recompile it once per item sprite.
        return parent
    lighting = _lighting_functions(unreal)  # Resolve required helper before replacing a graph.
    # This graph migration repairs both the ON branch and old fixed-brightness OFF.
    editing.delete_all_material_expressions(parent)
    parent.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_MASKED)
    # RenderPipelines CUTOUT_TERRAIN uses 0.5; entity/item cutouts use 0.1.
    # Native block models already provide reversed faces for crosses. Rendering
    # every face from both sides adds coincident duplicate fragments and flicker.
    parent.set_editor_property('opacity_mask_clip_value', 0.5 if use_vertex else 0.1)
    parent.set_editor_property('two_sided', not use_vertex)
    if translucent:
        parent.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE)
    def expression(cls):
        value = editing.create_material_expression(parent, cls, 0, 0)
        if value is None:
            raise RuntimeError('Cannot create model material expression')
        return value
    def connect(a, b, pin='', output=''):
        if not editing.connect_material_expressions(a, output, b, pin):
            raise RuntimeError('Cannot connect model material: ' + pin)
    sample = expression(unreal.MaterialExpressionTextureSampleParameter2D)
    sample.set_editor_property('parameter_name', 'FaceTexture'); sample.set_editor_property('texture', sample_texture)
    coordinate = expression(unreal.MaterialExpressionTextureCoordinate)
    clock = expression(unreal.MaterialExpressionTime)
    frames = expression(unreal.MaterialExpressionScalarParameter); frames.set_editor_property('parameter_name', 'AnimationFrames'); frames.set_editor_property('default_value', 1.0)
    duration = expression(unreal.MaterialExpressionScalarParameter); duration.set_editor_property('parameter_name', 'AnimationFrameTime'); duration.set_editor_property('default_value', 1.0)
    revision = expression(unreal.MaterialExpressionScalarParameter); revision.set_editor_property('parameter_name', 'BridgeAnimationRevision_v1'); revision.set_editor_property('default_value', 1.0)
    animation = expression(unreal.MaterialExpressionCustom); animation.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    animation.set_editor_property('code', 'return float2(UV.x,(clamp(UV.y,0,0.99999)+floor(fmod(Clock*20/max(Duration,1),max(Frames*Revision,1))))/max(Frames*Revision,1));')
    names = ['UV','Clock','Frames','Duration','Revision']; inputs = []
    for name in names:
        entry = unreal.CustomInput(); entry.set_editor_property('input_name', name); inputs.append(entry)
    animation.set_editor_property('inputs', inputs)
    for name, value in zip(names, [coordinate, clock, frames, duration, revision]): connect(value, animation, name)
    connect(animation, sample, 'UVs')
    color = expression(unreal.MaterialExpressionVectorParameter)
    color.set_editor_property('parameter_name', 'BlockColor'); color.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 1))
    tint = expression(unreal.MaterialExpressionScalarParameter)
    tint.set_editor_property('parameter_name', 'FaceTint'); tint.set_editor_property('default_value', 0.0)
    white = expression(unreal.MaterialExpressionConstant3Vector); white.set_editor_property('constant', unreal.LinearColor(1, 1, 1, 1))
    blend = expression(unreal.MaterialExpressionLinearInterpolate)
    connect(white, blend, 'A'); connect(color, blend, 'B'); connect(tint, blend, 'Alpha')
    colored = expression(unreal.MaterialExpressionMultiply)
    display_sample = lighting['texture_display_rgb'](unreal, editing, parent, sample)
    interpolate=expression(unreal.MaterialExpressionScalarParameter);interpolate.set_editor_property('parameter_name','AnimationInterpolate');interpolate.set_editor_property('default_value',0.0)
    next_uv=expression(unreal.MaterialExpressionCustom);next_uv.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    next_uv.set_editor_property('code','return float2(UV.x,(clamp(UV.y,0,0.99999)+fmod(floor(Clock*20/max(Duration,1))+1,max(Frames,1)))/max(Frames,1));')
    next_sources=[('UV',coordinate),('Clock',clock),('Frames',frames),('Duration',duration)]
    next_inputs=[]
    for name,value in next_sources:
        entry=unreal.CustomInput();entry.set_editor_property('input_name',name);next_inputs.append(entry)
    next_uv.set_editor_property('inputs',next_inputs)
    for name,value in next_sources: connect(value,next_uv,name)
    next_sample=expression(unreal.MaterialExpressionTextureSampleParameter2D);next_sample.set_editor_property('parameter_name','FaceTexture');next_sample.set_editor_property('texture',sample_texture)
    connect(next_uv,next_sample,'UVs')
    next_display=lighting['texture_display_rgb'](unreal,editing,parent,next_sample)
    animated=expression(unreal.MaterialExpressionCustom);animated.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    animated.set_editor_property('code','if(Enabled<.5) return Current;float amount=fmod(floor(Clock*20),max(Duration,1))/max(Duration,1);return floor(saturate(lerp(Current,Next,amount))*255+.0001)/255;')
    animation_sources=[('Current',display_sample),('Next',next_display),('Clock',clock),('Duration',duration),('Enabled',interpolate)]
    animation_inputs=[]
    for name,value in animation_sources:
        entry=unreal.CustomInput();entry.set_editor_property('input_name',name);animation_inputs.append(entry)
    animated.set_editor_property('inputs',animation_inputs)
    for name,value in animation_sources: connect(value,animated,name)
    display_sample=animated
    connect(display_sample, colored, 'A'); connect(blend, colored, 'B')
    lighting['wire_vanilla_lighting'](unreal, editing, parent, colored,
        use_vertex=use_vertex, pixel_display=True)
    alpha_target = unreal.MaterialProperty.MP_OPACITY if translucent else unreal.MaterialProperty.MP_OPACITY_MASK
    if not editing.connect_material_property(sample, 'A', alpha_target):
        raise RuntimeError('Cannot connect model-face alpha')
    roughness = expression(unreal.MaterialExpressionConstant); roughness.set_editor_property('r', 0.85)
    if not editing.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS):
        raise RuntimeError('Cannot connect model-face roughness')
    editing.recompile_material(parent)
    if not assets.save_loaded_asset(parent, False):
        raise RuntimeError('Cannot save model-face master')
    if not {'FaceTexture'}.issubset({str(name) for name in editing.get_texture_parameter_names(parent)}):
        raise RuntimeError('Model-face master has an incomplete texture graph')
    if not required_scalars.issubset({str(name) for name in editing.get_scalar_parameter_names(parent)}):
        raise RuntimeError('Model-face master has an incomplete tint/lighting graph')
    return parent


def _commit_texture_palette(unreal, assets, receiver, palette, values, atlas_import=None):
    """Restore registered palette data as well as the level on a failed import."""
    fields = list(values)
    if atlas_import is not None:
        fields.extend(("atlas_rects", "atlas_pages", "atlas_materials"))
    previous_values = {field: dict(palette.get_editor_property(field)) for field in fields}
    previous_palette = receiver.get_editor_property("texture_palette")
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    try:
        with unreal.ScopedEditorTransaction("Assign Minecraft texture palette"):
            for field, value in values.items():
                palette.set_editor_property(field, value)
            if atlas_import is not None:
                atlas_import()
            if not assets.save_loaded_asset(palette, False):
                raise RuntimeError("Cannot save texture palette")
            receiver.set_editor_property("texture_palette", palette)
        if not level.save_current_level() or receiver.get_editor_property("texture_palette") != palette:
            raise RuntimeError("Cannot save/verify current level with texture palette")
    except Exception:
        # Unreal's editor transaction does not automatically undo Python errors.
        # Copy the maps before assignment; a registered palette may be the same
        # UObject as the new destination and restoring only the Receiver is insufficient.
        failures = []
        for field, value in previous_values.items():
            try:
                palette.set_editor_property(field, value)
            except Exception:
                failures.append(field)
        try:
            receiver.set_editor_property("texture_palette", previous_palette)
        except Exception:
            failures.append("receiver")
        for name, save in (("palette save", lambda: assets.save_loaded_asset(palette, False)), ("level save", level.save_current_level)):
            try:
                if not save():
                    failures.append(name)
            except Exception:
                failures.append(name)
        if failures:
            getattr(unreal, "log_warning", unreal.log)("Texture import rollback could not save all previous values (" + ", ".join(failures) + "). Do not save the failed import; retain your backup.")
        raise


def import_minecraft_textures(filename, asset_root="/Game/Bridge/Minecraft"):
    if not isinstance(asset_root, str) or not re.fullmatch(r"/Game(?:/[A-Za-z0-9_]+)+", asset_root):
        raise ValueError("Invalid generated texture asset root")
    import unreal
    manifest = load_texture_manifest(filename)  # Validate all files before mutating UE assets.
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / "UEBridge.uproject").is_file() or not (project / "Source/UEBridge/BridgeBlockPalette.h").is_file():
        raise RuntimeError("Use the updated UEBridge project only")
    if not (project / 'bridge_lighting_materials.py').is_file() or (manifest['version'] == 2 and not (project / 'import_minecraft_atlas.py').is_file()):
        raise RuntimeError('Copy bridge_lighting_materials.py and import_minecraft_atlas.py next to UEBridge.uproject first')
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play before importing textures")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save your level first and stop Play before importing")
    receiver_class = getattr(unreal, "BridgeReceiver", None)
    palette_class = getattr(unreal, "BridgeBlockPalette", None)
    if receiver_class is None or palette_class is None:
        raise RuntimeError("Build the updated UEBridge first")
    try:
        palette_defaults = unreal.get_default_object(palette_class)
        for field in ("particle_textures", "particle_tints", "particle_colors", "face_materials", "blockstate_definitions", "models", "state_shapes"):
            palette_defaults.get_editor_property(field)
    except Exception as error:
        raise RuntimeError("Build UEBridge 0.9 and reopen the editor before importing models") from error
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, receiver_class)]
    if len(receivers) != 1:
        raise RuntimeError("The current saved level must have exactly one BridgeReceiver")
    assets, tools = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools()
    editing = unreal.MaterialEditingLibrary
    root = asset_root

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

        parent_path = root + "/M_MinecraftFaces_v4"
        parent = unreal.load_asset(parent_path) if assets.does_asset_exist(parent_path) else None
        if parent is None:
            parent = tools.create_asset("M_MinecraftFaces_v4", root, unreal.Material, unreal.MaterialFactoryNew())
        if not isinstance(parent, unreal.Material):
            raise RuntimeError("Master material path is occupied by a different type")
        if parent is not None:
            editing.delete_all_material_expressions(parent)
            parent.set_editor_property("used_with_instanced_static_meshes", True)
            parent.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
            parent.set_editor_property("opacity_mask_clip_value", 0.5)

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
            channels, alpha_channels = {}, {}
            lighting = _lighting_functions(unreal)
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
                display_sample = lighting['texture_display_rgb'](unreal, editing, parent, sample)
                wire(display_sample, product, "A"); wire(blend, product, "B")
                channels[face] = product
                alpha_channels[face] = sample
            normal = node(unreal.MaterialExpressionPixelNormalWS)
            z = node(unreal.MaterialExpressionComponentMask)
            z.set_editor_property("r", False); z.set_editor_property("g", False)
            z.set_editor_property("b", True); z.set_editor_property("a", False)
            wire(normal, z)
            # UE5.8's Python wrapper does not expose a stable Clamp input pin
            # name.  Max(value, 0) is equivalent to saturating the normal
            # component here and compiles consistently across SM5 targets.
            zero = node(unreal.MaterialExpressionConstant); zero.set_editor_property("r", 0.0)
            top = node(unreal.MaterialExpressionMax); wire(z, top, "A"); wire(zero, top, "B")
            negative = node(unreal.MaterialExpressionMultiply); negative.set_editor_property("const_b", -1.0); wire(z, negative, "A")
            bottom = node(unreal.MaterialExpressionMax); wire(negative, bottom, "A"); wire(zero, bottom, "B")
            first = node(unreal.MaterialExpressionLinearInterpolate)
            wire(channels["Side"], first, "A"); wire(channels["Top"], first, "B"); wire(top, first, "Alpha")
            result = node(unreal.MaterialExpressionLinearInterpolate)
            wire(first, result, "A"); wire(channels["Bottom"], result, "B"); wire(bottom, result, "Alpha")
            if not editing.connect_material_property(result, "", unreal.MaterialProperty.MP_BASE_COLOR):
                raise RuntimeError("Cannot connect material Base Color")
            opacity_first = node(unreal.MaterialExpressionLinearInterpolate)
            wire(alpha_channels["Side"], opacity_first, "A", "A"); wire(alpha_channels["Top"], opacity_first, "B", "A"); wire(top, opacity_first, "Alpha")
            opacity = node(unreal.MaterialExpressionLinearInterpolate)
            wire(opacity_first, opacity, "A"); wire(alpha_channels["Bottom"], opacity, "B", "A"); wire(bottom, opacity, "Alpha")
            if not editing.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK):
                raise RuntimeError("Cannot connect cube alpha mask")
            lighting['wire_vanilla_lighting'](unreal, editing, parent, result, use_vertex=False, pixel_display=True)
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
            digest = hashlib.sha256(json.dumps({"master_version": 4, "faces": content}, sort_keys=True).encode()).hexdigest()
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
            texture_values = []
            scalar_values = []
            for face in ("top", "side", "bottom"):
                texture_values.append(unreal.TextureParameterValue(
                    parameter_info=unreal.MaterialParameterInfo(name=face.title() + "Texture"),
                    parameter_value=imported[entry[face]["texture"]]))
                scalar_values.append(unreal.ScalarParameterValue(
                    parameter_info=unreal.MaterialParameterInfo(name=face.title() + "Tint"),
                    parameter_value=float(entry[face]["tint"])))
            # Explicit overrides avoid the failed parameter lookup in the editor setter.
            material.set_editor_property("texture_parameter_values", texture_values)
            material.set_editor_property("scalar_parameter_values", scalar_values)
            editing.update_material_instance(material)
            for face in ("top", "side", "bottom"):
                parameter = face.title() + "Texture"
                actual = editing.get_material_instance_texture_parameter_value(material, parameter)
                expected = imported[entry[face]["texture"]]
                if actual != expected:
                    raise RuntimeError("Texture override readback failed: " + identifier + ":" + parameter)
            if not assets.save_loaded_asset(material, False):
                raise RuntimeError("Cannot save block material")
            palette_materials[identifier] = material

        # Arbitrary cuboid/plane model faces use their own UVs rather than cube-normal
        # texture selection. Keep this master separate so imported v1 palettes remain usable.
        alpha_modes = {entry.get("alphaMode", "cutout") for entry in manifest["textures"].values()}
        face_parents = {mode: _model_parent(unreal, assets, tools, editing, root, next(iter(imported.values())), mode) for mode in alpha_modes}
        face_materials = {}
        for identifier, texture in imported.items():
            alpha_mode = manifest["textures"][identifier].get("alphaMode", "cutout")
            face_parent = face_parents[alpha_mode]
            for tinted in (False, True):
                digest = hashlib.sha256((manifest["textures"][identifier]["sha256"] + "model-v2:" + alpha_mode + ":" + str(tinted)).encode()).hexdigest()
                name = asset_name("MI_Model_", identifier, digest)
                folder = root + "/Materials"
                target = folder + "/" + name
                instance = unreal.load_asset(target) if assets.does_asset_exist(target) else None
                if instance is None:
                    instance = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
                if not isinstance(instance, unreal.MaterialInstanceConstant):
                    raise RuntimeError("Cannot create model-face instance")
                editing.set_material_instance_parent(instance, face_parent)
                instance.set_editor_property("texture_parameter_values", [unreal.TextureParameterValue(parameter_info=unreal.MaterialParameterInfo(name="FaceTexture"), parameter_value=texture)])
                # Assign the complete list together: replacing scalar values
                # with FaceTint alone used to erase the animation parameters.
                scalars={'FaceTint':float(tinted),'AnimationFrames':float(manifest['textures'][identifier].get('animationFrames',1)),
                         'AnimationFrameTime':float(manifest['textures'][identifier].get('animationFrameTime',1)),
                         'AnimationInterpolate':float(manifest['textures'][identifier].get('animationInterpolate',False))}
                instance.set_editor_property("scalar_parameter_values",[unreal.ScalarParameterValue(parameter_info=unreal.MaterialParameterInfo(name=key),parameter_value=value) for key,value in scalars.items()])
                editing.update_material_instance(instance)
                if editing.get_material_instance_texture_parameter_value(instance, "FaceTexture") != texture or not assets.save_loaded_asset(instance, False):
                    raise RuntimeError("Cannot save/read back model-face instance")
                face_materials[identifier + ("#1" if tinted else "#0")] = instance

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
    # This is a separate sprite map: block dust must not reuse the block's three-face material.
    # Older texture exports have no particle entry and remain usable with their side texture.
    particle_textures = dict(palette.get_editor_property("particle_textures"))
    particle_tints = dict(palette.get_editor_property("particle_tints"))
    particle_colors = dict(palette.get_editor_property("particle_colors"))
    for identifier, entry in manifest["blocks"].items():
        particle = entry.get("particle", {"texture": entry["side"]["texture"], "tint": False, "color": 0xffffff})
        particle_textures[identifier] = imported[particle["texture"]]
        particle_tints[identifier] = particle["tint"]
        value = particle["color"] if particle["tint"] else 0xffffff
        particle_colors[identifier] = unreal.Color((value >> 16) & 255, (value >> 8) & 255, value & 255, 255)
    merged_faces = dict(palette.get_editor_property("face_materials")); merged_faces.update(face_materials)
    values = dict(materials=materials, particle_textures=particle_textures,
                  particle_tints=particle_tints, particle_colors=particle_colors, face_materials=merged_faces)
    atlas_import = None
    if manifest["version"] == 2:
        for field, source in (("blockstate_definitions", manifest["blockstates"]), ("models", manifest["models"]), ("state_shapes", manifest["blocks"])):
            merged = dict(palette.get_editor_property(field))
            merged.update({key: json.dumps(value, separators=(",", ":"), ensure_ascii=True) for key, value in source.items()})
            values[field] = merged
        import runpy
        atlas_script = project / "import_minecraft_atlas.py"
        if not atlas_script.is_file():
            raise RuntimeError("Copy import_minecraft_atlas.py next to UEBridge.uproject first")
        atlas_function = runpy.run_path(str(atlas_script))["import_minecraft_atlas"]
        atlas_import = lambda: atlas_function(unreal, manifest, palette, root)
    _commit_texture_palette(unreal, assets, receivers[0], palette, values, atlas_import)
    unreal.log("Minecraft texture/model palette ready: " + str(len(palette_materials)) + " block types, " + str(len(manifest.get("models", {}))) + " models. Press Play and /uebridge world refresh.")
