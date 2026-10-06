import copy
import hashlib
import importlib.util
import json
import pathlib
import struct
import tempfile
import unittest
import zlib

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "tools/import_minecraft_textures.py"
spec = importlib.util.spec_from_file_location("texture_import", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def png():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"\0\xff\0\0\xff")) + chunk(b"IEND", b"")


class TextureManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.image = self.root / "assets/minecraft/textures/block/stone.png"
        self.image.parent.mkdir(parents=True)
        self.image.write_bytes(png())
        face = {"texture": "minecraft:block/stone", "tint": False}
        self.manifest = {"format": "uebridge-block-textures", "version": 1,
                         "blocks": {"minecraft:stone": {key: copy.deepcopy(face) for key in ("top", "side", "bottom")}},
                         "textures": {"minecraft:block/stone": {"file": "assets/minecraft/textures/block/stone.png", "width": 1, "height": 1, "sha256": hashlib.sha256(png()).hexdigest()}}}
        self.path = self.root / "manifest.json"

    def load(self, manifest=None):
        self.path.write_text(json.dumps(self.manifest if manifest is None else manifest), encoding="utf-8")
        return module.load_texture_manifest(self.path)

    def test_checked_png_manifest_resolves_without_unreal(self):
        result = self.load()
        self.assertEqual(str(self.image.resolve()), result["textures"]["minecraft:block/stone"]["source"])

    def test_corrupt_hash_and_dimensions_rejected(self):
        for key, value in (("sha256", "0" * 64), ("width", 2), ("height", True), ("width", 100000)):
            with self.subTest(key=key, value=value):
                manifest = copy.deepcopy(self.manifest)
                manifest["textures"]["minecraft:block/stone"][key] = value
                with self.assertRaises(ValueError):
                    self.load(manifest)

    def test_path_traversal_absolute_and_symlink_escape_rejected(self):
        for value in ("../secret.png", "/tmp/secret.png", "C:/secret.png", "assets\\secret.png"):
            manifest = copy.deepcopy(self.manifest)
            manifest["textures"]["minecraft:block/stone"]["file"] = value
            with self.assertRaises(ValueError):
                self.load(manifest)
        with tempfile.TemporaryDirectory() as outside:
            image = pathlib.Path(outside) / "stone.png"
            image.write_bytes(png())
            self.image.unlink(); self.image.symlink_to(image)
            with self.assertRaises(ValueError):
                self.load()

    def test_unknown_face_and_nonboolean_tint_rejected(self):
        for key, value in (("texture", "minecraft:block/missing"), ("tint", "false")):
            manifest = copy.deepcopy(self.manifest)
            manifest["blocks"]["minecraft:stone"]["top"][key] = value
            with self.assertRaises(ValueError):
                self.load(manifest)

    def test_png_crc_and_truncation_rejected_even_with_matching_sha(self):
        for data in (png()[:-1], png()[:40] + b"bad" + png()[43:]):
            self.image.write_bytes(data)
            self.manifest["textures"]["minecraft:block/stone"]["sha256"] = hashlib.sha256(data).hexdigest()
            with self.assertRaises(ValueError):
                self.load()

    def test_wrong_format_and_empty_palette_rejected(self):
        for change in ({"version": True}, {"version": 2}, {"format": "other"}, {"blocks": {}}, {"textures": {}}):
            manifest = copy.deepcopy(self.manifest); manifest.update(change)
            with self.assertRaises(ValueError):
                self.load(manifest)


if __name__ == "__main__":
    unittest.main()
