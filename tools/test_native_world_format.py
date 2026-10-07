import copy
import hashlib
import json
import pathlib
import tempfile
import unittest

from native_world_format import NativeWorldError, resolve_world_file, validate_world_file, write_world_file


def fixture():
    header = {"type": "native_world", "schema": 1, "id": "450ec755-e088-4082-a459-0c49e9c932e7", "dimension": "minecraft:overworld",
              "origin": [-8.25, 64.0, -0.5], "spawn": [-8.25, 64.0, -0.5], "yaw": 90, "pitch": -4,
              "center": [-2, 8, -1], "radius": 1, "halfHeight": 1, "cells": 27,
              "vanillaLight": {"skyFactor": 1, "blockFactor": 1.5, "ambient": 0, "gamma": 0.5, "nightVision": 0,
                               "darkness": 0, "darkenWorld": 0, "skyColor": 0x80AAFF, "ambientColor": 0xFFFFFF, "hasSky": True}}
    cells = [{"type": "cell", "cell": [x, y, z], "palette": [], "blocks": [], "skyTop": [15] * 64}
             for y in range(7, 10) for z in range(-2, 1) for x in range(-3, 0)]
    target = next(c for c in cells if c["cell"] == [-2, 8, -1])
    target["palette"] = [["minecraft:oak_stairs", "facing=east,half=bottom,shape=straight,waterlogged=false", 0xAB7733, 0, 0]]
    target["blocks"] = [[511, 0, 12, 2]]
    return header, cells


class NativeWorldFormatTest(unittest.TestCase):
    def test_round_trip_preserves_negative_origin_state_light_and_empty_cells(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "world.ndjson"
            header, cells = fixture()
            result = write_world_file(path, header, iter(cells))
            self.assertEqual((result["cells"], result["rows"]), (27, 1))
            data = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(data, [header, *cells])
            # Breaking the block writes an empty cell, preserving the deletion on reopen.
            cells[13]["blocks"] = []
            self.assertEqual(write_world_file(path, header, cells)["rows"], 0)

    def test_failed_save_keeps_previous_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "world.ndjson"
            header, cells = fixture()
            write_world_file(path, header, cells)
            previous = path.read_bytes()
            with self.assertRaises(NativeWorldError):
                write_world_file(path, header, cells[:-1])
            self.assertEqual(path.read_bytes(), previous)
            self.assertEqual(list(path.parent.glob("*.tmp-*")), [])

    def test_corruption_and_invalid_coordinates_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "world.ndjson"
            header, cells = fixture()
            variants = []
            duplicate = copy.deepcopy(cells)
            duplicate[-1] = duplicate[0]
            variants.append((header, duplicate))
            out_of_bounds = copy.deepcopy(cells)
            out_of_bounds[0]["cell"] = [99, 8, -1]
            variants.append((header, out_of_bounds))
            duplicate_voxel = copy.deepcopy(cells)
            duplicate_voxel[13]["blocks"] *= 2
            variants.append((header, duplicate_voxel))
            wrong_type = copy.deepcopy(cells)
            wrong_type[13]["blocks"][0][1] = 1
            variants.append((header, wrong_type))
            invalid_light = copy.deepcopy(cells)
            invalid_light[13]["blocks"][0][2] = 16
            variants.append((header, invalid_light))
            nonfinite = copy.deepcopy(header)
            nonfinite["yaw"] = float("nan")
            variants.append((nonfinite, cells))
            for bad_header, bad_cells in variants:
                with self.subTest(bad_header=bad_header, bad_cells=bad_cells):
                    with self.assertRaises(ValueError):
                        write_world_file(path, bad_header, bad_cells)
            path.write_bytes(b'{"type": "native_world"\n')
            with self.assertRaises(NativeWorldError):
                validate_world_file(path)

    def test_manifest_checksum_and_path_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            path = root / "world.ndjson"
            write_world_file(path, *fixture())
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            manifest = {"world": {"file": "world.ndjson", "sha256": digest, "bytes": path.stat().st_size}}
            self.assertEqual(resolve_world_file(root / "native_manifest.json", manifest), path)
            for relative in ("../world.ndjson", "C:/world.ndjson", "/world.ndjson", "..\\world.ndjson"):
                manifest["world"]["file"] = relative
                with self.assertRaises(NativeWorldError):
                    resolve_world_file(root / "native_manifest.json", manifest)
            manifest["world"]["file"] = "world.ndjson"
            path.write_bytes(path.read_bytes() + b" ")
            with self.assertRaisesRegex(NativeWorldError, "checksum"):
                resolve_world_file(root / "native_manifest.json", manifest)

    def test_invalid_present_runtime_fields_cannot_replace_a_saved_inventory(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "world.ndjson"
            header, cells = fixture()
            inventory = {"version": 1, "selected": 3, "slots": [{"item": "minecraft:diamond", "count": 32}] + [{"item": "", "count": 0}] * 35}
            header["runtimeState"] = {"inventory": inventory, "health": 13.5, "lighting": False, "flying": True,
                                      "perspective": 2, "respawn": header["spawn"], "drops": [], "fuses": [], "mobs": []}
            write_world_file(path, header, cells)
            previous = path.read_bytes()
            corrupt = {"inventory": (None, [], "missing"), "health": ("20", True, -1, 21, float("nan")),
                       "lighting": (0, "false", None), "flying": (1, "true", None),
                       "perspective": (-1, 3, 1.5, True), "respawn": ([], [0, 0, 0], [float("inf"), 64, 0]),
                       "drops": ({}, [None], [{}] * 129), "fuses": ({}, [False], [{}] * 65), "mobs": (None, [{}] * 129)}
            for key, values in corrupt.items():
                for value in values:
                    damaged = copy.deepcopy(header)
                    damaged["runtimeState"][key] = value
                    with self.subTest(key=key, value=value):
                        with self.assertRaises(ValueError):
                            write_world_file(path, damaged, cells)
                        self.assertEqual(path.read_bytes(), previous)
                        self.assertEqual(list(path.parent.glob("*.tmp-*")), [])
            # Earlier valid snapshots may omit optional fields. Corrupt present
            # fields cannot take that compatibility path.
            header["runtimeState"] = {}
            write_world_file(path, header, cells)


if __name__ == "__main__":
    unittest.main()
