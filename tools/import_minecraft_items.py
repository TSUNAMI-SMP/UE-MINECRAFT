"""Import native item geometry and local textures; retain the current level/block palette."""
import hashlib
import gzip
import json
import math
import pathlib
import re
import struct
import zlib

HAND_CONTEXTS = ('firstperson_righthand', 'firstperson_lefthand', 'thirdperson_righthand', 'thirdperson_lefthand')
CONTEXTS = HAND_CONTEXTS + ('ground',)
MAX_PAYLOAD_BYTES = 256 * 1024 * 1024

def load_item_manifest(filename):
    path = pathlib.Path(filename).expanduser().resolve()
    if not path.is_file() or path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('Missing/oversized item manifest')
    manifest = json.loads(path.read_bytes())
    if isinstance(manifest, dict) and manifest.get('kind') == 'items' and type(manifest.get('version')) is int and manifest['version'] == 2:
        if manifest.get('payload') != 'items.json.gz' or type(manifest.get('uncompressedBytes')) is not int or not 1 <= manifest['uncompressedBytes'] <= MAX_PAYLOAD_BYTES:
            raise ValueError('Invalid compressed item payload metadata')
        source = (path.parent / manifest['payload']).resolve()
        if not source.is_relative_to(path.parent) or not source.is_file() or source.stat().st_size > 64 * 1024 * 1024:
            raise ValueError('Missing/oversized or escaping compressed item payload')
        if not isinstance(manifest.get('sha256'), str) or hashlib.sha256(source.read_bytes()).hexdigest() != manifest['sha256']:
            raise ValueError('Compressed item payload checksum mismatch')
        try:
            with gzip.open(source, 'rb') as stream:
                data = stream.read(manifest['uncompressedBytes'] + 1)
            if len(data) != manifest['uncompressedBytes']:
                raise ValueError('Compressed item payload size mismatch')
            manifest = json.loads(data)
        except (OSError, EOFError) as error:
            raise ValueError('Invalid compressed item payload') from error
    if not isinstance(manifest, dict) or manifest.get('kind') != 'items' or type(manifest.get('version')) is not int or manifest['version'] != 1:
        raise ValueError('Unsupported item manifest')
    textures, items = manifest.get('textures'), manifest.get('items')
    if not isinstance(textures, dict) or len(textures) > 16384 or not isinstance(items, dict) or not 1 <= len(items) <= 4096:
        raise ValueError('Invalid item/texture registry')
    budget = 0
    for key, entry in textures.items():
        if not isinstance(key, str) or not re.fullmatch(r'[0-9a-f]{64}', key) or not isinstance(entry, dict) or entry.get('sha256') != key or entry.get('file') != f'textures/{key}.png':
            raise ValueError('Invalid item texture path/hash')
        source = (path.parent / entry['file']).resolve()
        if not source.is_relative_to(path.parent):
            raise ValueError('Item texture symlink escapes export')
        if not source.is_file() or source.stat().st_size > 4 * 1024 * 1024:
            raise ValueError('Missing/oversized item PNG')
        data = source.read_bytes(); budget += len(data)
        if budget > 256 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != key or data[:8] != b'\x89PNG\r\n\x1a\n':
            raise ValueError('Item PNG checksum/budget mismatch')
        cursor, size, ended = 8, None, False
        while cursor + 12 <= len(data):
            length = struct.unpack_from('>I', data, cursor)[0]; kind = data[cursor + 4:cursor + 8]; end = cursor + 12 + length
            if end > len(data) or zlib.crc32(data[cursor + 4:cursor + 8 + length]) & 0xffffffff != struct.unpack_from('>I', data, cursor + 8 + length)[0]:
                raise ValueError('Invalid item PNG chunk')
            if size is None:
                if kind != b'IHDR' or length != 13:
                    raise ValueError('Missing item PNG header')
                size = struct.unpack_from('>II', data, cursor + 8)
            if kind == b'IEND':
                if length or end != len(data):
                    raise ValueError('Invalid item PNG end')
                ended = True; break
            cursor = end
        if type(entry.get('width')) is not int or type(entry.get('height')) is not int or not ended or size != (entry.get('width'), entry.get('height')) or any(type(v) is not int or not 1 <= v <= 2048 for v in size):
            raise ValueError('Item PNG dimensions/end mismatch')
        entry['source'] = str(source)
    count = 0
    def vector(values, size):
        if not isinstance(values, list) or len(values) != size or any(type(v) not in (int, float) or not math.isfinite(v) or abs(v) > 4096 for v in values):
            raise ValueError('Invalid native item geometry')
    for key, contexts in items.items():
        if not isinstance(key, str) or not re.fullmatch(r'[a-z0-9_.-]+:[a-z0-9_./-]+@[0-9a-f]{64}', key) or '..' in key or not isinstance(contexts, dict) or set(contexts) not in (set(HAND_CONTEXTS), set(CONTEXTS)):
            raise ValueError('Invalid item ID/display contexts')
        for faces in contexts.values():
            if not isinstance(faces, list) or not 1 <= len(faces) <= 8192:
                raise ValueError('Invalid native item face count')
            count += len(faces)
            if count > 500000:
                raise ValueError('Native item geometry budget exceeded')
            for face in faces:
                if not isinstance(face, dict) or not isinstance(face.get('texture'), str) or face.get('texture') not in textures or type(face.get('color')) is not int or not 0 <= face['color'] <= 0xffffff:
                    raise ValueError('Invalid native item face texture/tint')
                for name, size in (('vertices', 3), ('uv', 2)):
                    values = face.get(name)
                    if not isinstance(values, list) or len(values) != 4:
                        raise ValueError('Invalid native item quad')
                    for value in values:
                        vector(value, size)
    return manifest


def ground_model_count(manifest):
    return sum('ground' in contexts for contexts in manifest['items'].values())


def import_minecraft_items(filename):
    manifest = load_item_manifest(filename)
    import runpy
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / 'UEBridge.uproject').is_file():
        raise RuntimeError('Use the updated UEBridge project')
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None or unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError('Stop Play and save your level first')
    receivers = [a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a, unreal.BridgeReceiver)]
    if len(receivers) != 1:
        raise RuntimeError('The saved level needs exactly one BridgeReceiver')
    receiver = receivers[0]; palette = receiver.get_editor_property('texture_palette')
    if palette is None:
        raise RuntimeError('Import block textures before items')
    palette.get_editor_property('item_models')  # Require the updated C++ before importing anything.
    assets, tools, editing = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools(), unreal.MaterialEditingLibrary
    helper = runpy.run_path(str(project / 'import_minecraft_textures.py'))
    root = '/Game/Bridge/Minecraft/Items'; materials = {}
    with unreal.ScopedSlowTask(len(manifest['textures']), 'Import Minecraft item textures') as progress:
        progress.make_dialog(True)
        for key, entry in manifest['textures'].items():
            if progress.should_cancel():
                raise RuntimeError('Item import cancelled; existing palette assignment retained')
            progress.enter_progress_frame(1, key)
            name = 'T_Item_' + key[:24]; target = root + '/' + name
            texture = unreal.load_asset(target) if assets.does_asset_exist(target) else None
            if texture is None:
                task = unreal.AssetImportTask()
                for field, value in dict(filename=entry['source'], destination_path=root, destination_name=name, automated=True, save=True, replace_existing=False).items():
                    task.set_editor_property(field, value)
                tools.import_asset_tasks([task]); texture = unreal.load_asset(target)
            if not isinstance(texture, unreal.Texture2D):
                raise RuntimeError('Cannot import item texture: ' + key)
            texture.set_editor_property('srgb', True); texture.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
            texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
            if not assets.save_loaded_asset(texture, False):
                raise RuntimeError('Cannot save item texture')
            parent = helper['_model_parent'](unreal, assets, tools, editing, root, texture, 'masked')
            name = 'MI_Item_' + key[:24]; target = root + '/' + name
            material = unreal.load_asset(target) if assets.does_asset_exist(target) else None
            if material is None:
                material = tools.create_asset(name, root, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            if not isinstance(material, unreal.MaterialInstanceConstant):
                raise RuntimeError('Cannot create item material')
            editing.set_material_instance_parent(material, parent)
            editing.set_material_instance_texture_parameter_value(material, 'FaceTexture', texture)
            editing.set_material_instance_scalar_parameter_value(material, 'FaceTint', 1.0)
            editing.update_material_instance(material)
            if not assets.save_loaded_asset(material, False):
                raise RuntimeError('Cannot save item material')
            materials[key] = material
    with unreal.ScopedEditorTransaction('Assign native Minecraft item models'):
        palette.set_editor_property('item_models', {key: json.dumps(value, separators=(',', ':'), allow_nan=False) for key, value in manifest['items'].items()})
        palette.set_editor_property('item_materials', materials)
        if not assets.save_loaded_asset(palette, False):
            raise RuntimeError('Cannot save native item palette')
    ground = ground_model_count(manifest)
    if ground < len(manifest['items']):
        unreal.log_warning(f"Legacy hand models retained: {len(manifest['items']) - ground} items lack native GROUND display. Drops are rejected and refunded for those items; run /uebridge items export with MOD 0.11.0 and import again.")
    unreal.log(f"Native ground models: {ground}/{len(manifest['items'])}")
    unreal.log(f"Native item models ready: {len(manifest['items'])}; exclusions: {len(manifest.get('excluded', {}))}. See local manifest.json for unsupported renderers.")
