"""Preflight an exact local native package, stage its UE assets, then publish its own map.

Manual UE Python: import_native_play('C:/.../native_manifest.json').
Play-Native.cmd supplies UEBRIDGE_NATIVE_MANIFEST and UEBRIDGE_NATIVE_AUTOMATION=1.
All package references are validated before importing Unreal or changing a level.
No Minecraft content is downloaded or added to the source distribution.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import pathlib
import re
import runpy
import traceback
import uuid

NATIVE_MAP = '/Game/Bridge/Native/NativePlay'
MANAGED_TAG = 'BridgeNativeManaged_v1'
ASSET_NAMES = ('textures', 'items', 'mobs', 'player', 'ui', 'sounds')
HELPERS = ('native_world_format.py', 'import_minecraft_textures.py', 'import_minecraft_atlas.py',
           'import_minecraft_items.py', 'import_minecraft_mobs.py', 'import_minecraft_player.py',
           'import_minecraft_ui.py', 'import_minecraft_sounds.py', 'setup_world_bridge.py',
           'bridge_lighting_materials.py', 'setup_vanilla_effects.py',
           'setup_bridge_rendering.py', 'setup_native_explosion.py', 'setup_realistic_physics.py')
REGISTRY_ID = re.compile(r'[a-z0-9_.-]+:[a-z0-9_./-]+\Z')


def _helper_directory():
    filename = globals().get('__file__')
    if filename and pathlib.Path(filename).is_file():
        return pathlib.Path(filename).resolve().parent
    import unreal
    return pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()


def _json(path, maximum):
    if not path.is_file() or not 1 <= path.stat().st_size <= maximum:
        raise ValueError('Missing/oversized native manifest: ' + str(path))
    return json.loads(path.read_bytes(), parse_constant=lambda value: (_ for _ in ()).throw(ValueError('Nonfinite native JSON number')))


def _digest(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(65536), b''):
            digest.update(data)
    return digest.hexdigest()


def _reference(root, entry, label, maximum):
    if not isinstance(entry, dict) or not isinstance(entry.get('file'), str) or not isinstance(entry.get('sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', entry['sha256']):
        raise ValueError('Invalid native ' + label + ' reference/checksum')
    relative = pathlib.PurePosixPath(entry['file'])
    if relative.is_absolute() or '..' in relative.parts or '\\' in entry['file'] or ':' in entry['file']:
        raise ValueError('Native ' + label + ' path escapes its package')
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or not path.is_file() or not 1 <= path.stat().st_size <= maximum:
        raise ValueError('Missing/oversized/escaping native ' + label)
    if type(entry.get('bytes')) is not int or entry['bytes'] != path.stat().st_size or _digest(path) != entry['sha256']:
        raise ValueError('Native ' + label + ' byte count/checksum mismatch; re-export the complete package')
    return path


def _number(value, low, high, name):
    if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError('Invalid native setting ' + name)


def _settings(settings):
    if not isinstance(settings, dict):
        raise ValueError('Missing native player settings')
    keys = settings.get('keyBindings')
    if not isinstance(keys, dict) or not 1 <= len(keys) <= 256:
        raise ValueError('Missing/oversized native key bindings')
    for action, key in keys.items():
        if not isinstance(action, str) or not action.startswith('key.') or len(action) > 128 or not isinstance(key, str) or len(key) > 128 or not re.fullmatch(r'key\.(?:keyboard|mouse)\.[a-z0-9_.-]+', key):
            raise ValueError('Invalid native key binding')
    for name, low, high in (('mouseSensitivity', 0, 1), ('fov', 30, 110)):
        _number(settings.get(name), low, high, name)
    for name in ('invertYMouse', 'slimArms'):
        if type(settings.get(name)) is not bool:
            raise ValueError('Invalid native boolean setting ' + name)
    if 'smoothCamera' in settings and type(settings['smoothCamera']) is not bool:
        raise ValueError('Invalid native boolean setting smoothCamera')
    if 'attackIndicator' in settings and settings['attackIndicator'] not in ('off', 'crosshair', 'hotbar'):
        raise ValueError('Invalid native attack indicator')
    if settings.get('mainHand') not in ('left', 'right') or settings.get('gameMode') not in ('creative', 'survival'):
        raise ValueError('Invalid native hand/game mode setting')
    if type(settings.get('skinLayers')) is not int or not 0 <= settings['skinLayers'] <= 127:
        raise ValueError('Invalid native skin layer setting')
    for name, limit in (('inventory', 36), ('hotbar', 9)):
        stacks = settings.get(name, [])
        if not isinstance(stacks, list) or len(stacks) > limit:
            raise ValueError('Invalid native ' + name)
        slots = set()
        for stack in stacks:
            if not isinstance(stack, dict) or type(stack.get('slot')) is not int or not 0 <= stack['slot'] < limit or stack['slot'] in slots:
                raise ValueError('Invalid/duplicate native inventory slot')
            slots.add(stack['slot'])
            item = stack.get('item', stack.get('id', ''))
            if not isinstance(item, str) or (item and (not REGISTRY_ID.fullmatch(item) or '..' in item)) or type(stack.get('count')) is not int or not 0 <= stack['count'] <= 99:
                raise ValueError('Invalid native inventory stack')
    if 'equipment' in settings:
        equipment = settings['equipment']
        if not isinstance(equipment, list) or len(equipment) != 4:
            raise ValueError('Invalid native equipment slots')
        for stack in equipment:
            if not isinstance(stack, dict):
                raise ValueError('Invalid native equipment stack')
            item = stack.get('item', stack.get('id', ''))
            if not isinstance(item, str) or (item and (not REGISTRY_ID.fullmatch(item) or '..' in item)) or type(stack.get('count')) is not int or not 0 <= stack['count'] <= 1:
                raise ValueError('Invalid native equipment stack')


def load_native_manifest(filename, helper_directory=None):
    """Validate bytes, all nested assets, world completeness and cross-package links."""
    path = pathlib.Path(filename).expanduser().resolve()
    manifest = _json(path, 16 * 1024 * 1024)
    if not isinstance(manifest, dict) or manifest.get('kind') != 'native-play' or manifest.get('schema') != 'uebridge.native.v1' or type(manifest.get('version')) is not int or manifest['version'] != 1:
        raise ValueError('Unsupported native-play package; use /uebridge native export')
    package_id = manifest.get('id')
    try:
        if not isinstance(package_id, str) or str(uuid.UUID(package_id)) != package_id.lower():
            raise ValueError
    except (ValueError, AttributeError):
        raise ValueError('Native package ID must be a UUID') from None
    if type(manifest.get('radiusChunks')) is not int or not 4 <= manifest['radiusChunks'] <= 6:
        raise ValueError('Native package radius must be 4..6 chunks')
    _settings(manifest.get('settings'))
    helpers_dir = pathlib.Path(helper_directory or _helper_directory()).resolve()
    for helper in HELPERS:
        if not (helpers_dir / helper).is_file():
            raise RuntimeError('Missing native setup helper: ' + helper + '. Extract the complete UE update beside UEBridge.uproject.')
    references = manifest.get('assets')
    if not isinstance(references, dict) or set(references) != set(ASSET_NAMES):
        raise ValueError('Native package requires exact texture/item/mob/player/UI/sound manifests')
    paths = {name: _reference(path.parent, references[name], name, 64 * 1024 * 1024) for name in ASSET_NAMES}
    world_entry = manifest.get('world')
    if not isinstance(world_entry, dict) or world_entry.get('complete') is not True:
        raise ValueError('Native world export is incomplete')
    world_path = _reference(path.parent, world_entry, 'world', 512 * 1024 * 1024)
    helpers = {}
    parsed = {}
    for name, loader in (('textures', 'load_texture_manifest'), ('items', 'load_item_manifest'),
                         ('mobs', 'load_mob_manifest'), ('player', 'load_player_manifest'),
                         ('ui', 'load_ui_manifest'), ('sounds', 'load_sound_manifest')):
        helpers[name] = runpy.run_path(str(helpers_dir / ('import_minecraft_' + name + '.py')))
        parsed[name] = helpers[name][loader](str(paths[name]))
    helpers['mobs']['validate_mob_baseline'](parsed['mobs'])
    for sprite in ('hud/hotbar', 'hud/hotbar_selection', 'hud/crosshair'):
        if sprite not in parsed['ui']['sprites']:
            raise ValueError('Missing required native HUD sprite: ' + sprite)
    if parsed['textures']['version'] != 2:
        raise ValueError('Native play requires the baked model/blockstate texture export')
    world_functions = runpy.run_path(str(helpers_dir / 'native_world_format.py'))
    world = world_functions['validate_world_file'](world_path)
    header = world['header']
    if header['id'].lower() != package_id.lower() or header['radius'] != manifest['radiusChunks'] * 2:
        raise ValueError('Native world ID/radius does not match its package')
    if world_entry.get('cells') != world['cells'] or world_entry.get('blocks') != world['rows']:
        raise ValueError('Native world counts do not match its completed export')
    appearances = parsed['mobs']['appearances']
    for mob in header.get('mobs', []):
        if mob['appearance'] not in appearances or appearances[mob['appearance']]['type'] != mob['type']:
            raise ValueError('Native mob snapshot references an absent/wrong appearance')
    states = parsed['textures']['blocks']
    checked = set()
    with world_path.open('rb') as stream:
        next(stream)
        for line in stream:
            for entry in json.loads(line)['palette']:
                key = (entry[0], entry[1])
                if key not in checked:
                    if key[0] not in states or key[1] not in states[key[0]].get('states', {}):
                        raise ValueError('Missing offline block/state model: ' + str(key) + '; re-export assets and terrain together')
                    checked.add(key)
    for item in parsed['ui']['items']:
        if item.get('modelKey') and item['modelKey'] not in parsed['items']['items']:
            raise ValueError('Native UI item references an absent item model: ' + item['id'])
    sounds = manifest.get('blockSounds')
    if not isinstance(sounds, dict) or len(sounds) > 4096:
        raise ValueError('Invalid native block sound registry')
    for block, entry in sounds.items():
        if not isinstance(block, str) or not REGISTRY_ID.fullmatch(block) or not isinstance(entry, dict):
            raise ValueError('Invalid native block sound entry')
        for event in ('break', 'place', 'step', 'hit', 'fall'):
            if not isinstance(entry.get(event), str) or not REGISTRY_ID.fullmatch(entry[event]):
                raise ValueError('Invalid native block sound event')
        for name, low, high in (('volume', 0, 16), ('pitch', .1, 4)):
            _number(entry.get(name), low, high, 'block sound ' + name)
    return dict(manifest=manifest, path=path, manifestSha256=_digest(path), paths=paths,
                world=world, parsed=parsed, helpers=helpers, helperDirectory=helpers_dir)


def _save_marker(project, package):
    saved = project / 'Saved'
    saved.mkdir(parents=True, exist_ok=True)
    marker = saved / 'NativeLauncher.json'
    temporary = saved / ('NativeLauncher.json.tmp-' + uuid.uuid4().hex)
    try:
        with temporary.open('x', encoding='utf-8') as stream:
            json.dump(dict(completed=True, manifest=str(package['path']), manifestSha256=package['manifestSha256'],
                           map=NATIVE_MAP, packageId=package['manifest']['id'],
                           importAttemptId=os.environ.get('UEBRIDGE_NATIVE_ATTEMPT', '')), stream, ensure_ascii=False, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(marker)
    finally:
        temporary.unlink(missing_ok=True)


def _check_initial_level(unreal, editor):
    """Only the launcher's fresh unnamed startup template may be discarded.

    Unreal can mark its initial OpenWorld template dirty without user edits. A
    separate unattended process must be able to replace that temporary map;
    an interactive editor or any dirty saved/user map still requires a save.
    """
    if editor.get_game_world() is not None:
        raise RuntimeError('Stop Play before native setup')
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    if not dirty:
        return
    world = editor.get_editor_world()
    world_path = world.get_path_name() if world is not None else ''
    temporary = world_path.split('.', 1)[0]
    automation = os.environ.get('UEBRIDGE_NATIVE_AUTOMATION') == '1'
    startup_template = bool(re.fullmatch(r'/Temp/Untitled(?:_\d+)?', temporary))
    package_names = [package.get_path_name() for package in dirty]
    if automation and startup_template and all(name == temporary for name in package_names):
        unreal.log('Native setup: replacing the fresh temporary startup template ' + temporary + '; no saved user map is changed')
        return
    raise RuntimeError('Save current map changes before native setup; dirty user maps are preserved')


def import_native_play(filename):
    package = load_native_manifest(filename)
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    if not (project / 'UEBridge.uproject').is_file():
        raise RuntimeError('Run native setup inside the built UEBridge project')
    for name in ('BridgeReceiver', 'BridgeGameMode', 'BridgeNativeUiPalette', 'BridgeNativeSoundPalette', 'BridgeMobPalette'):
        if getattr(unreal, name, None) is None:
            raise RuntimeError('Build UEBridge 0.12 before native setup; missing ' + name)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    _check_initial_level(unreal, editor)
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(NATIVE_MAP):
        if not level.load_level(NATIVE_MAP):
            raise RuntimeError('Cannot inspect the existing native setup map')
        receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.BridgeReceiver)]
        if len(receivers) != 1 or MANAGED_TAG not in {str(tag) for tag in receivers[0].get_editor_property('tags')}:
            raise RuntimeError(NATIVE_MAP + ' is occupied by an unmanaged map; it has been preserved')
    saved_marker = project / 'Saved/NativeLauncher.json'
    saved_marker.unlink(missing_ok=True)
    package_root = '/Game/Bridge/Native/Packages/P_' + uuid.UUID(package['manifest']['id']).hex
    staging_map = package_root + '/Setup_' + uuid.uuid4().hex[:12]
    unreal.log('Native setup preflight complete: all checksums, models, terrain and settings validated; package=' + str(package['path']))
    if not level.new_level(staging_map):
        raise RuntimeError('Cannot create the separate native staging map')
    game_world = editor.get_editor_world()
    if game_world is None:
        raise RuntimeError('Cannot obtain native staging editor world')
    world_settings = game_world.get_world_settings()
    if world_settings is None:
        raise RuntimeError('Cannot obtain native level world settings')
    world_settings.set_editor_property('default_game_mode', unreal.BridgeGameMode)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 200))
    if start is None:
        raise RuntimeError('Cannot create native PlayerStart')
    start.set_actor_label('Native Player Start')
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, -30, 0))
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300))
    atmosphere = actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    if sun is None or sky is None or atmosphere is None:
        raise RuntimeError('Cannot create native UE lighting/sky actors')
    for actor in (sun, sky, atmosphere):
        actor.set_editor_property('tags', [unreal.Name('BridgeNativeLighting')])
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property('atmosphere_sun_light', True)
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('real_time_capture', True)
    if not level.save_current_level():
        raise RuntimeError('Cannot save native staging map before importing assets')
    helper_dir = package['helperDirectory']
    receiver = runpy.run_path(str(helper_dir / 'setup_world_bridge.py'))['setup_world_bridge']()
    receiver.set_actor_label('Native Minecraft Bridge')
    receiver.set_editor_property('tags', [unreal.Name(MANAGED_TAG)])
    receiver.set_editor_property('prefer_native_play', False)
    if not level.save_current_level():
        raise RuntimeError('Cannot save the staged native receiver')
    helper_functions, paths = package['helpers'], package['paths']
    asset_root = package_root + '/Minecraft'
    steps = (
        ('textures', lambda: helper_functions['textures']['import_minecraft_textures'](str(paths['textures']), asset_root=asset_root)),
        ('items', lambda: helper_functions['items']['import_minecraft_items'](str(paths['items']), asset_root=asset_root + '/Items')),
        ('mobs', lambda: helper_functions['mobs']['import_minecraft_mobs'](str(paths['mobs']))),
        ('player', lambda: helper_functions['player']['import_minecraft_player'](str(paths['player']))),
        ('ui', lambda: helper_functions['ui']['import_minecraft_ui'](str(paths['ui']))),
        ('sounds', lambda: helper_functions['sounds']['import_minecraft_sounds'](str(paths['sounds']))),
        ('effects', lambda: runpy.run_path(str(helper_dir / 'setup_vanilla_effects.py'))['setup_vanilla_effects']()),
        ('rendering', lambda: runpy.run_path(str(helper_dir / 'setup_bridge_rendering.py'))['setup_bridge_rendering'](asset_root=asset_root)),
        ('explosion', lambda: runpy.run_path(str(helper_dir / 'setup_native_explosion.py'))['setup_native_explosion']()),
    )
    stage = 'create'
    try:
        for stage, action in steps:
            unreal.log('Native setup stage=' + stage)
            action()
        for field in ('texture_palette', 'mob_palette', 'player_appearance', 'native_ui_palette', 'native_sound_palette', 'preview_material', 'outline_material', 'vanilla_particle_material'):
            if receiver.get_editor_property(field) is None:
                raise RuntimeError('Native setup did not assign ' + field)
        # Re-read the immutable top-level reference before publishing, so a
        # replacement during imports cannot leave a successful launcher marker.
        checked_package = load_native_manifest(package['path'], helper_dir)
        if checked_package['manifestSha256'] != package['manifestSha256']:
            raise RuntimeError('Native manifest changed during import; export a new package')
        receiver.set_editor_property('native_world_file', str(package['path']))
        receiver.set_editor_property('prefer_native_play', True)
        if not level.save_current_level() or receiver.get_editor_property('native_world_file') != str(package['path']) or not receiver.get_editor_property('prefer_native_play'):
            raise RuntimeError('Cannot save/verify native receiver package assignment')
        # Only the marked, generated native map can be replaced. User maps such
        # as /Game/UE are never saved, cloned or overwritten by this setup.
        if not unreal.EditorLoadingAndSavingUtils.save_map(game_world, NATIVE_MAP):
            raise RuntimeError('Cannot publish the native play map')
        if not level.load_level(NATIVE_MAP):
            raise RuntimeError('Cannot reopen the published native play map')
        saved_receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.BridgeReceiver)]
        if len(saved_receivers) != 1 or saved_receivers[0].get_editor_property('native_world_file') != str(package['path']):
            raise RuntimeError('Published native map package verification failed')
        _save_marker(project, package)
        unreal.log('Native setup COMPLETE: map=' + NATIVE_MAP + ' package=' + str(package['path']) + '. Minecraft can stay closed; launch Play-Native.cmd.')
        return saved_receivers[0]
    except Exception as error:
        unreal.log_error('Native setup FAILED stage=' + stage + ': ' + str(error) + '. Previous native map retained until the publish stage; user maps untouched. Staging map=' + staging_map)
        raise


def _automation_entry():
    import unreal
    filename = os.environ.get('UEBRIDGE_NATIVE_MANIFEST')
    automated = os.environ.get('UEBRIDGE_NATIVE_AUTOMATION') == '1'
    if not filename:
        command_line = unreal.SystemLibrary.get_command_line()
        match = re.search(r'(?:^|\s)-BridgeNativeManifest=(?:"([^"]+)"|([^\s]+))', command_line, re.IGNORECASE)
        if match:
            filename = match.group(1) or match.group(2)
    try:
        if not filename:
            raise RuntimeError('No native package selected; call import_native_play(path) or launch Play-Native.cmd')
        import_native_play(filename)
    except Exception as error:
        # Record the full traceback before requesting editor shutdown.
        unreal.log_error('Native automation failed: ' + str(error))
        unreal.log_error(traceback.format_exc())
        raise
    finally:
        if automated:
            unreal.SystemLibrary.quit_editor()


if __name__ == '__main__':
    requested = bool(os.environ.get('UEBRIDGE_NATIVE_MANIFEST') or os.environ.get('UEBRIDGE_NATIVE_AUTOMATION') == '1')
    if not requested:
        try:
            import unreal
            requested = '-bridgenativemanifest=' in unreal.SystemLibrary.get_command_line().lower()
        except ImportError:
            pass
    if requested:
        _automation_entry()
