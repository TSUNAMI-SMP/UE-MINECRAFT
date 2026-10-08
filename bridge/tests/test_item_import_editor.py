"""Item importer editor contracts: usable overrides and recoverable assignments."""
import json
import pathlib
import runpy
import sys
import types
import unittest
from unittest.mock import patch

import test_item_manifest as manifest_fixture
from test_mob_import_editor import MobEditor, Palette


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_minecraft_items.py'


class Progress:
    def __init__(self, *args):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def make_dialog(self, *args):
        pass

    def should_cancel(self):
        return False

    def enter_progress_frame(self, *args):
        pass


class ItemPalette(Palette):
    def __init__(self):
        super().__init__()
        self.properties.update(item_models={'old': 'old geometry'}, item_materials={'old': object()},
                               default_item_models={'minecraft:old': 'old'},
                               materials={'stone': object()}, blockstate_definitions={})
        self.fail_field = None

    def set_editor_property(self, name, value):
        if self.fail_field == name:
            self.fail_field = None
            raise RuntimeError('item property write failed')
        super().set_editor_property(name, value)


class ItemEditor(MobEditor):
    def __init__(self, project):
        super().__init__(project)
        self.fail_palette_saves = 0
        self.bad_readback = False
        self.saved_palette_values = []
        api = self.api
        api.ScopedSlowTask = Progress
        api.TextureMipGenSettings = types.SimpleNamespace(TMGS_NO_MIPMAPS='no mips')
        api.ScalarParameterValue = lambda **kwargs: types.SimpleNamespace(**kwargs)
        api.log_warning = self.messages.append
        editing = api.MaterialEditingLibrary
        texture_readback = editing.get_material_instance_texture_parameter_value
        editing.get_material_instance_texture_parameter_value = lambda material, name: None if self.bad_readback else texture_readback(material, name)
        # A strict contract reproduces UE's setter lookup failure. Import must
        # supply explicit, verified overrides without depending on these APIs.
        editing.set_material_instance_texture_parameter_value = self.failed_setter
        editing.set_material_instance_scalar_parameter_value = self.failed_setter

    @staticmethod
    def failed_setter(*args):
        raise AssertionError('Unreliable parameter setter was used')

    def save_asset(self, asset, only_dirty):
        if isinstance(asset, ItemPalette):
            if self.fail_palette_saves:
                self.fail_palette_saves -= 1
                return False
            self.saved_palette_values.append((dict(asset.properties['item_models']), dict(asset.properties['default_item_models']), dict(asset.properties['item_materials'])))
        return super().save_asset(asset, only_dirty)


class ItemImportEditorTest(unittest.TestCase):
    def setUp(self):
        self.fixture = manifest_fixture.ItemManifestTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.tearDown)
        self.fixture.load()
        self.project = self.fixture.root / 'project'
        self.project.mkdir()
        (self.project / 'UEBridge.uproject').touch()
        for name in ('import_minecraft_textures.py', 'bridge_lighting_materials.py'):
            (self.project / name).write_bytes((SCRIPT.parent / name).read_bytes())
        self.editor = ItemEditor(self.project)
        self.palette = ItemPalette()
        self.previous_models = dict(self.palette.properties['item_models'])
        self.previous_defaults = dict(self.palette.properties['default_item_models'])
        self.previous_materials = dict(self.palette.properties['item_materials'])
        self.blocks = dict(self.palette.properties['materials'])
        self.editor.receivers[0].set_editor_property('texture_palette', self.palette)

    def run_import(self):
        with patch.dict(sys.modules, {'unreal': self.editor.api}):
            runpy.run_path(str(SCRIPT))['import_minecraft_items'](self.fixture.root / 'manifest.json')

    def assert_previous_palette(self):
        self.assertEqual(self.previous_models, self.palette.properties['item_models'])
        self.assertEqual(self.previous_defaults, self.palette.properties['default_item_models'])
        self.assertEqual(self.previous_materials, self.palette.properties['item_materials'])
        self.assertEqual(self.blocks, self.palette.properties['materials'])
        self.assertIs(self.palette, self.editor.receivers[0].get_editor_property('texture_palette'))

    def test_dedicated_world_model_uses_default_key_and_vertex_light_material(self):
        self.fixture.manifest['defaultModels']={'minecraft:diamond_sword':self.fixture.key}
        self.fixture.load()
        self.palette.properties['blockstate_definitions']={'minecraft:diamond_sword':'{"variants":{"":{"model":"minecraft:block/uebridge_fallback_test"}}}'}
        self.run_import()
        self.assertEqual(self.fixture.key,self.palette.properties['default_item_models']['minecraft:diamond_sword'])
        self.assertNotIn('minecraft:diamond_sword',self.palette.properties['item_models'])
        material=self.palette.properties['item_materials'][self.fixture.hash+'#world']
        parent=material.properties['parent']
        self.assertIn('BridgeUseVertexLight',self.editor.api.MaterialEditingLibrary.get_scalar_parameter_names(parent))
        self.assertEqual('FaceTexture',material.properties['texture_parameter_values'][0].parameter_info.name)

    def test_real_item_master_and_explicit_overrides_import_tinted_ground_model(self):
        self.run_import()
        model = json.loads(self.palette.properties['item_models'][self.fixture.key])
        self.assertIn('ground', model)
        self.assertEqual(0x345678, model['ground'][0]['color'])
        material = self.palette.properties['item_materials'][self.fixture.hash]
        texture = material.properties['texture_parameter_values'][0]
        self.assertEqual('FaceTexture', texture.parameter_info.name)
        self.assertIs(self.editor.assets['/Game/Bridge/Minecraft/Items/T_Item_' + self.fixture.hash[:24]], texture.parameter_value)
        scalar = material.properties['scalar_parameter_values'][0]
        self.assertEqual(('FaceTint', 1.0), (scalar.parameter_info.name, scalar.parameter_value))
        self.assertEqual(self.blocks, self.palette.properties['materials'])
        self.assertEqual(1, len(self.editor.saved_palette_values))

    def test_failed_palette_save_restores_memory_disk_snapshot_then_retry_works(self):
        self.editor.fail_palette_saves = 1
        with self.assertRaisesRegex(RuntimeError, 'Cannot save native item palette'):
            self.run_import()
        self.assert_previous_palette()
        self.assertEqual([(self.previous_models, self.previous_defaults, self.previous_materials)], self.editor.saved_palette_values)
        self.run_import()
        self.assertIn(self.fixture.key, self.palette.properties['item_models'])

    def test_second_property_failure_restores_first_map(self):
        self.palette.fail_field = 'item_materials'
        with self.assertRaisesRegex(RuntimeError, 'item property write failed'):
            self.run_import()
        self.assert_previous_palette()
    def test_default_reference_assignment_failure_restores_all_maps(self):
        self.palette.fail_field = 'default_item_models'
        with self.assertRaisesRegex(RuntimeError, 'item property write failed'):
            self.run_import()
        self.assert_previous_palette()

    def test_material_readback_failure_never_reassigns_palette(self):
        self.editor.bad_readback = True
        with self.assertRaisesRegex(RuntimeError, 'Cannot save/read back item material'):
            self.run_import()
        self.assert_previous_palette()
        self.assertEqual([], self.editor.saved_palette_values)

    def test_unsaved_world_missing_helper_play_or_dirty_map_changes_no_assets(self):
        subsystem = self.editor.api.get_editor_subsystem(self.editor.api.UnrealEditorSubsystem)
        original_world = subsystem.get_editor_world
        for mode in ('temporary', 'missing helper', 'playing', 'dirty'):
            with self.subTest(mode=mode):
                self.editor.playing = mode == 'playing'
                self.editor.dirty = mode == 'dirty'
                subsystem.get_editor_world = (lambda: types.SimpleNamespace(get_path_name=lambda: '/Temp/Untitled')) if mode == 'temporary' else original_world
                helper = self.project / 'import_minecraft_textures.py'
                if mode == 'missing helper':
                    helper.unlink()
                else:
                    helper.write_bytes((SCRIPT.parent / helper.name).read_bytes())
                with self.assertRaises(RuntimeError):
                    self.run_import()
                self.assert_previous_palette()
                self.assertEqual({}, self.editor.assets)


if __name__ == '__main__':
    unittest.main()
