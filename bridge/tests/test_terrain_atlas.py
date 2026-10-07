"""Atlas round trips check pixels, gutters, alpha split, packing and repeat UV metadata."""
import importlib.util
import pathlib
import struct
import tempfile
import unittest
import zlib
ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("terrain_atlas", ROOT / "tools/import_minecraft_atlas.py")
atlas = importlib.util.module_from_spec(spec)
spec.loader.exec_module(atlas)


class TerrainAtlasTest(unittest.TestCase):
    def test_rgba_pixels_round_trip_and_png_size_guard(self):
        image = bytes([2, 18, 201, 255, 80, 70, 60, 0, 120, 10, 4, 127, 0, 0, 0, 255])
        encoded = atlas._encode_png(2, 2, image)
        self.assertEqual((2, 2, image), tuple([*atlas._decode_png(encoded)[:2], bytes(atlas._decode_png(encoded)[2])]))
        with self.assertRaises(ValueError):
            atlas._decode_png(encoded[:-10])

    def test_small_tiles_share_page_preserve_gutters_and_split_translucency(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = pathlib.Path(temporary)
            manifest = {"textures": {}}
            colors = {"minecraft:block/a": bytes([12, 120, 200, 255]), "minecraft:block/b": bytes([100, 1, 40, 0]), "minecraft:block/glass": bytes([2, 3, 4, 128])}
            for name, color in colors.items():
                source = folder / (name.split("/")[-1] + ".png")
                source.write_bytes(atlas._encode_png(2, 2, color * 4))
                manifest["textures"][name] = {"source": str(source), "alphaMode": "translucent" if name.endswith("glass") else "cutout"}
            result = atlas.build_atlases(manifest, folder / "cache", 32)
            self.assertEqual(2, len(result["pages"]))
            self.assertEqual(result["tiles"]["minecraft:block/a"]["page"], result["tiles"]["minecraft:block/b"]["page"])
            self.assertNotEqual(result["tiles"]["minecraft:block/a"]["page"], result["tiles"]["minecraft:block/glass"]["page"])
            for name, color in colors.items():
                tile = result["tiles"][name]
                page = next(value for value in result["pages"] if value["id"] == tile["page"])
                width, unused_height, image = atlas._decode_png(pathlib.Path(page["source"]).read_bytes())
                x, y = int(tile["rect"][0] * width), int(tile["rect"][1] * width)
                for dx, dy in ((0, 0), (1, 1), (-2, -2), (3, 3)):
                    index = ((y + dy) * width + x + dx) * 4
                    self.assertEqual(color, image[index:index + 4])
            self.assertEqual(result, atlas.build_atlases(manifest, folder / "cache", 32))

    def test_palette_four_bit_and_paeth_filter(self):
        def chunk(kind, content):
            return struct.pack(">I", len(content)) + kind + content + struct.pack(">I", zlib.crc32(kind + content) & 0xffffffff)
        indexed = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 4, 3, 0, 0, 0)) + chunk(b"PLTE", bytes([10, 20, 30, 100, 200, 50])) + chunk(b"tRNS", bytes([255, 0])) + chunk(b"IDAT", zlib.compress(b"\x00\x01")) + chunk(b"IEND", b"")
        self.assertEqual(bytes([10, 20, 30, 255, 100, 200, 50, 0]), atlas._decode_png(indexed)[2])
        # Paeth's first scanline predictor is the left RGBA pixel.
        paeth = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 1, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(bytes([4, 1, 2, 3, 4, 4, 4, 4, 4]))) + chunk(b"IEND", b"")
        self.assertEqual(bytes([1, 2, 3, 4, 5, 6, 7, 8]), atlas._decode_png(paeth)[2])

    def test_large_or_unsupported_tiles_keep_original_material(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = pathlib.Path(temporary)
            source = folder / "large.png"
            source.write_bytes(atlas._encode_png(32, 32, bytes([20, 30, 40, 255]) * 1024))
            result = atlas.build_atlases({"textures": {"minecraft:large": {"source": str(source)}}}, folder / "cache", 32)
            self.assertFalse(result["pages"])
            self.assertIn("minecraft:large", result["skipped"])
