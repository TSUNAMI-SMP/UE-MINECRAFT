"""Local PCM import validation and event variant/assignment contract."""
import copy
import hashlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
import wave
from unittest.mock import patch

from test_mob_import_editor import MobEditor, Palette, PropertyObject

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / 'tools/import_minecraft_sounds.py'
SPEC = importlib.util.spec_from_file_location('native_sound_import', SCRIPT)
SOUNDS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SOUNDS)


def pcm(channels=1, width=2):
    buffer = io.BytesIO()
    with wave.open(buffer, 'wb') as writer:
        writer.setnchannels(channels)
        writer.setsampwidth(width)
        writer.setframerate(22050)
        writer.writeframes(b'\0' * 256 * channels * width)
    return buffer.getvalue()


class Wave(PropertyObject):
    pass


class SoundEditor(MobEditor):
    def __init__(self, project):
        super().__init__(project)
        self.api.SoundWave = Wave
        self.api.BridgeNativeSoundPalette = Palette
        self.api.BridgeNativeSoundEvent = self.api.BridgeNativeSoundVariant = PropertyObject

    def import_tasks(self, tasks):
        for task in tasks:
            self.assets[task.properties['destination_path'] + '/' + task.properties['destination_name']] = Wave()


class SoundManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.audio = self.root / 'stone.wav'
        self.audio.write_bytes(pcm())
        self.path = self.root / 'manifest.json'
        (self.root / 'UEBridge.uproject').touch()
        variant = dict(file='stone.wav', sha256=hashlib.sha256(self.audio.read_bytes()).hexdigest(), volume=.75, pitch=1.1, weight=3)
        self.manifest = dict(kind='sounds', version=1, sounds={'minecraft:block.stone.break': [variant], 'minecraft:block.stone.step': [dict(variant, volume=.15, pitch=1, weight=1)]})

    def load(self, data=None):
        self.path.write_text(json.dumps(self.manifest if data is None else data))
        return SOUNDS.load_sound_manifest(self.path)

    def test_valid_pcm_variants_remain_separate_and_share_checked_file(self):
        result = self.load()
        self.assertEqual(str(self.audio), result['sounds']['minecraft:block.stone.break'][0]['source'])
        self.assertEqual(.75, result['sounds']['minecraft:block.stone.break'][0]['volume'])
        self.assertEqual(.15, result['sounds']['minecraft:block.stone.step'][0]['volume'])

    def test_corrupt_truncated_unsupported_pcm_and_unsafe_paths_rejected(self):
        for payload in (b'not WAV', pcm()[:-3], pcm(width=1)):
            self.audio.write_bytes(payload)
            data = copy.deepcopy(self.manifest)
            for variants in data['sounds'].values():
                variants[0]['sha256'] = hashlib.sha256(payload).hexdigest()
            with self.assertRaises(ValueError):
                self.load(data)
        self.audio.write_bytes(pcm())
        for relative in ('../stone.wav', '/stone.wav', 'C:/stone.wav', 'assets\\stone.wav'):
            data = copy.deepcopy(self.manifest)
            data['sounds']['minecraft:block.stone.break'][0]['file'] = relative
            with self.assertRaises(ValueError):
                self.load(data)

    def test_bool_weight_nonfinite_pitch_and_changed_hash_rejected(self):
        for key, value in (('weight', True), ('pitch', float('nan')), ('volume', -1), ('sha256', '0' * 64)):
            data = copy.deepcopy(self.manifest)
            data['sounds']['minecraft:block.stone.break'][0][key] = value
            with self.assertRaises(ValueError):
                self.load(data)

    def test_editor_import_preserves_sound_rules_and_only_imports_shared_pcm_once(self):
        self.load()
        editor = SoundEditor(self.root)
        with patch.dict(sys.modules, {'unreal': editor.api}):
            palette = SOUNDS.import_minecraft_sounds(self.path)
        events = palette.get_editor_property('events')
        self.assertEqual(2, len(events))
        first, second = [event.get_editor_property('variants')[0] for event in events]
        self.assertIs(first.get_editor_property('wave'), second.get_editor_property('wave'))
        self.assertEqual(.75, first.get_editor_property('volume'))
        self.assertEqual(1.1, first.get_editor_property('pitch'))
        self.assertEqual(3, first.get_editor_property('weight'))
        self.assertEqual(1, sum(isinstance(asset, Wave) for asset in editor.assets.values()))
        self.assertIs(palette, editor.receivers[0].get_editor_property('native_sound_palette'))

    def test_failed_level_save_preserves_previous_sound_palette(self):
        self.load()
        editor = SoundEditor(self.root)
        old = Palette()
        editor.receivers[0].set_editor_property('native_sound_palette', old)
        editor.fail_level_saves = 1
        with patch.dict(sys.modules, {'unreal': editor.api}):
            with self.assertRaisesRegex(RuntimeError, 'Cannot save/verify'):
                SOUNDS.import_minecraft_sounds(self.path)
        self.assertIs(old, editor.receivers[0].get_editor_property('native_sound_palette'))
        self.assertEqual(2, editor.level_saves)


if __name__ == '__main__':
    unittest.main()
