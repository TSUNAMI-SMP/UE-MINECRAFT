"""Integrity and dependency tests; fixtures contain no Minecraft source/assets."""
import json
import pathlib
import tempfile
import unittest
import zipfile
from collect_reference import collect, dependencies, digest, safe_member, source_inputs, vanilla_assets


class CollectorTests(unittest.TestCase):
    def fixture(self, root):
        inputs = root / 'inputs'
        inputs.mkdir()
        with zipfile.ZipFile(inputs / 'minecraft-common-1.21.11+build.6-v2-sources.jar', 'w') as z:
            z.writestr('net/minecraft/test/Main.java', 'package net.minecraft.test;\nimport net.minecraft.other.Dependency;\nclass Main { Sibling sibling; }')
            z.writestr('net/minecraft/test/Sibling.java', 'package net.minecraft.test; class Sibling {}')
            z.writestr('net/minecraft/other/Dependency.java', 'package net.minecraft.other; class Dependency {}')
        with zipfile.ZipFile(inputs / 'minecraft-clientonly-1.21.11+build.6-v2-sources.jar', 'w') as z:
            z.writestr('net/minecraft/client/Test.java', 'package net.minecraft.client; class Test {}')
        with zipfile.ZipFile(inputs / 'minecraft-client.jar', 'w') as z:
            values = {
                'blockstates/grass_block.json': {'variants': {'snowy=false': {'model': 'minecraft:block/grass_block'}}},
                'items/grass_block.json': {'model': {'type': 'minecraft:model', 'model': 'minecraft:block/grass_block'}},
                'models/block/grass_block.json': {'parent': 'block/parent', 'textures': {'side': 'block/dirt', 'overlay': 'block/grass_block_side_overlay'}},
                'models/block/grass_block_snow.json': {'parent': 'minecraft:block/parent'},
                'models/block/parent.json': {'parent': 'block/block'},
                'models/block/block.json': {'textures': {'particle': '#side'}}
            }
            for p, obj in values.items():
                z.writestr('assets/minecraft/' + p, json.dumps(obj))
            for p in ('colormap/grass.png', 'colormap/foliage.png', 'block/dirt.png', 'block/grass_block_side_overlay.png'):
                z.writestr('assets/minecraft/textures/' + p, b'fixture-image')
            z.writestr('assets/minecraft/textures/block/dirt.png.mcmeta', '{}')
        data = (inputs / 'minecraft-client.jar').read_bytes()
        (inputs / 'mojang_minecraft_info.json').write_text(json.dumps({'id': '1.21.11', 'downloads': {'client': {'sha1': digest(data, 'sha1'), 'url': 'https://example.invalid/fixture'}}}))
        (inputs / 'mappings.tiny').write_bytes(b'tiny\t2\t0\tintermediary\tnamed\n')
        targets = {'minecraft': '1.21.11', 'mappings': 'test mappings', 'topics': [{'id': 'test', 'title': 'test', 'patterns': ['net/minecraft/test/Main.java'], 'terms': ['sibling']}]}
        return inputs, targets

    def test_recursive_dependencies(self):
        with tempfile.TemporaryDirectory() as temp:
            inputs, _ = self.fixture(pathlib.Path(temp))
            sources, _, _ = source_inputs(inputs)
            found = dependencies(sources, ['net/minecraft/test/Main.java'])
            self.assertEqual(found, {'net/minecraft/test/Main.java', 'net/minecraft/test/Sibling.java', 'net/minecraft/other/Dependency.java'})

    def test_asset_graph_includes_parent_overlay_dirt_and_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            inputs, _ = self.fixture(pathlib.Path(temp))
            assets = vanilla_assets(inputs / 'minecraft-client.jar')
            for path in ('models/block/block.json', 'models/block/parent.json', 'textures/block/dirt.png', 'textures/block/dirt.png.mcmeta', 'textures/block/grass_block_side_overlay.png'):
                self.assertIn('assets/minecraft/' + path, assets)

    def test_collect_archive_and_indices_match(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            inputs, targets = self.fixture(root)
            result = collect(inputs, root / 'out', targets)
            self.assertEqual(result['all_source_count'], 4)
            self.assertEqual(result['selected_source_count'], 3)
            with zipfile.ZipFile(root / 'out/Minecraft-Reference-selected.zip') as z:
                self.assertIsNone(z.testzip())
                self.assertFalse(any(p.startswith('sources/all/') for p in z.namelist()))
                self.assertIn('sources/selected/net/minecraft/other/Dependency.java', z.namelist())
                for entry in json.loads(z.read('asset-index.json')):
                    self.assertEqual(digest(z.read('vanilla/' + entry['path'])), entry['sha256'])

    def test_wrong_official_hash_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            inputs, targets = self.fixture(root)
            info = json.loads((inputs / 'mojang_minecraft_info.json').read_text())
            info['downloads']['client']['sha1'] = '0' * 40
            (inputs / 'mojang_minecraft_info.json').write_text(json.dumps(info))
            with self.assertRaisesRegex(ValueError, 'SHA-1'):
                collect(inputs, root / 'out', targets)
            self.assertFalse((root / 'out').exists())

    def test_wrong_version_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            inputs, targets = self.fixture(root)
            info = json.loads((inputs / 'mojang_minecraft_info.json').read_text())
            info['id'] = '1.21.10'
            (inputs / 'mojang_minecraft_info.json').write_text(json.dumps(info))
            with self.assertRaisesRegex(ValueError, 'metadata'):
                collect(inputs, root / 'out', targets)

    def test_output_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            inputs, targets = self.fixture(root)
            collect(inputs, root / 'out', targets)
            with self.assertRaises(FileExistsError):
                collect(inputs, root / 'out', targets)

    def test_unsafe_members(self):
        for p in ('../escape.java', '/absolute.java', 'C:/escape.java', 'a\\b.java'):
            with self.assertRaises(ValueError):
                safe_member(p)


if __name__ == '__main__':
    unittest.main()
