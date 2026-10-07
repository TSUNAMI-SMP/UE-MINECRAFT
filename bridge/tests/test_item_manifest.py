import copy
import hashlib
import gzip
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
    def test_native_quads_tints_and_ground_display_contexts_round_trip(self):
        result = self.load()
        self.assertEqual(set(items.CONTEXTS), set(result['items'][self.key]))
        self.assertEqual(0x345678, result['items'][self.key][items.CONTEXTS[0]][0]['color'])
        self.assertEqual(str(self.root / 'textures' / (self.hash + '.png')), result['textures'][self.hash]['source'])
    def test_legacy_hand_only_export_remains_valid_without_fabricated_ground(self):
        data = copy.deepcopy(self.manifest)
        data['items'][self.key].pop('ground')
        result = self.load(data)
        self.assertEqual(set(items.HAND_CONTEXTS), set(result['items'][self.key]))
        self.assertEqual(0, items.ground_model_count(result))
        self.assertNotIn('ground', result['items'][self.key])
    def test_new_ground_display_retains_native_scale_and_texture(self):
        data = copy.deepcopy(self.manifest)
        data['items'][self.key]['ground'][0]['vertices'] = [[-.25,.125,0],[.25,.125,0],[.25,.625,0],[-.25,.625,0]]
        result = self.load(self.compressed(data))
        self.assertEqual(1, items.ground_model_count(result))
        self.assertEqual([-.25,.125,0], result['items'][self.key]['ground'][0]['vertices'][0])
        self.assertEqual(self.hash, result['items'][self.key]['ground'][0]['texture'])
    def test_partial_or_unknown_ground_contexts_are_rejected(self):
        for extra in ('fixed', 'ground_typo'):
            data = copy.deepcopy(self.manifest); data['items'][self.key][extra] = data['items'][self.key]['ground']
            with self.assertRaises(ValueError): self.load(data)
        data = copy.deepcopy(self.manifest); data['items'][self.key]['ground'] = []
        with self.assertRaises(ValueError): self.load(data)
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
    def compressed(self, data=None):
        raw = json.dumps(self.manifest if data is None else data).encode()
        compressed = gzip.compress(raw)
        (self.root / 'items.json.gz').write_bytes(compressed)
        return dict(kind='items', version=2, payload='items.json.gz', sha256=hashlib.sha256(compressed).hexdigest(), uncompressedBytes=len(raw))
    def test_compressed_payload_uses_same_geometry_and_texture_validation(self):
        result = self.load(self.compressed())
        self.assertEqual(0x345678, result['items'][self.key][items.CONTEXTS[0]][0]['color'])
        broken = copy.deepcopy(self.manifest); broken['items'][self.key][items.CONTEXTS[0]][0]['color'] = True
        with self.assertRaises(ValueError): self.load(self.compressed(broken))
    def test_compressed_payload_rejects_corruption_size_and_traversal(self):
        for field, value in (('payload','../items.json.gz'), ('sha256','f'*64), ('uncompressedBytes',True), ('uncompressedBytes',items.MAX_PAYLOAD_BYTES+1), ('uncompressedBytes',1)):
            envelope = self.compressed(); envelope[field] = value
            with self.assertRaises(ValueError): self.load(envelope)
        envelope = self.compressed(); data = bytearray((self.root/'items.json.gz').read_bytes()); data[-8] ^= 1
        (self.root/'items.json.gz').write_bytes(data); envelope['sha256'] = hashlib.sha256(data).hexdigest()
        with self.assertRaises(ValueError): self.load(envelope)
    def test_compressed_payload_cannot_follow_external_symlink(self):
        envelope = self.compressed()
        with tempfile.TemporaryDirectory() as outside:
            source = pathlib.Path(outside)/'items.json.gz'; source.write_bytes((self.root/'items.json.gz').read_bytes())
            target = self.root/'items.json.gz'; target.unlink(); target.symlink_to(source)
            with self.assertRaises(ValueError): self.load(envelope)
    def test_valid_payload_over_old64MiBLimit_can_be_imported(self):
        prefix = json.dumps(self.manifest).encode()[:-1] + b',"padding":"'
        count = len(prefix)
        with gzip.open(self.root/'items.json.gz', 'wb') as stream:
            stream.write(prefix)
            chunk = b'a' * (1024*1024)
            for _ in range(65): stream.write(chunk); count += len(chunk)
            stream.write(b'"}'); count += 2
        data = (self.root/'items.json.gz').read_bytes()
        envelope = dict(kind='items', version=2, payload='items.json.gz', sha256=hashlib.sha256(data).hexdigest(), uncompressedBytes=count)
        result = self.load(envelope)
        self.assertEqual(65*1024*1024, len(result['padding']))
        self.assertEqual(0x345678, result['items'][self.key][items.CONTEXTS[0]][0]['color'])
