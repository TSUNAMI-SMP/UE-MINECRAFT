import copy
import hashlib
import importlib.util
import json
import pathlib
import struct
import tempfile
import types
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
        for change in ({"version": True}, {"version": 3}, {"format": "other"}, {"blocks": {}}, {"textures": {}}):
            manifest = copy.deepcopy(self.manifest); manifest.update(change)
            with self.assertRaises(ValueError):
                self.load(manifest)

    def test_particle_sprite_and_tint_are_optional_and_checked(self):
        block = self.manifest["blocks"]["minecraft:stone"]
        block["particle"] = {"texture": "minecraft:block/stone", "tint": False, "color": 0xffffff}
        self.assertEqual(0xffffff, self.load()["blocks"]["minecraft:stone"]["particle"]["color"])
        for key, value in (("texture", "minecraft:block/missing"), ("tint", 1), ("color", True), ("color", -1), ("color", 0x1000000)):
            with self.subTest(key=key, value=value):
                manifest = copy.deepcopy(self.manifest)
                manifest["blocks"]["minecraft:stone"]["particle"][key] = value
                with self.assertRaises(ValueError):
                    self.load(manifest)

    def model_manifest(self):
        value = copy.deepcopy(self.manifest)
        value["version"] = 2
        block = value["blocks"]["minecraft:stone"]
        block.update(defaultState="facing=north,half=bottom", states={
            "facing=north,half=bottom": {"collision": [[0, 0, 0, 1, 0.5, 1]], "outline": [[0, 0, 0, 1, 0.5, 1]]},
            "facing=east,half=bottom": {"collision": [[0, 0, 0, 1, 0.5, 1]], "outline": [[0, 0, 0, 1, 0.5, 1]]}})
        value["blockstates"] = {"minecraft:stone": {"variants": {
            "facing=north": {"model": "minecraft:block/stone"},
            "facing=east": [{"model": "block/stone", "y": 90, "uvlock": True, "weight": 2}]}}}
        value["models"] = {"minecraft:block/stone": {"textures": {"all": "minecraft:block/stone"}, "elements": [{
            "from": [0, 0, 0], "to": [16, 8, 16], "faces": {"up": {"texture": "minecraft:block/stone", "uv": [0, 0, 16, 16], "rotation": 90, "tintindex": 0}}}]}}
        return value

    def test_v2_nonfull_states_models_and_rotations(self):
        value = self.model_manifest()
        result = self.load(value)
        self.assertEqual(0.5, result["blocks"]["minecraft:stone"]["states"]["facing=north,half=bottom"]["collision"][0][4])
        self.assertEqual(90, result["blockstates"]["minecraft:stone"]["variants"]["facing=east"][0]["y"])

    def test_invalid_native_state_reports_block_and_reexport_instruction(self):
        for key, default in (("facing=NORTH,half=bottom", True), ("facing=east,half=BOTTOM", False)):
            with self.subTest(key=key, default=default):
                value = self.model_manifest()
                block = value["blocks"]["minecraft:stone"]
                if default:
                    block["defaultState"] = key
                else:
                    block["states"][key] = copy.deepcopy(next(iter(block["states"].values())))
                with self.assertRaises(ValueError) as caught:
                    self.load(value)
                self.assertIn("minecraft:stone", str(caught.exception))
                self.assertIn(key, str(caught.exception))
                self.assertIn("re-export textures", str(caught.exception))

    def test_v2_rejects_invalid_shapes_and_noncanonical_states(self):
        for box in ([0, 0, 0, 1, 0, 1], [0, 0, 0, 1, 1, float("nan")], [0, 0, 0, 1, True, 1], [0, 0, 0, 5, 1, 1]):
            value = self.model_manifest()
            value["blocks"]["minecraft:stone"]["states"]["facing=north,half=bottom"]["collision"] = [box]
            with self.assertRaises(ValueError):
                self.load(value)
        for fields in ({"cannotConnect": 1}, {"solidFaces": ["north", "north"]}, {"solidFaces": ["unknown"]}, {"solidFaces": [[]]}):
            value = self.model_manifest()
            value["blocks"]["minecraft:stone"]["states"]["facing=north,half=bottom"].update(fields)
            with self.assertRaises(ValueError):
                self.load(value)
        for key in ("half=bottom,facing=north", "facing=north,facing=east", "facing=North", "facing=north=other"):
            value = self.model_manifest()
            value["blocks"]["minecraft:stone"]["defaultState"] = key
            with self.assertRaises(ValueError):
                self.load(value)

    def test_v2_rejects_unresolved_models_textures_and_bad_rotations(self):
        for change in ("model", "texture", "rotation", "element"):
            value = self.model_manifest()
            face = value["models"]["minecraft:block/stone"]["elements"][0]["faces"]["up"]
            if change == "model":
                value["blockstates"]["minecraft:stone"]["variants"]["facing=north"]["model"] = "minecraft:block/missing"
            elif change == "texture":
                face["texture"] = "#unresolved"
            elif change == "rotation":
                face["rotation"] = 45
            else:
                value["models"]["minecraft:block/stone"]["elements"][0]["rotation"] = {"origin": [8, 8, 8], "axis": "y", "angle": 90}
            with self.assertRaises(ValueError):
                self.load(value)

    def test_v2_multipart_boolean_conditions_and_cross_planes(self):
        value = self.model_manifest()
        value["blockstates"]["minecraft:stone"] = {"multipart": [{"when": {"OR": [{"facing": "north|east"}, {"AND": [{"half": "bottom"}, {"facing": "south"}]}]}, "apply": {"model": "minecraft:block/stone"}}]}
        value["models"]["minecraft:block/stone"]["elements"][0]["to"][0] = 0
        self.assertEqual(2, self.load(value)["version"])
        value["blockstates"]["minecraft:stone"]["multipart"][0]["when"] = {"OR": []}
        with self.assertRaises(ValueError):
            self.load(value)

    def test_texture_alpha_mode_is_optional_and_strict(self):
        for mode in ("opaque", "cutout", "translucent"):
            value = copy.deepcopy(self.manifest)
            value["textures"]["minecraft:block/stone"]["alphaMode"] = mode
            self.assertEqual(mode, self.load(value)["textures"]["minecraft:block/stone"]["alphaMode"])
        for mode in (None, True, "transparent", 1, {}):
            value = copy.deepcopy(self.manifest)
            value["textures"]["minecraft:block/stone"]["alphaMode"] = mode
            with self.assertRaises(ValueError):
                self.load(value)


class MaterialGraphTest(unittest.TestCase):
    """Exercise the importer graph builder, not a second copy of its formulas."""
    def setUp(self):
        class Material:
            def __init__(self):
                self.properties, self.nodes, self.outputs = {}, [], {}
            def set_editor_property(self, name, value):
                if name not in ("blend_mode", "opacity_mask_clip_value", "two_sided", "translucency_lighting_mode"):
                    raise RuntimeError("Unexpected material property " + name)
                self.properties[name] = value
        class Expression:
            def __init__(self, kind):
                self.kind, self.properties, self.inputs = kind, {}, {}
            def set_editor_property(self, name, value):
                self.properties[name] = value
        self.assets_by_path, self.saved = {}, []
        self.unreal = types.SimpleNamespace(Material=Material, MaterialFactoryNew=lambda: None, LinearColor=lambda *args: args,
            BlendMode=types.SimpleNamespace(BLEND_MASKED="masked", BLEND_TRANSLUCENT="translucent"),
            TranslucencyLightingMode=types.SimpleNamespace(TLM_SURFACE="surface"),
            MaterialProperty=types.SimpleNamespace(MP_BASE_COLOR="base", MP_EMISSIVE_COLOR="emissive", MP_OPACITY_MASK="mask", MP_OPACITY="opacity", MP_ROUGHNESS="roughness", MP_SPECULAR="specular"),
            load_asset=lambda path: self.assets_by_path.get(path))
        for name in ("TextureSampleParameter2D", "VectorParameter", "ScalarParameter", "Constant3Vector", "LinearInterpolate", "Multiply", "Constant"):
            setattr(self.unreal, "MaterialExpression" + name, name)
        self.assets = types.SimpleNamespace(does_asset_exist=lambda path: path in self.assets_by_path,
            save_loaded_asset=lambda material, force: self.saved.append(material) is None)
        def create(name, root, cls, factory):
            result = cls(); self.assets_by_path[root + "/" + name] = result; return result
        self.tools = types.SimpleNamespace(create_asset=create)
        def node(material, kind, x, y):
            result = Expression(kind); material.nodes.append(result); return result
        def connect(a, output, b, pin):
            b.inputs[pin] = (a, output); return True
        def output(node_value, pin, target):
            for material in self.assets_by_path.values():
                if node_value in material.nodes:
                    material.outputs[target] = (node_value, pin); return True
            return False
        self.editing = types.SimpleNamespace(create_material_expression=node, connect_material_expressions=connect,
            connect_material_property=output, recompile_material=lambda material: None,
            get_texture_parameter_names=lambda material: [n.properties["parameter_name"] for n in material.nodes if n.kind == "TextureSampleParameter2D"],
            get_scalar_parameter_names=lambda material: [n.properties["parameter_name"] for n in material.nodes if n.kind == "ScalarParameter"])

    def build(self, mode):
        return module._model_parent(self.unreal, self.assets, self.tools, self.editing, "/Game/Bridge/Minecraft", object(), mode)

    def test_glass_alpha_is_connected_to_opacity_and_cutouts_to_mask(self):
        glass = self.build("translucent")
        leaves = self.build("cutout")
        self.assertEqual("translucent", glass.properties["blend_mode"])
        self.assertEqual("surface", glass.properties["translucency_lighting_mode"])
        self.assertNotIn("mask", glass.outputs)
        self.assertEqual("A", glass.outputs["opacity"][1])
        self.assertEqual("TextureSampleParameter2D", glass.outputs["opacity"][0].kind)
        self.assertEqual("masked", leaves.properties["blend_mode"])
        self.assertNotIn("opacity", leaves.outputs)
        self.assertEqual("A", leaves.outputs["mask"][1])
        self.assertTrue(leaves.properties["two_sided"])

    def test_lighting_switch_controls_both_base_and_emissive(self):
        material = self.build("opaque")
        base, emissive = material.outputs["base"][0], material.outputs["emissive"][0]
        self.assertIs(base.inputs["Alpha"][0], emissive.inputs["Alpha"][0])
        self.assertEqual("BridgeUnlit", base.inputs["Alpha"][0].properties["parameter_name"])
        self.assertIs(base.inputs["A"][0], emissive.inputs["B"][0])
        self.assertEqual(0.0, base.inputs["B"][0].properties["r"])
        self.assertEqual(0.0, emissive.inputs["A"][0].properties["r"])
        self.assertIs(material, self.build("cutout"))  # Opaque and cutout reuse masked master.
        self.assertEqual(2, len(self.saved))

    def test_diffuse_master_has_no_white_specular_addition(self):
        material = self.build("opaque")
        specular = material.outputs["specular"][0]
        self.assertEqual("BridgeSpecular", specular.properties["parameter_name"])
        self.assertEqual(0.0, specular.properties["default_value"])
        count = len(material.nodes)
        self.build("opaque")
        self.assertEqual(count, len(material.nodes))

    def test_incomplete_existing_graph_is_rejected(self):
        material = self.build("translucent")
        material.nodes[:] = [node for node in material.nodes if node.properties.get("parameter_name") != "BridgeUnlit"]
        with self.assertRaisesRegex(RuntimeError, "tint/lighting"):
            self.build("translucent")


if __name__ == "__main__":
    unittest.main()
