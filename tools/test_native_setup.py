"""Validate real native packages and setup publication without requiring an editor.

The fake editor checks staging/ownership and the launcher completion contract;
it does not claim to compile shaders or verify Unreal's Python bindings.
"""
import copy
import hashlib
import importlib.util
import json
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest
import uuid
import wave
import zlib
from unittest.mock import patch

from native_world_format import write_world_file

spec = importlib.util.spec_from_file_location('native_setup', pathlib.Path(__file__).with_name('import_native_play.py'))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def png(width=2, height=2):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    pixels = b''.join(b'\0' + b'\xff\xff\xff\xff' * width for _ in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')


class PackageFixture:
    def __init__(self, root):
        self.root = root
        self.id = '450ec755-e088-4082-a459-0c49e9c932e7'
        self.path = root / 'native_manifest.json'
        image = png()
        digest = hashlib.sha256(image).hexdigest()
        for name in native.ASSET_NAMES:
            (root / name).mkdir()
        (root / 'textures/texture.png').write_bytes(image)
        texture = dict(file='texture.png', width=2, height=2, sha256=digest)
        face = dict(texture='minecraft:block/stone', tint=False)
        self.write_json(root / 'textures/manifest.json', dict(format='uebridge-block-textures', version=2,
            textures={'minecraft:block/stone': texture}, blocks={'minecraft:stone': dict(top=face, side=face, bottom=face,
            defaultState='', states={'': dict(collision=[[0,0,0,1,1,1]], outline=[[0,0,0,1,1,1]])})},
            models={'minecraft:block/stone': dict(elements=[dict(**{'from': [0,0,0], 'to': [16,16,16]}, faces={'up': dict(texture='minecraft:block/stone')})])},
            blockstates={'minecraft:stone': {'variants': {'': {'model': 'minecraft:block/stone'}}}}))
        (root / 'items/textures').mkdir()
        (root / ('items/textures/' + digest + '.png')).write_bytes(image)
        model_key = 'minecraft:stone@' + digest
        quad = dict(texture=digest, color=0xffffff, vertices=[[0,0,0],[1,0,0],[1,1,0],[0,1,0]], uv=[[0,0],[1,0],[1,1],[0,1]])
        self.write_json(root / 'items/manifest.json', dict(kind='items', version=1, textures={digest: dict(texture, file='textures/' + digest + '.png')},
            items={model_key: {context: [quad] for context in ('firstperson_righthand', 'firstperson_lefthand', 'thirdperson_righthand', 'thirdperson_lefthand', 'ground')}}))
        skin = png(64,64)
        (root / 'player/skin.png').write_bytes(skin)
        self.write_json(root / 'player/manifest.json', dict(kind='player', version=1, player=dict(name='Test', uuid=self.id),
            skin=dict(file='skin.png', sha256=hashlib.sha256(skin).hexdigest(), width=64, height=64, model='classic')))
        (root / 'mobs/texture.png').write_bytes(image)
        appearances, templates = {}, {}
        transform = [0,0,0,0,0,0,1,1,1]
        for index, species in enumerate(('minecraft:zombie', 'minecraft:villager')):
            key = str(index + 1) * 64
            templates[species] = key
            appearances[key] = dict(type=species, rendererScale=[1,1,1], rendererOffset=[0,0,0], texture='texture.png',
                textureHash=digest, textureWidth=2, textureHeight=2, parts=[dict(name='body', parent=-1, transform=transform,
                quads=[[[0,0,0,0,0],[1,0,0,1,0],[1,1,0,1,1],[0,1,0,0,1]]])], walkFrames=[[transform] for _ in range(16)],
                stats=dict(width=.6, height=1.8, maxHealth=20, speed=.2, damage=3, hostile=index==0, baby=False))
        self.write_json(root / 'mobs/manifest.json', dict(kind='mobs', version=1, appearances=appearances, templates=templates, entities={}))
        (root / 'ui/icon.png').write_bytes(image)
        self.write_json(root / 'ui/manifest.json', dict(kind='native-ui', version=1,
            sprites={key: dict(texture, file='icon.png') for key in ('hud/hotbar', 'hud/hotbar_selection', 'hud/crosshair')},
            items=[dict(id='minecraft:stone', name='石', icon='icon.png', sha256=digest, width=2, height=2, maxCount=64, block='minecraft:stone', modelKey=model_key)]))
        with wave.open(str(root / 'sounds/step.wav'), 'wb') as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(8000)
            wav.writeframes(b'\0\0' * 80)
        self.write_json(root / 'sounds/manifest.json', dict(kind='sounds', version=1,
            sounds={'minecraft:block.stone.step': [dict(file='step.wav', sha256=hashlib.sha256((root / 'sounds/step.wav').read_bytes()).hexdigest(), volume=1, pitch=1, weight=1)]}))
        self.header = dict(type='native_world', schema=1, id=self.id, dimension='minecraft:overworld', origin=[0,64,0], spawn=[0,64,0],
                           center=[0,8,0], radius=8, halfHeight=1, cells=867, yaw=0, pitch=0)
        self.cells = [dict(type='cell', cell=[x,y,z], palette=[], blocks=[], skyTop=[15]*64)
                      for y in range(7,10) for z in range(-8,9) for x in range(-8,9)]
        self.cells[0]['palette'] = [['minecraft:stone','',0xffffff,15,0]]
        self.cells[0]['blocks'] = [[0,0,15,0]]
        write_world_file(root / 'world.ndjson', self.header, self.cells)
        self.manifest = dict(kind='native-play', schema='uebridge.native.v1', version=1, id=self.id, radiusChunks=4,
            settings=dict(keyBindings={'key.forward': 'key.keyboard.w', 'key.togglePerspective': 'key.mouse.4'},
                          mouseSensitivity=.5, fov=70, invertYMouse=False, slimArms=False, mainHand='right', gameMode='creative', skinLayers=127),
            blockSounds={}, assets={name: self.ref(root / name / 'manifest.json') for name in native.ASSET_NAMES},
            world=dict(self.ref(root / 'world.ndjson'), cells=867, blocks=1, complete=True))
        self.finish()

    @staticmethod
    def write_json(path, data):
        path.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')

    def ref(self, path):
        return dict(file=path.relative_to(self.root).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), bytes=path.stat().st_size)

    def refresh(self, name):
        self.manifest['assets'][name] = self.ref(self.root / name / 'manifest.json')
        self.finish()

    def finish(self):
        self.write_json(self.path, self.manifest)


class NativePackageValidation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.fixture = PackageFixture(pathlib.Path(self.temp.name))

    def test_valid_package_uses_exact_paths_and_real_nested_validators(self):
        result = native.load_native_manifest(self.fixture.path)
        self.assertEqual(result['world']['rows'], 1)
        self.assertEqual(result['paths']['ui'], self.fixture.root / 'ui/manifest.json')
        self.assertEqual(result['parsed']['ui']['items'][0]['name'], '石')
    def test_look_indicator_and_equipment_settings_are_preserved_and_validated(self):
        settings = self.fixture.manifest['settings']
        settings.update(smoothCamera=True, attackIndicator='hotbar', equipment=[{'id': 'minecraft:diamond_helmet', 'count': 1}] + [{'id': '', 'count': 0}] * 3)
        self.fixture.finish()
        result = native.load_native_manifest(self.fixture.path)
        self.assertEqual(settings, result['manifest']['settings'])
        for field, value in (('smoothCamera', 1), ('attackIndicator', 'bad'), ('equipment', []), ('equipment', [{'id': '../helmet', 'count': 1}] * 4), ('equipment', [{'id': '', 'count': True}] * 4)):
            bad = copy.deepcopy(settings); bad[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError): native._settings(bad)

    def test_submanifest_hash_modified_even_if_still_valid_json(self):
        path = self.fixture.root / 'items/manifest.json'
        path.write_bytes(path.read_bytes() + b' ')
        with self.assertRaisesRegex(ValueError, 'items byte count/checksum'):
            native.load_native_manifest(self.fixture.path)

    def test_nested_texture_corruption_rejected_before_unreal_is_loaded(self):
        (self.fixture.root / 'ui/icon.png').write_bytes(b'broken')
        with patch.dict(sys.modules, {'unreal': None}):
            with self.assertRaisesRegex(ValueError, 'PNG checksum'):
                native.import_native_play(self.fixture.path)

    def test_world_id_mismatch_rejected(self):
        self.fixture.header['id'] = str(uuid.uuid4())
        write_world_file(self.fixture.root / 'world.ndjson', self.fixture.header, self.fixture.cells)
        self.fixture.manifest['world'].update(self.fixture.ref(self.fixture.root / 'world.ndjson'))
        self.fixture.finish()
        with self.assertRaisesRegex(ValueError, 'ID/radius'):
            native.load_native_manifest(self.fixture.path)

    def test_absent_world_model_rejected(self):
        self.fixture.cells[0]['palette'][0][0] = 'minecraft:missing'
        write_world_file(self.fixture.root / 'world.ndjson', self.fixture.header, self.fixture.cells)
        self.fixture.manifest['world'].update(self.fixture.ref(self.fixture.root / 'world.ndjson'))
        self.fixture.finish()
        with self.assertRaisesRegex(ValueError, 'Missing offline block/state'):
            native.load_native_manifest(self.fixture.path)

    def test_absent_ui_model_rejected(self):
        path = self.fixture.root / 'ui/manifest.json'
        ui = json.loads(path.read_bytes())
        ui['items'][0]['modelKey'] = 'minecraft:stone@' + 'f'*64
        self.fixture.write_json(path, ui)
        self.fixture.refresh('ui')
        with self.assertRaisesRegex(ValueError, 'absent item model'):
            native.load_native_manifest(self.fixture.path)

    def test_missing_baseline_mob_rejected(self):
        path = self.fixture.root / 'mobs/manifest.json'
        mobs = json.loads(path.read_bytes())
        del mobs['templates']['minecraft:zombie']
        self.fixture.write_json(path, mobs)
        self.fixture.refresh('mobs')
        with self.assertRaisesRegex(RuntimeError, 'missing usable adult templates'):
            native.load_native_manifest(self.fixture.path)

    def test_escaping_asset_and_unknown_schema_rejected(self):
        for value in ('../outside.json', 'C:/outside.json', '/outside.json', '..\\outside.json'):
            self.fixture.manifest['assets']['items']['file'] = value
            self.fixture.finish()
            with self.assertRaisesRegex(ValueError, 'escapes'):
                native.load_native_manifest(self.fixture.path)
        self.fixture.manifest['schema'] = 'uebridge.native.v2'
        self.fixture.finish()
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            native.load_native_manifest(self.fixture.path)


class Properties:
    def __init__(self):
        self.properties = {}
    def set_editor_property(self, key, value):
        self.properties[key] = value
    def get_editor_property(self, key):
        return self.properties.get(key)
    def set_mobility(self, value):
        self.properties['mobility'] = value


class Actor(Properties):
    def __init__(self):
        super().__init__()
        self.light_component = Properties()
    def set_actor_label(self, label):
        self.properties['label'] = label


class Receiver(Actor):
    pass


class World:
    def __init__(self):
        self.actors, self.settings = [], Properties()
        self.path = '/Game/UE'
    def get_world_settings(self):
        return self.settings
    def get_path_name(self):
        return self.path + '.' + self.path.rsplit('/', 1)[-1]


class FakeEditor:
    def __init__(self, project):
        self.project = project
        (project / 'UEBridge.uproject').write_text('{}')
        self.current = World()
        self.path = '/Game/UE'
        self.maps = {self.path: copy.deepcopy(self.current)}
        self.published = []
        self.dirty_packages = []
        self.logs = []
        self.quits = 0
        self.module = types.SimpleNamespace()
        m = self.module
        for name in ('UnrealEditorSubsystem', 'LevelEditorSubsystem', 'EditorActorSubsystem'):
            setattr(m, name, type(name, (), {}))
        m.BridgeReceiver, m.BridgeGameMode = Receiver, type('Mode', (), {})
        for name in ('BridgeNativeUiPalette', 'BridgeNativeSoundPalette', 'BridgeMobPalette'):
            setattr(m, name, type(name, (), {}))
        m.PlayerStart = m.DirectionalLight = m.SkyLight = m.SkyAtmosphere = Actor
        m.ComponentMobility = types.SimpleNamespace(MOVABLE='movable')
        m.Vector = m.Rotator = lambda *args: args
        m.Name = str
        m.Paths = types.SimpleNamespace(project_dir=lambda: str(project), convert_relative_path_to_full=lambda p: p)
        # GameplayStatics has no get_world_settings in the UE Python API.
        m.GameplayStatics = types.SimpleNamespace()
        m.log = m.log_error = self.logs.append
        m.SystemLibrary = types.SimpleNamespace(quit_editor=self.quit, get_command_line=lambda: '')
        m.get_editor_subsystem = lambda cls: self
        m.EditorAssetLibrary = types.SimpleNamespace(does_asset_exist=lambda path: path in self.maps)
        m.EditorLoadingAndSavingUtils = types.SimpleNamespace(get_dirty_map_packages=lambda: self.dirty_packages, save_map=self.save_map)

    def quit(self):
        self.quits += 1
    def get_game_world(self):
        return None
    def get_editor_world(self):
        return self.current
    def get_all_level_actors(self):
        return self.current.actors
    def load_level(self, path):
        self.path, self.current = path, copy.deepcopy(self.maps[path])
        return True
    def new_level(self, path):
        self.path, self.current = path, World()
        self.current.path = path
        return True
    def save_current_level(self):
        self.maps[self.path] = copy.deepcopy(self.current)
        return True
    def save_map(self, world, path):
        self.maps[path] = copy.deepcopy(world)
        self.published.append(path)
        return True
    def spawn_actor_from_class(self, cls, *args):
        actor = cls()
        self.current.actors.append(actor)
        return actor

    def receiver(self):
        return next(actor for actor in self.current.actors if isinstance(actor, Receiver))


class NativeSetupPublication(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = pathlib.Path(self.temp.name)
        export = root / 'export'
        export.mkdir()
        self.fixture = PackageFixture(export)
        self.package = native.load_native_manifest(self.fixture.path)
        project = root / 'project'
        project.mkdir()
        self.editor = FakeEditor(project)
        self.roots = []
        mapping = dict(textures='texture_palette', mobs='mob_palette', player='player_appearance', ui='native_ui_palette', sounds='native_sound_palette')
        for name in native.ASSET_NAMES:
            def imported(filename, asset_root=None, name=name):
                self.roots.append((name, asset_root))
                if name in mapping:
                    self.editor.receiver().set_editor_property(mapping[name], filename)
            self.package['helpers'][name]['import_minecraft_' + name] = imported
        self.original_runpy = native.runpy.run_path

    def run_helper(self, filename, *args, **kwargs):
        name = pathlib.Path(filename).name
        if name == 'setup_world_bridge.py':
            def setup():
                receiver = self.editor.spawn_actor_from_class(Receiver)
                receiver.set_editor_property('preview_material', 'Preview')
                return receiver
            return {'setup_world_bridge': setup}
        if name == 'setup_vanilla_effects.py':
            return {'setup_vanilla_effects': lambda: self.editor.receiver().set_editor_property('vanilla_particle_material', 'Dust')}
        if name == 'setup_bridge_rendering.py':
            return {'setup_bridge_rendering': lambda asset_root: self.editor.receiver().set_editor_property('outline_material', asset_root + '/Outline')}
        if name == 'setup_native_explosion.py':
            return {'setup_native_explosion': lambda: None}
        return self.original_runpy(filename, *args, **kwargs)

    def context(self):
        return patch.dict(sys.modules, {'unreal': self.editor.module})

    def test_success_publishes_only_managed_map_and_exact_launcher_marker(self):
        with self.context(), patch.object(native, 'load_native_manifest', return_value=self.package), patch.object(native.runpy, 'run_path', side_effect=self.run_helper):
            receiver = native.import_native_play(self.fixture.path)
        marker = json.loads((self.editor.project / 'Saved/NativeLauncher.json').read_text())
        self.assertTrue(marker['completed'])
        self.assertEqual(marker['manifestSha256'], hashlib.sha256(self.fixture.path.read_bytes()).hexdigest())
        self.assertEqual(marker['map'], native.NATIVE_MAP)
        self.assertEqual(self.editor.published, [native.NATIVE_MAP])
        self.assertIs(self.editor.maps[native.NATIVE_MAP].settings.get_editor_property('default_game_mode'), self.editor.module.BridgeGameMode)
        self.assertEqual(self.editor.maps['/Game/UE'].settings.properties, {})
        self.assertEqual(len(self.editor.maps['/Game/UE'].actors), 0)
        self.assertEqual(receiver.get_editor_property('native_world_file'), str(self.fixture.path))
        roots = dict(self.roots)
        self.assertIn('/Native/Packages/P_', roots['textures'])
        self.assertEqual(roots['items'], roots['textures'] + '/Items')

    def test_import_failure_retains_previous_native_map_and_no_success_marker(self):
        old_world = World()
        old = Receiver()
        old.set_editor_property('tags', [native.MANAGED_TAG])
        old.set_editor_property('native_world_file', 'previous-valid-export')
        old_world.actors.append(old)
        self.editor.maps[native.NATIVE_MAP] = old_world
        self.package['helpers']['ui']['import_minecraft_ui'] = lambda filename: (_ for _ in ()).throw(RuntimeError('UI import failed'))
        with self.context(), patch.object(native, 'load_native_manifest', return_value=self.package), patch.object(native.runpy, 'run_path', side_effect=self.run_helper):
            with self.assertRaisesRegex(RuntimeError, 'UI import failed'):
                native.import_native_play(self.fixture.path)
        self.assertEqual(self.editor.maps[native.NATIVE_MAP].actors[0].get_editor_property('native_world_file'), 'previous-valid-export')
        self.assertEqual(self.editor.published, [])
        self.assertFalse((self.editor.project / 'Saved/NativeLauncher.json').exists())

    def test_unmanaged_target_preserved(self):
        self.editor.maps[native.NATIVE_MAP] = World()
        with self.context(), patch.object(native, 'load_native_manifest', return_value=self.package):
            with self.assertRaisesRegex(RuntimeError, 'unmanaged map'):
                native.import_native_play(self.fixture.path)
        self.assertEqual(self.editor.published, [])

    def test_automated_error_still_quits_editor(self):
        with self.context(), patch.dict(native.os.environ, {'UEBRIDGE_NATIVE_MANIFEST': str(self.fixture.path), 'UEBRIDGE_NATIVE_AUTOMATION': '1'}), patch.object(native, 'import_native_play', side_effect=ValueError('bad checksum')):
            with self.assertRaisesRegex(ValueError, 'bad checksum'):
                native._automation_entry()
        self.assertEqual(self.editor.quits, 1)
        self.assertTrue(any('Traceback (most recent call last)' in entry and 'ValueError: bad checksum' in entry for entry in self.editor.logs))
        self.assertEqual(self.editor.published, [])

    def test_completion_marker_identifies_current_import_attempt(self):
        with patch.dict(native.os.environ, {'UEBRIDGE_NATIVE_ATTEMPT': 'current-attempt'}):
            native._save_marker(self.editor.project, self.package)
        marker = json.loads((self.editor.project / 'Saved/NativeLauncher.json').read_text())
        self.assertEqual(marker['importAttemptId'], 'current-attempt')

    def test_automated_fresh_dirty_template_is_replaced(self):
        self.editor.current.path = '/Temp/Untitled_1'
        self.editor.dirty_packages = [types.SimpleNamespace(get_path_name=lambda: '/Temp/Untitled_1')]
        with self.context(), patch.dict(native.os.environ, {'UEBRIDGE_NATIVE_AUTOMATION': '1'}), patch.object(native, 'load_native_manifest', return_value=self.package), patch.object(native.runpy, 'run_path', side_effect=self.run_helper):
            native.import_native_play(self.fixture.path)
        self.assertEqual(self.editor.published, [native.NATIVE_MAP])
        self.assertTrue(any('fresh temporary startup template' in entry for entry in self.editor.logs))

    def test_automated_dirty_user_map_is_preserved(self):
        self.editor.current.path = '/Game/UE'
        self.editor.dirty_packages = [types.SimpleNamespace(get_path_name=lambda: '/Game/UE')]
        with self.context(), patch.dict(native.os.environ, {'UEBRIDGE_NATIVE_AUTOMATION': '1'}), patch.object(native, 'load_native_manifest', return_value=self.package):
            with self.assertRaisesRegex(RuntimeError, 'dirty user maps are preserved'):
                native.import_native_play(self.fixture.path)
        self.assertEqual(self.editor.published, [])
        self.assertEqual(self.editor.path, '/Game/UE')

    def test_interactive_dirty_temporary_map_is_preserved(self):
        self.editor.current.path = '/Temp/Untitled_1'
        self.editor.dirty_packages = [types.SimpleNamespace(get_path_name=lambda: '/Temp/Untitled_1')]
        with self.context(), patch.dict(native.os.environ, {'UEBRIDGE_NATIVE_AUTOMATION': ''}), patch.object(native, 'load_native_manifest', return_value=self.package):
            with self.assertRaisesRegex(RuntimeError, 'dirty user maps are preserved'):
                native.import_native_play(self.fixture.path)
        self.assertEqual(self.editor.published, [])

    def test_dirty_saved_map_hidden_behind_template_is_preserved(self):
        self.editor.current.path = '/Temp/Untitled_1'
        self.editor.dirty_packages = [types.SimpleNamespace(get_path_name=lambda: '/Temp/Untitled_1'), types.SimpleNamespace(get_path_name=lambda: '/Game/Custom')]
        with self.context(), patch.dict(native.os.environ, {'UEBRIDGE_NATIVE_AUTOMATION': '1'}), patch.object(native, 'load_native_manifest', return_value=self.package):
            with self.assertRaisesRegex(RuntimeError, 'dirty user maps are preserved'):
                native.import_native_play(self.fixture.path)
        self.assertEqual(self.editor.published, [])


class NativeBuildPluginContract(unittest.TestCase):
    def test_required_build_plugins_match_current_project_descriptor(self):
        project = pathlib.Path(__file__).resolve().parents[1] / 'unreal/UEBridge'
        script = (project / 'Build-UEBridge.ps1').read_text()
        declaration = re.search(r'\$requiredPlugins = @\(([^\n]+)\)', script).group(1)
        enabled = {entry['Name'] for entry in json.loads((project / 'UEBridge.uproject').read_bytes())['Plugins'] if entry['Enabled']}
        self.assertEqual(set(re.findall(r"'([^']+)'", declaration)), enabled)
        # Require a whole-file replacement with backup, never replacement from a
        # shipped descriptor that could drop the user's unrelated settings.
        self.assertIn('[IO.File]::Replace($temporary, $project, $backup)', script)
        self.assertIn('Enable-UEBridgeRequiredPlugins -Descriptor $descriptor', script)
        self.assertNotIn('$descriptor.EngineAssociation =', script)
        self.assertNotIn('$descriptor.Modules =', script)

    @unittest.skipUnless(shutil.which('pwsh') or shutil.which('powershell'), 'PowerShell fixture test requires pwsh/Windows PowerShell')
    def test_real_powershell_merge_fixture(self):
        shell = shutil.which('pwsh') or shutil.which('powershell')
        subprocess.run([shell, '-NoProfile', '-File', str(pathlib.Path(__file__).with_name('test_native_build_plugins.ps1'))], check=True)


if __name__ == '__main__':
    unittest.main()
