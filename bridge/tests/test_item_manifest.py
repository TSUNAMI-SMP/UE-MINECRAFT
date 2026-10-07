import copy
import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
from test_mob_manifest import png

spec = importlib.util.spec_from_file_location('items', pathlib.Path(__file__).resolve().parents[2] / 'tools/import_minecraft_items.py')
items = importlib.util.module_from_spec(spec); spec.loader.exec_module(items)

class ItemManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = pathlib.Path(self.temp.name)
        texture = png(); self.hash = hashlib.sha256(texture).hexdigest()
        (self.root / 'textures').mkdir(); (self.root / 'textures' / (self.hash + '.png')).write_bytes(texture)
        face = dict(texture=self.hash, color=0x345678, vertices=[[0,0,0],[1,0,0],[1,1,0],[0,1,0]], uv=[[0,0],[1,0],[1,1],[0,1]])
        self.key = 'minecraft:diamond_sword@' + 'a' * 64
        self.manifest = dict(kind='items', version=1, textures={self.hash:dict(file=f'textures/{self.hash}.png',sha256=self.hash,width=1,height=1)}, items={self.key:{context:[face] for context in items.CONTEXTS}})
    def tearDown(self):
        self.temp.cleanup()
    def load(self, data=None):
        path = self.root / 'manifest.json'; path.write_text(json.dumps(self.manifest if data is None else data))
        return items.load_item_manifest(path)
    def test_native_quads_tints_and_four_display_contexts_round_trip(self):
        result = self.load()
        self.assertEqual(set(items.CONTEXTS), set(result['items'][self.key]))
        self.assertEqual(0x345678, result['items'][self.key][items.CONTEXTS[0]][0]['color'])
        self.assertEqual(str(self.root / 'textures' / (self.hash + '.png')), result['textures'][self.hash]['source'])
    def test_native_geometry_rejects_nan_booleans_and_incomplete_quads(self):
        for value in (float('nan'), float('inf'), True, '1'):
            data = copy.deepcopy(self.manifest); data['items'][self.key][items.CONTEXTS[0]][0]['vertices'][0][0] = value
            with self.assertRaises(ValueError): self.load(data)
        data = copy.deepcopy(self.manifest); data['items'][self.key][items.CONTEXTS[0]][0]['uv'].pop()
        with self.assertRaises(ValueError): self.load(data)
    def test_unresolved_textures_tints_contexts_and_model_keys_are_rejected(self):
        for field, value in (('texture', 'f'*64), ('texture', {}), ('color', True), ('color', 0x1000000)):
            data = copy.deepcopy(self.manifest); data['items'][self.key][items.CONTEXTS[0]][0][field] = value
            with self.assertRaises(ValueError): self.load(data)
        data = copy.deepcopy(self.manifest); data['items'][self.key].pop(items.CONTEXTS[1])
        with self.assertRaises(ValueError): self.load(data)
        data = copy.deepcopy(self.manifest); data['items']['../invalid@'+'a'*64] = data['items'].pop(self.key)
        with self.assertRaises(ValueError): self.load(data)
    def test_texture_hash_dimensions_and_path_are_checked_before_unreal(self):
        for field, value in (('file','../texture.png'), ('sha256','f'*64), ('width',2), ('width',True)):
            data = copy.deepcopy(self.manifest); data['textures'][self.hash][field] = value
            with self.assertRaises(ValueError): self.load(data)
    def test_symlink_cannot_import_a_texture_outside_the_export(self):
        with tempfile.TemporaryDirectory() as outside:
            source = pathlib.Path(outside) / 'outside.png'; source.write_bytes(png())
            target = self.root / 'textures' / (self.hash+'.png'); target.unlink();target.symlink_to(source)
            with self.assertRaisesRegex(ValueError, 'symlink'): self.load()
