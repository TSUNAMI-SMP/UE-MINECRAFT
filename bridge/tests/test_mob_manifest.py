import copy
import hashlib
import importlib.util
import json
import math
import pathlib
import struct
import tempfile
import unittest
import zlib

MODULE = pathlib.Path(__file__).resolve().parents[2] / "tools/import_minecraft_mobs.py"
SPEC = importlib.util.spec_from_file_location("import_minecraft_mobs", MODULE)
MOBS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOBS)


def png():
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"\x00\xff\xff\xff\xff")) + chunk(b"IEND", b"")


class MobManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.texture = png()
        (self.root / "texture.png").write_bytes(self.texture)
        self.key = "a" * 64
        transform = [0, 24, 0, 0, 0, 0, 1, 1, 1]
        appearance = {"type": "minecraft:zombie", "texture": "texture.png", "textureHash": hashlib.sha256(self.texture).hexdigest(),
                      "textureWidth": 1, "textureHeight": 1, "rendererScale": [1, 1, 1], "rendererOffset": [0, 0, 0],
                      "parts": [{"name": "root", "parent": -1, "transform": transform,
                                 "quads": [[[0, 0, 0, 0, 0], [1, 0, 0, 1, 0], [1, 1, 0, 1, 1], [0, 1, 0, 0, 1]]]}],
                      "walkFrames": [[transform] for _ in range(16)]}
        self.manifest = {"kind": "mobs", "version": 1, "appearances": {self.key: appearance},
                         "entities": {"00000000-0000-0000-0000-000000000001": self.key}}

    def tearDown(self):
        self.temp.cleanup()

    def load(self, data=None):
        file = self.root / "manifest.json"
        file.write_text(json.dumps(data or self.manifest), encoding="utf-8")
        return MOBS.load_mob_manifest(file)

    def test_complete_local_export_with_source_and_hash(self):
        result = self.load()
        self.assertEqual(str(self.root / "texture.png"), result["appearances"][self.key]["source"])
        self.assertEqual(64, len(result["manifestHash"]))

    def test_texture_traversal_checksum_and_crc_rejected(self):
        data = copy.deepcopy(self.manifest)
        data["appearances"][self.key]["texture"] = "../texture.png"
        with self.assertRaises(ValueError):
            self.load(data)
        data = copy.deepcopy(self.manifest)
        data["appearances"][self.key]["textureHash"] = "0" * 64
        with self.assertRaises(ValueError):
            self.load(data)
        changed = self.texture[:-1] + bytes([self.texture[-1] ^ 1])
        (self.root / "texture.png").write_bytes(changed)
        data["appearances"][self.key]["textureHash"] = hashlib.sha256(changed).hexdigest()
        with self.assertRaises(ValueError):
            self.load(data)

    def test_parent_cycle_and_pose_mismatch_rejected(self):
        data = copy.deepcopy(self.manifest)
        data["appearances"][self.key]["parts"][0]["parent"] = 0
        with self.assertRaises(ValueError):
            self.load(data)
        data = copy.deepcopy(self.manifest)
        data["appearances"][self.key]["walkFrames"][0] = []
        with self.assertRaises(ValueError):
            self.load(data)

    def test_nonfinite_coerced_and_invalid_entity_rejected(self):
        data = copy.deepcopy(self.manifest)
        data["appearances"][self.key]["parts"][0]["transform"][0] = math.nan
        with self.assertRaises(ValueError):
            self.load(data)
        data = copy.deepcopy(self.manifest)
        data["appearances"][self.key]["rendererScale"][0] = "1"
        with self.assertRaises(ValueError):
            self.load(data)
        data = copy.deepcopy(self.manifest)
        data["entities"] = {"bad": self.key}
        with self.assertRaises(ValueError):
            self.load(data)

    def test_native_model_units_basis_and_quaternion(self):
        position, rotation, scale = MOBS.minecraft_part_transform([16, 24, -8, 0, 0, 0, 2, 3, 4])
        self.assertEqual((50, -100, -150), position)
        self.assertEqual((0, 0, 0, 1), rotation)
        self.assertEqual((4, 2, 3), scale)
        _, rotation, _ = MOBS.minecraft_part_transform([0, 0, 0, math.pi/2, 0, 0, 1, 1, 1])
        self.assertAlmostEqual(math.sqrt(.5), rotation[1])
        self.assertAlmostEqual(math.sqrt(.5), rotation[3])

    def test_setup_reports_missing_export_before_importing_unreal(self):
        with self.assertRaisesRegex(FileNotFoundError, "/uebridge mobs export"):
            MOBS.setup_minecraft_mobs(self.root)


if __name__ == "__main__":
    unittest.main()
