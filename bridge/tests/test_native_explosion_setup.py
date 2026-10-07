"""The fallback sprite requires an actual alpha graph, not an unset Niagara slot."""
import pathlib
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch

from test_vanilla_effects_setup import Editor

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / 'tools/setup_native_explosion.py'


class NativeExplosionSetupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        project = pathlib.Path(self.temp.name)
        (project / 'UEBridge.uproject').touch()
        self.editor = Editor(project)
        self.editor.api.MaterialProperty.MP_OPACITY = 'opacity'
        self.editor.api.BlendMode.BLEND_TRANSLUCENT = 'translucent'

    def run_setup(self):
        with patch.dict(sys.modules, {'unreal': self.editor.api}):
            return runpy.run_path(str(SCRIPT))['setup_native_explosion']()

    def test_emissive_color_and_texture_alpha_times_tint_alpha(self):
        material = self.run_setup()
        alpha = material.outputs['opacity'][0]
        self.assertEqual('Multiply', type(alpha).__name__)
        self.assertEqual('A', alpha.inputs['A'][1])
        self.assertEqual('A', alpha.inputs['B'][1])
        self.assertEqual('ExplosionTexture', alpha.inputs['A'][0].properties['parameter_name'])
        self.assertEqual('ExplosionTint', alpha.inputs['B'][0].properties['parameter_name'])
        self.assertIn('emissive', material.outputs)
        self.assertEqual('unlit', material.properties['shading_model'])
        self.assertEqual('translucent', material.properties['blend_mode'])
        self.assertEqual({}, self.editor.receivers[0].properties)

    def test_complete_graph_is_not_rebuilt_and_play_blocks_changes(self):
        material = self.run_setup()
        original_nodes = list(material.nodes)
        self.assertIs(material, self.run_setup())
        self.assertEqual(original_nodes, material.nodes)
        self.editor.playing = True
        with self.assertRaisesRegex(RuntimeError, 'Stop Play'):
            self.run_setup()
        self.assertEqual(original_nodes, material.nodes)


if __name__ == '__main__':
    unittest.main()
