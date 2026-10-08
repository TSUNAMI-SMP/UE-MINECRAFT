"""Failure recovery for reimports of an already assigned sound palette."""
import sys
import unittest
from unittest.mock import patch

import test_sound_manifest as fixtures
from test_mob_import_editor import Palette


class SoundEditor(fixtures.SoundEditor):
    def __init__(self, project):
        super().__init__(project)
        self.fail_palette_saves = 0
        self.saved_events = []
        self.fail_rollback_level = False

    def save_asset(self, asset, only_dirty):
        if isinstance(asset, Palette):
            if self.fail_palette_saves:
                self.fail_palette_saves -= 1
                return False
            self.saved_events.append(list(asset.get_editor_property('events')))
        return super().save_asset(asset, only_dirty)

    def save_level(self):
        if self.fail_rollback_level and self.level_saves == 1:
            self.level_saves += 1
            raise RuntimeError('rollback level save failed')
        return super().save_level()


class SoundReimportEditorTest(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.SoundManifestTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        manifest = self.fixture.load()
        self.editor = SoundEditor(self.fixture.root)
        self.palette = Palette()
        self.old_events = [object()]
        self.palette.set_editor_property('events', list(self.old_events))
        target = '/Game/Bridge/Minecraft/Sounds/DA_MinecraftSounds_v1_' + manifest['manifestHash'][:24]
        self.editor.assets[target] = self.palette
        self.editor.receivers[0].set_editor_property('native_sound_palette', self.palette)

    def run_import(self):
        with patch.dict(sys.modules, {'unreal': self.editor.api}):
            return fixtures.SOUNDS.import_minecraft_sounds(self.fixture.path)

    def assert_previous_palette(self):
        self.assertIs(self.palette, self.editor.receivers[0].get_editor_property('native_sound_palette'))
        self.assertEqual(self.old_events, self.palette.get_editor_property('events'))
        self.assertEqual(self.old_events, self.editor.saved_events[-1])

    def test_failed_reimport_palette_save_restores_existing_events_then_retry_works(self):
        self.editor.fail_palette_saves = 1
        with self.assertRaisesRegex(RuntimeError, 'Cannot save native sound palette'):
            self.run_import()
        self.assert_previous_palette()
        self.assertEqual(0, self.editor.level_saves)
        self.assertIs(self.palette, self.run_import())
        self.assertEqual(2, len(self.palette.get_editor_property('events')))

    def test_failed_reimport_level_save_restores_events_and_receiver(self):
        self.editor.fail_level_saves = 1
        with self.assertRaisesRegex(RuntimeError, 'Cannot save/verify'):
            self.run_import()
        self.assert_previous_palette()
        self.assertEqual(2, self.editor.level_saves)

    def test_rollback_error_is_logged_without_hiding_original_save_failure(self):
        self.editor.fail_level_saves = 1
        self.editor.fail_rollback_level = True
        with self.assertRaisesRegex(RuntimeError, 'Cannot save/verify'):
            self.run_import()
        self.assert_previous_palette()
        self.assertTrue(any('rollback level save failed' in message for message in self.editor.messages))


if __name__ == '__main__':
    unittest.main()
