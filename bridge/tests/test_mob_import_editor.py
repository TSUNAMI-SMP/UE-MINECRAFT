"""Editor contract regressions: separate runpy scope, staging and save failure."""
import contextlib
import copy
import json
import pathlib
import runpy
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

from test_mob_manifest import MobManifestTest, MOBS, MODULE
from test_vanilla_effects_setup import Editor, Material, PropertyObject


class Texture(PropertyObject):
    pass


class Instance(PropertyObject):
    pass


class Palette(PropertyObject):
    pass


class Rotator:
    def __init__(self, quaternion):
        self.source_quaternion = quaternion


class Quat:
    def __init__(self, *values):
        self.values = values

    def rotator(self):
        return Rotator(self.values)


def make_transform(**kwargs):
    # UE's Python Transform constructor calls MakeTransform: its Rotation is a
    # Rotator even though FTransform stores a quaternion internally.
    if not isinstance(kwargs.get('rotation'), Rotator):
        raise TypeError("Transform: Cannot nativize 'Quat' as 'Rotator'")
    return kwargs


class MobEditor(Editor):
    def __init__(self, project):
        super().__init__(project)
        self.fail_level_saves = 0
        self.messages = []
        api = self.api
        api.BridgeMobPalette, api.BridgeMobPart, api.BridgeMobAppearance = Palette, PropertyObject, PropertyObject
        api.Texture2D, api.MaterialInstanceConstant = Texture, Instance
        api.MaterialInstanceConstantFactoryNew = object
        api.AssetImportTask = api.DataAssetFactory = PropertyObject
        api.TextureFilter = types.SimpleNamespace(TF_NEAREST='nearest')
        api.TextureMipGenSettings = types.SimpleNamespace(TMGS_NO_MIPMAPS='none')
        api.Vector = api.Vector2D = lambda *args: args
        api.Quat, api.Rotator = Quat, Rotator
        api.Transform = make_transform
        api.TextureParameterValue = api.MaterialParameterInfo = lambda **kwargs: types.SimpleNamespace(**kwargs)
        api.log = self.messages.append
        api.log_error = self.messages.append
        tools = types.SimpleNamespace(create_asset=self.create_asset, import_asset_tasks=self.import_tasks)
        api.AssetToolsHelpers = types.SimpleNamespace(get_asset_tools=lambda: tools)
        api.MaterialEditingLibrary.set_material_instance_parent = lambda material, parent: material.set_editor_property('parent', parent)
        api.MaterialEditingLibrary.update_material_instance = lambda material: None
        api.MaterialEditingLibrary.get_material_instance_texture_parameter_value = lambda material, name: next((value.parameter_value for value in material.properties.get('texture_parameter_values', []) if value.parameter_info.name == name), None)

    def import_tasks(self, tasks):
        for task in tasks:
            self.assets[task.properties['destination_path'] + '/' + task.properties['destination_name']] = Texture()

    def save_level(self):
        self.level_saves += 1
        if self.fail_level_saves:
            self.fail_level_saves -= 1
            return False
        return True


class MobImportEditorTest(unittest.TestCase):
    def setUp(self):
        self.fixture = MobManifestTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.project = self.fixture.root / 'project'
        self.project.mkdir()
        (self.project / 'UEBridge.uproject').touch()
        (self.project / 'bridge_lighting_materials.py').write_bytes((MODULE.parent / 'bridge_lighting_materials.py').read_bytes())
        data = copy.deepcopy(self.fixture.manifest)
        stats = dict(width=.6, height=1.8, maxHealth=20, speed=.25, damage=4, hostile=True, baby=False)
        zombie = data['appearances'][self.fixture.key]
        zombie['stats'] = stats
        villager = copy.deepcopy(zombie)
        villager['type'] = 'minecraft:villager'
        villager['stats']['hostile'] = False
        self.villager_key = 'b' * 64
        data['appearances'][self.villager_key] = villager
        data['templates'] = {'minecraft:zombie': self.fixture.key, 'minecraft:villager': self.villager_key}
        self.fixture.load(data)
        self.path = self.fixture.root / 'manifest.json'
        self.editor = MobEditor(self.project)

    def run_import(self):
        # Mirrors native_setup: runpy receives no parent's local project binding.
        with patch.dict(sys.modules, {'unreal': self.editor.api}):
            return runpy.run_path(str(MODULE))['import_minecraft_mobs'](str(self.path))

    def test_fresh_runpy_scope_imports_baseline_geometry_and_saves_palette(self):
        palette = self.run_import()
        self.assertIs(palette, self.editor.receivers[0].get_editor_property('mob_palette'))
        self.assertEqual(2, len(palette.get_editor_property('appearances')))
        self.assertEqual({'minecraft:zombie', 'minecraft:villager'}, set(palette.get_editor_property('templates')))
        parts = palette.get_editor_property('appearances')[0].get_editor_property('parts')
        self.assertEqual((0, -6.25, -6.25), parts[0].get_editor_property('vertices')[2])
        self.assertEqual(16, len(parts[0].get_editor_property('walk_frames')))
        self.assertEqual(1, self.editor.level_saves)
        textures = [value for value in self.editor.assets.values() if isinstance(value, Texture)]
        self.assertTrue(textures)
        self.assertTrue(all(value.get_editor_property('filter') == 'nearest' and value.get_editor_property('mip_gen_settings') == 'none' for value in textures))
        self.assertTrue(any('Minecraft mob body models ready:' in message for message in self.editor.messages))

    def test_missing_baseline_or_helper_preflight_changes_no_assets(self):
        (self.project / 'bridge_lighting_materials.py').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Copy bridge_lighting_materials'):
            self.run_import()
        self.assertEqual({}, self.editor.assets)
        self.fixture.load()
        with self.assertRaisesRegex(RuntimeError, 'missing usable adult templates'):
            self.run_import()
        self.assertEqual({}, self.editor.assets)

    def test_rest_and_walk_poses_use_rotator_constructor_with_mapped_transforms(self):
        data = json.loads(self.path.read_text())
        for source in data['appearances'].values():
            for part in source['parts']:
                part['transform'] = [16, 24, -8, .3, -.7, 1.1, 2, 3, 4]
            for index, frame in enumerate(source['walkFrames']):
                for part_index in range(len(frame)):
                    frame[part_index] = [index, 24 - index, -8, .1 * index, -.2, .4, 2, 3, 4]
        self.fixture.load(data)
        palette = self.run_import()
        for appearance in palette.get_editor_property('appearances'):
            source = data['appearances'][appearance.get_editor_property('key')]
            for part_index, (part, original) in enumerate(zip(appearance.get_editor_property('parts'), source['parts'])):
                poses = [part.get_editor_property('rest')] + part.get_editor_property('walk_frames')
                inputs = [original['transform']] + [frame[part_index] for frame in source['walkFrames']]
                self.assertEqual(17, len(poses))
                for pose, values in zip(poses, inputs):
                    position, quaternion, scale = MOBS.minecraft_part_transform(values)
                    self.assertEqual(position, pose['location'])
                    self.assertEqual(quaternion, pose['rotation'].source_quaternion)
                    self.assertEqual(scale, pose['scale'])

    def test_graph_failure_keeps_previous_palette_material_and_retry_finishes(self):
        old_palette, old_material = Palette(), Material()
        sentinel = object()
        old_material.nodes.append(sentinel)
        self.editor.assets['/Game/Bridge/Minecraft/M_MinecraftMob_v1'] = old_material
        self.editor.receivers[0].set_editor_property('mob_palette', old_palette)
        self.editor.fail_connections = True
        with self.assertRaises(RuntimeError):
            self.run_import()
        self.assertEqual([sentinel], old_material.nodes)
        self.assertIs(old_palette, self.editor.receivers[0].get_editor_property('mob_palette'))
        self.assertEqual(0, self.editor.level_saves)
        self.assertTrue(any('FAILED' in message for message in self.editor.messages))
        self.editor.fail_connections = False
        self.assertIsNot(old_palette, self.run_import())

    def test_failed_level_save_restores_previous_palette_before_retry(self):
        old_palette = Palette()
        self.editor.receivers[0].set_editor_property('mob_palette', old_palette)
        self.editor.fail_level_saves = 1
        with self.assertRaisesRegex(RuntimeError, 'Cannot save level'):
            self.run_import()
        self.assertIs(old_palette, self.editor.receivers[0].get_editor_property('mob_palette'))
        self.assertEqual(2, self.editor.level_saves)
        self.assertIsNot(old_palette, self.run_import())


if __name__ == '__main__':
    unittest.main()
