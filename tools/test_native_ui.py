"""Exercise UI package validation before it can modify the Unreal asset registry."""
import copy
import hashlib
import importlib.util
import json
import pathlib
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("import_minecraft_ui", pathlib.Path(__file__).with_name("import_minecraft_ui.py"))
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


def png(width=2, height=2):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    scanlines = b"".join(b"\0" + b"\xff\xff\xff\xff" * width for _ in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")


class NativeUiValidation(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = pathlib.Path(self.directory.name)
        self.data = png()
        (self.root / "icon.png").write_bytes(self.data)
        self.entry = dict(file="icon.png", sha256=hashlib.sha256(self.data).hexdigest(), width=2, height=2)
        self.manifest = dict(kind="native-ui", version=1, sprites={"hud/hotbar": dict(self.entry)}, items=[dict(id="minecraft:stone", name="石", icon="icon.png", sha256=self.entry["sha256"], width=2, height=2, maxCount=64, block="minecraft:stone", modelKey="")], font=dict(self.entry, glyphs=[dict(codepoint=65, x=0, y=0, width=2, height=2, advance=6, drawWidth=1, drawHeight=1, ascent=7)]))

    def load(self, manifest=None):
        path = self.root / "manifest.json"
        path.write_text(json.dumps(manifest or self.manifest), encoding="utf-8")
        return ui.load_ui_manifest(path)

    def test_valid_japanese_name_and_scaled_font(self):
        result = self.load()
        self.assertEqual(result["items"][0]["name"], "石")
        self.assertEqual(result["font"]["glyphs"][0]["drawWidth"], 1)
        self.assertEqual(result["items"][0]["source"], str(self.root / "icon.png"))

    def test_modified_texture_rejected(self):
        (self.root / "icon.png").write_bytes(self.data + b"tampered")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.load()

    def test_escaping_path_rejected(self):
        self.manifest["items"][0]["icon"] = "../icon.png"
        with self.assertRaisesRegex(ValueError, "relative PNG"):
            self.load()

    def test_symlink_escape_rejected(self):
        with tempfile.TemporaryDirectory() as outside:
            external = pathlib.Path(outside) / "asset.png"
            external.write_bytes(self.data)
            (self.root / "escape.png").symlink_to(external)
            self.manifest["items"][0]["icon"] = "escape.png"
            with self.assertRaisesRegex(ValueError, "escaping"):
                self.load()

    def test_duplicate_items_rejected(self):
        self.manifest["items"].append(dict(self.manifest["items"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate native UI item"):
            self.load()

    def test_spawn_entity_uses_exported_id(self):
        self.manifest["items"][0]["spawnType"] = "custom:entity/special"
        self.assertEqual(self.load()["items"][0]["spawnType"], "custom:entity/special")

    def test_invalid_spawn_entity_rejected(self):
        self.manifest["items"][0]["spawnType"] = "not an entity"
        with self.assertRaisesRegex(ValueError, "spawn entity"):
            self.load()

    def test_resource_pack_namespaced_sprite_is_retained(self):
        self.manifest['sprites']['my_pack:hud/custom'] = dict(self.entry)
        self.assertIn('my_pack:hud/custom', self.load()['sprites'])

    def test_current_celestial_sprites_keep_phase_keys_and_verified_pixels(self):
        phases = ('full_moon', 'waning_gibbous', 'third_quarter', 'waning_crescent',
                  'new_moon', 'waxing_crescent', 'first_quarter', 'waxing_gibbous')
        keys = ['environment/celestial/sun'] + ['environment/celestial/moon/' + phase for phase in phases]
        for key in keys:
            self.manifest['sprites'][key] = dict(self.entry)
        result = self.load()
        for key in keys:
            self.assertEqual(result['sprites'][key]['sha256'], self.entry['sha256'])
            self.assertEqual(pathlib.Path(result['sprites'][key]['source']).read_bytes(), self.data)

    def test_malformed_sprite_namespace_is_rejected(self):
        self.manifest['sprites']['my_pack:other:hud/custom'] = dict(self.entry)
        with self.assertRaisesRegex(ValueError, 'sprite ID'):
            self.load()

    def test_glyph_outside_atlas_rejected(self):
        self.manifest["font"]["glyphs"][0]["x"] = 1
        with self.assertRaisesRegex(ValueError, "atlas bounds"):
            self.load()

    def test_nan_glyph_metrics_rejected(self):
        self.manifest["font"]["glyphs"][0]["advance"] = float("nan")
        with self.assertRaisesRegex(ValueError, "advance"):
            self.load()

    def test_unsupported_font_allowed_with_explicit_fallback(self):
        self.manifest.pop("font")
        self.assertNotIn("font", self.load())

    def test_crc_rejected_even_with_updated_hash(self):
        damaged = bytearray(self.data)
        damaged[29] ^= 1
        (self.root / "icon.png").write_bytes(damaged)
        self.manifest["sprites"]["hud/hotbar"]["sha256"] = hashlib.sha256(damaged).hexdigest()
        with self.assertRaisesRegex(ValueError, "chunk"):
            self.load()


if __name__ == "__main__":
    unittest.main()
