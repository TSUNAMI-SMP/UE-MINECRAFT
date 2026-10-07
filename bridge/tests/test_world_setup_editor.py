"""One-call setup preserves saved levels and rolls back failed receiver creation."""
import pathlib
import runpy
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

from test_mob_import_editor import MobEditor
from test_vanilla_effects_setup import Receiver

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / 'tools/setup_world_bridge.py'


class WorldSetupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = pathlib.Path(self.temp.name)
        (self.project / 'UEBridge.uproject').touch()
        (self.project / 'Source/UEBridge').mkdir(parents=True)
        (self.project / 'Source/UEBridge/BridgeWorld.h').touch()
        self.editor = MobEditor(self.project)
        self.editor.receivers = []
        self.world_path = '/Game/Test.Test'
        api = self.editor.api
        before = api.get_editor_subsystem
        actor_subsystem = before(api.EditorActorSubsystem)
        actor_subsystem.spawn_actor_from_class = self.spawn
        actor_subsystem.destroy_actor = self.destroy
        editor_subsystem = before(api.UnrealEditorSubsystem)
        editor_subsystem.get_editor_world = lambda: types.SimpleNamespace(get_path_name=lambda: self.world_path)

    def spawn(self, cls, location):
        receiver = cls()
        receiver.set_actor_label = lambda label: setattr(receiver, 'label', label)
        self.editor.receivers.append(receiver)
        return receiver

    def destroy(self, receiver):
        self.editor.receivers.remove(receiver)

    def run_setup(self):
        with patch.dict(sys.modules, {'unreal': self.editor.api}):
            return runpy.run_path(str(SCRIPT))['setup_world_bridge']()

    def test_saved_level_adds_once_and_preserves_existing_material_vfx(self):
        receiver = self.run_setup()
        first_material = receiver.get_editor_property('preview_material')
        explosion = object()
        receiver.set_editor_property('explosion_system', explosion)
        self.assertIs(receiver, self.run_setup())
        self.assertEqual(1, len(self.editor.receivers))
        self.assertIs(first_material, receiver.get_editor_property('preview_material'))
        self.assertIs(explosion, receiver.get_editor_property('explosion_system'))
        self.assertEqual(2, self.editor.level_saves)

    def test_unsaved_level_and_multiple_receivers_block_asset_creation(self):
        self.world_path = '/Temp/Untitled_1.Untitled_1'
        with self.assertRaisesRegex(RuntimeError, 'Save this level'):
            self.run_setup()
        self.assertEqual({}, self.editor.assets)
        self.world_path = '/Game/Test.Test'
        self.editor.receivers = [Receiver(), Receiver()]
        with self.assertRaisesRegex(RuntimeError, 'exactly one'):
            self.run_setup()
        self.assertEqual({}, self.editor.assets)

    def test_failed_level_save_removes_only_new_receiver(self):
        self.editor.fail_level_saves = 1
        with self.assertRaisesRegex(RuntimeError, 'Cannot save'):
            self.run_setup()
        self.assertEqual([], self.editor.receivers)
        self.assertEqual(2, self.editor.level_saves)
        self.assertIsNotNone(self.run_setup())


if __name__ == '__main__':
    unittest.main()
