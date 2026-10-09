"""Reproduce Unreal runpy with no project helper directory on sys.path."""
import pathlib
import runpy
import subprocess
import sys
import tempfile
import unittest


class UiMaterialHelpers(unittest.TestCase):
    def test_isolated_unreal_search_path(self):
        script = pathlib.Path(__file__).with_name("import_minecraft_ui.py").resolve()
        code = """
import runpy, sys
from pathlib import Path
script = Path(sys.argv[1])
assert str(script.parent) not in sys.path
ui = runpy.run_path(str(script))
lighting, physics = ui['_ui_material_helpers'](None)
assert lighting.__name__ == 'ensure_native_icon_glint_material'
assert physics.__name__ == 'setup_realistic_materials'
assert 'import_minecraft_textures' not in sys.modules
"""
        result = subprocess.run([sys.executable, "-I", "-c", code, str(script)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_helper_is_actionable(self):
        ui = runpy.run_path(str(pathlib.Path(__file__).with_name('import_minecraft_ui.py')))
        fn = ui['_ui_material_helpers']
        with tempfile.TemporaryDirectory() as directory:
            fn.__globals__['__file__'] = str(pathlib.Path(directory) / 'import_minecraft_ui.py')
            with self.assertRaisesRegex(RuntimeError, 'Missing UI material helper: bridge_lighting_materials.py'):
                fn(None)


if __name__ == '__main__':
    unittest.main()
