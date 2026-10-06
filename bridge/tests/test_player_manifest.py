import copy
import hashlib
import importlib.util
import json
import os
import pathlib
import struct
import tempfile
import unittest
import zlib


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "tools/import_minecraft_player.py"
SPEC = importlib.util.spec_from_file_location("player_import", SCRIPT)
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


def skin_png(width=64, height=64):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    # A generated checker pattern represents opaque base pixels and transparent outer layers.
    rows = b"".join(b"\0" + b"".join(
        bytes((x * 3 % 256, y * 5 % 256, 127, 255 if x < 32 else 0))
        for x in range(width)) for y in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


class PlayerManifestTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.image = self.root / "skin.png"
        self.image.write_bytes(skin_png())
        self.path = self.root / "manifest.json"
        self.manifest = {
            "kind": "player", "version": 1,
            "skin": {"file": "skin.png", "width": 64, "height": 64, "model": "classic",
                     "sha256": hashlib.sha256(self.image.read_bytes()).hexdigest()},
            "player": {"uuid": "12345678-1234-5678-9abc-123456789abc", "name": "BridgePlayer"},
        }

    def load(self, manifest=None):
        self.path.write_text(json.dumps(self.manifest if manifest is None else manifest), encoding="utf-8")
        return IMPORTER.load_player_manifest(self.path)

    def test_current_classic_and_slim_skin_resolve_without_unreal_or_network(self):
        for model in ("classic", "slim"):
            with self.subTest(model=model):
                manifest = copy.deepcopy(self.manifest)
                manifest["skin"]["model"] = model
                result = self.load(manifest)
                self.assertEqual(model, result["skin"]["model"])
                self.assertEqual(str(self.image.resolve()), result["skin"]["source"])
                self.assertEqual("BridgePlayer", result["player"]["name"])

    def test_strict_player_and_skin_metadata(self):
        cases = (("version", True), ("version", 2), ("kind", "textures"),
                 ("skin", "invalid"), ("player", None))
        for field, value in cases:
            with self.subTest(field=field, value=value):
                manifest = copy.deepcopy(self.manifest)
                manifest[field] = value
                with self.assertRaises(ValueError):
                    self.load(manifest)
        for parent, field, values in (
            ("skin", "model", ["unknown", False]),
            ("skin", "width", [True, 128, "64"]),
            ("skin", "height", [32, 128, "64"]),
            ("skin", "sha256", ["0" * 63, "X" * 64, None]),
            ("player", "name", ["", "x" * 65, 1]),
            ("player", "uuid", ["1-2-3-4-5", "12345678-1234-5678-9abc-123456789abz", None]),
        ):
            for value in values:
                with self.subTest(parent=parent, field=field, value=value):
                    manifest = copy.deepcopy(self.manifest)
                    manifest[parent][field] = value
                    with self.assertRaises(ValueError):
                        self.load(manifest)

    def test_png_corruption_is_rejected_even_with_matching_export_checksum(self):
        original = skin_png()
        data_cases = (original[:-1], original[:30] + b"bad" + original[33:],
                      original + b"trailing", skin_png(128, 64), b"not a png")
        for data in data_cases:
            with self.subTest(size=len(data)):
                self.image.write_bytes(data)
                self.manifest["skin"]["sha256"] = hashlib.sha256(data).hexdigest()
                with self.assertRaises(ValueError):
                    self.load()

    def test_changed_image_checksum_is_rejected(self):
        self.manifest["skin"]["sha256"] = "0" * 64
        with self.assertRaises(ValueError):
            self.load()

    def test_skin_path_traversal_absolute_drive_and_backslash_are_rejected(self):
        for path in ("../skin.png", "/tmp/skin.png", "C:/skin.png", "assets\\skin.png"):
            with self.subTest(path=path):
                manifest = copy.deepcopy(self.manifest)
                manifest["skin"]["file"] = path
                with self.assertRaises(ValueError):
                    self.load(manifest)

    def test_symlink_cannot_import_a_skin_outside_the_export(self):
        with tempfile.TemporaryDirectory() as outside:
            image = pathlib.Path(outside) / "skin.png"
            image.write_bytes(skin_png())
            self.image.unlink()
            self.image.symlink_to(image)
            with self.assertRaises(ValueError):
                self.load()

    def test_nested_skin_inside_the_export_is_accepted(self):
        folder = self.root / "assets/player"
        folder.mkdir(parents=True)
        destination = folder / "skin.png"
        self.image.rename(destination)
        self.manifest["skin"]["file"] = "assets/player/skin.png"
        self.assertEqual(str(destination.resolve()), self.load()["skin"]["source"])

    def test_manifest_and_image_byte_budgets_are_enforced(self):
        self.manifest["padding"] = "x" * (16 * 1024)
        with self.assertRaises(ValueError):
            self.load()
        del self.manifest["padding"]
        self.image.write_bytes(b"x" * (1024 * 1024 + 1))
        with self.assertRaises(ValueError):
            self.load()

    def test_latest_completed_export_uses_modification_time_and_requested_kind(self):
        exports = self.root / "uebridge-export"
        older = exports / "player-z-old" / "manifest.json"
        newer = exports / "player-a-new" / "manifest.json"
        unrelated = exports / "textures-latest" / "manifest.json"
        for path in (older, newer, unrelated):
            path.parent.mkdir(parents=True)
            path.write_text("{}", encoding="utf-8")
        os.utime(older, ns=(1_000_000_000, 1_000_000_000))
        os.utime(newer, ns=(2_000_000_000, 2_000_000_000))
        os.utime(unrelated, ns=(3_000_000_000, 3_000_000_000))
        (exports / "player-incomplete").mkdir()
        self.assertEqual(newer, IMPORTER._latest_export(self.root, "player", "/uebridge player export"))

    def test_missing_export_explains_the_required_minecraft_command(self):
        with self.assertRaisesRegex(RuntimeError, "/uebridge player export"):
            IMPORTER._latest_export(self.root, "player", "/uebridge player export")


if __name__ == "__main__":
    unittest.main()
