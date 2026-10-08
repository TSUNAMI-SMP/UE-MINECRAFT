"""Strict graph contracts cover import stages without claiming UE compilation.

Epic documents selecting vector channels with ComponentMask rather than
inventing combined outputs:
https://dev.epicgames.com/documentation/en-us/unreal-engine/vector-operation-material-expressions-in-unreal-engine
https://dev.epicgames.com/documentation/en-us/unreal-engine/material-parameter-expressions-in-unreal-engine
"""
import pathlib
import runpy
import tempfile
import unittest

from test_vanilla_effects_setup import Editor, Material, PropertyObject

TOOLS = pathlib.Path(__file__).resolve().parents[2] / "tools"
LIGHTING = runpy.run_path(str(TOOLS / "bridge_lighting_materials.py"))
TEXTURES = runpy.run_path(str(TOOLS / "import_minecraft_textures.py"))


def evaluate_uv(node, uv, parameters):
    kind = type(node).__name__
    if kind == "TextureCoordinate":
        return uv
    if kind == "VectorParameter":
        return parameters[node.properties["parameter_name"]][:3]
    if kind == "ComponentMask":
        source = evaluate_uv(node.inputs[""][0], uv, parameters)
        return tuple(value for value, channel in zip(source, ("r", "g", "b", "a")) if node.properties[channel])
    left = evaluate_uv(node.inputs["A"][0], uv, parameters)
    right = evaluate_uv(node.inputs["B"][0], uv, parameters)
    if kind == "Multiply":
        return tuple(a * b for a, b in zip(left, right))
    if kind == "Add":
        return tuple(a + b for a, b in zip(left, right))
    raise AssertionError("Unexpected UV node: " + kind)


class MaterialGraphContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.editor = Editor(pathlib.Path(self.temp.name))

    def test_celestial_moon_uvs_use_two_channel_masks(self):
        materials = LIGHTING["ensure_native_sky_materials"](self.editor.api)
        celestial = materials["M_NativeCelestial_v1"]
        sample = next(node for node in celestial.nodes if type(node).__name__ == "TextureSampleParameter2D")
        uv_graph = sample.inputs["UVs"][0]
        masks = [node for node in celestial.nodes if type(node).__name__ == "ComponentMask"]
        self.assertEqual(2, len(masks))
        self.assertTrue(all(node.properties == dict(r=True, g=True, b=False, a=False) for node in masks))
        for phase in range(8):
            parameters = {"CelestialUVScale": (.25, .5, 99, 1), "CelestialUVOffset": (phase % 4 / 4, phase // 4 / 2, 99, 1)}
            for uv in ((0, 0), (1, 1), (.2, .7)):
                self.assertEqual((uv[0] / 4 + phase % 4 / 4, uv[1] / 2 + phase // 4 / 2), evaluate_uv(uv_graph, uv, parameters))
        alpha = celestial.outputs["opacity"][0]
        self.assertEqual((sample, "A"), alpha.inputs["A"])
        self.assertEqual("CelestialOpacity", alpha.inputs["B"][0].properties["parameter_name"])
        self.assertEqual(1.0, alpha.inputs["B"][0].properties["default_value"])
        self.assertEqual("additive", celestial.properties["blend_mode"])
        color = celestial.outputs["emissive"][0]
        self.assertEqual((sample, "RGB"), color.inputs["TextureColor"])
        self.assertEqual((alpha, ""), color.inputs["Alpha"])
        self.assertIn('cl-bl', color.properties['code'])
        self.assertEqual("unlit", celestial.properties["shading_model"])

    def test_inverse_hud_keeps_four_rectangle_and_crop_channels(self):
        material=LIGHTING['ensure_native_inverse_hud_material'](self.editor.api)
        self.assertEqual('postprocess',material.properties['material_domain'])
        self.assertEqual('aftertonemapping',material.properties['blendable_location'])
        output=material.outputs['emissive'][0]
        self.assertEqual('ViewportUV',output.inputs['UV'][1])
        self.assertEqual({},output.inputs['UV'][0].properties)
        self.assertIn('source*(1-destination)+destination*(1-source)',output.properties['code'])
        for slot in range(3):
            for name in ('Rect','Crop'):
                append=output.inputs[name+str(slot)][0]
                self.assertEqual('AppendVector',type(append).__name__)
                self.assertEqual('RGB',append.inputs['A'][1])
                self.assertEqual('A',append.inputs['B'][1])
            self.assertEqual('TextureObjectParameter',type(output.inputs['Texture'+str(slot)][0]).__name__)

    def test_vector_and_vertex_named_outputs_are_strict(self):
        material = Material()
        target = self.editor.create_expression(material, self.editor.api.MaterialExpressionMultiply, 0, 0)
        for cls in (self.editor.api.MaterialExpressionVectorParameter, self.editor.api.MaterialExpressionVertexColor):
            source = self.editor.create_expression(material, cls, 0, 0)
            for invalid in ("RG", "RGBA"):
                self.assertFalse(self.editor.connect(source, invalid, target, "A"))
            for output in ("", "RGB", "R", "G", "B", "A"):
                self.assertTrue(self.editor.connect(source, output, target, "A"))

    def test_entity_overlay_follows_diffuse_and_precedes_lightmap(self):
        material=Material()
        sample=self.editor.create_expression(material,self.editor.api.MaterialExpressionTextureSampleParameter2D,0,0)
        LIGHTING['wire_vanilla_lighting'](self.editor.api,self.editor.api.MaterialEditingLibrary,material,sample,use_vertex=False)
        hurt=next(n for n in material.nodes if type(n).__name__=='LinearInterpolate' and 'B' in n.inputs and n.inputs['B'][0].properties.get('parameter_name')=='BridgeHurtColor')
        self.assertEqual(77/255,hurt.inputs['Alpha'][0].properties['const_b'])
        shade_product=hurt.inputs['A'][0]
        self.assertEqual('Multiply',type(shade_product).__name__)
        self.assertEqual(dict(r=False,g=False,b=True,a=False),shade_product.inputs['B'][0].properties)
        self.assertEqual('Custom',type(shade_product.inputs['A'][0]).__name__)
        glint=material.outputs['emissive'][0].inputs['B'][0]
        self.assertEqual(0.0,glint.inputs['Enabled'][0].properties['default_value'])
        self.assertEqual('BridgeGlintTexture',glint.inputs['Glint'][0].properties['parameter_name'])
        final_decode=glint.inputs['Pixel'][0]
        display_product=final_decode.inputs['Color'][0]
        self.assertIs(hurt,display_product.inputs['A'][0])
        lightmap=display_product.inputs['B'][0]
        self.assertIn('floor(saturate(c)*255.0+.5)/255.0',lightmap.properties['code'])

    def test_item_master_has_actor_light_and_reuses_without_vertex_requirement(self):
        api = self.editor.api
        material = TEXTURES["_model_parent"](api, api.EditorAssetLibrary, api.AssetToolsHelpers.get_asset_tools(), api.MaterialEditingLibrary,
            "/Game/Bridge/Native/Packages/Test/Items", object(), "masked")
        scalar_names = set(self.editor.parameter_names(material, "ScalarParameter"))
        self.assertNotIn("BridgeUseVertexLight", scalar_names)
        self.assertTrue({"FaceTint", "BridgeUnlit", "BridgeSpecular"}.issubset(scalar_names))
        self.assertFalse(any(type(node).__name__ == "VertexColor" for node in material.nodes))
        original_nodes = list(material.nodes)
        self.assertIs(material, TEXTURES["_model_parent"](api, api.EditorAssetLibrary, api.AssetToolsHelpers.get_asset_tools(), api.MaterialEditingLibrary,
            "/Game/Bridge/Native/Packages/Test/Items", object(), "masked"))
        self.assertEqual(original_nodes, material.nodes)

    def test_terrain_master_keeps_vertex_payload_requirement(self):
        api = self.editor.api
        material = TEXTURES["_model_parent"](api, api.EditorAssetLibrary, api.AssetToolsHelpers.get_asset_tools(), api.MaterialEditingLibrary,
            "/Game/Bridge/Native/Packages/Test/Blocks", object(), "masked")
        self.assertIn("BridgeUseVertexLight", self.editor.parameter_names(material, "ScalarParameter"))
        self.assertTrue(any(type(node).__name__ == "VertexColor" for node in material.nodes))

    def test_missing_default_texture_preserves_existing_sky_graph(self):
        old = Material()
        sentinel = object()
        old.nodes.append(sentinel)
        self.editor.assets["/Game/Bridge/Minecraft/M_NativeSky_v1"] = old
        self.editor.api.load_asset = self.editor.assets.get
        with self.assertRaisesRegex(RuntimeError, "default texture is unavailable"):
            LIGHTING["ensure_native_sky_materials"](self.editor.api)
        self.assertEqual([sentinel], old.nodes)
        self.assertNotIn("/Game/Bridge/Minecraft/M_NativeCelestial_v1", self.editor.assets)


class TexturePalettePublication(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.editor = Editor(pathlib.Path(self.temp.name))
        self.palette = PropertyObject()
        self.original = {name: {"minecraft:old": object()} for name in
            ("materials", "particle_textures", "particle_tints", "particle_colors", "face_materials", "models", "atlas_rects", "atlas_pages", "atlas_materials")}
        self.palette.properties.update(self.original)
        self.receiver = self.editor.receivers[0]
        self.receiver.set_editor_property("texture_palette", self.palette)
        self.values = {name: {"minecraft:new": object()} for name in self.original if not name.startswith("atlas_")}

    def commit(self, atlas_import=None):
        return TEXTURES["_commit_texture_palette"](self.editor.api, self.editor.api.EditorAssetLibrary,
            self.receiver, self.palette, self.values, atlas_import)

    def assert_old_palette(self):
        self.assertIs(self.palette, self.receiver.get_editor_property("texture_palette"))
        self.assertEqual(self.original, self.palette.properties)

    def test_save_failure_restores_registered_palette_maps(self):
        attempts = []
        def save(palette, unused):
            attempts.append(dict(palette.properties))
            return len(attempts) != 1
        self.editor.api.EditorAssetLibrary.save_loaded_asset = save
        with self.assertRaisesRegex(RuntimeError, "Cannot save texture palette"):
            self.commit()
        self.assert_old_palette()
        self.assertEqual(self.original, attempts[-1])

    def test_level_save_failure_restores_already_published_asset(self):
        attempts = []
        level = self.editor.api.get_editor_subsystem(self.editor.api.LevelEditorSubsystem)
        def save():
            attempts.append(True)
            return len(attempts) != 1
        level.save_current_level = save
        with self.assertRaisesRegex(RuntimeError, "Cannot save/verify current level"):
            self.commit()
        self.assert_old_palette()
        self.assertEqual(2, len(attempts))

    def test_atlas_failure_restores_earlier_block_and_atlas_updates(self):
        def broken_atlas():
            self.palette.set_editor_property("atlas_rects", {"minecraft:new": object()})
            self.palette.set_editor_property("atlas_pages", {"minecraft:new": "page0"})
            raise RuntimeError("Cannot connect atlas material")
        with self.assertRaisesRegex(RuntimeError, "Cannot connect atlas material"):
            self.commit(broken_atlas)
        self.assert_old_palette()

    def test_cleanup_failure_retains_original_import_error(self):
        self.editor.api.EditorAssetLibrary.save_loaded_asset = lambda *args: False
        level = self.editor.api.get_editor_subsystem(self.editor.api.LevelEditorSubsystem)
        attempts = []
        def broken_save():
            attempts.append(True)
            raise ValueError("cleanup failure")
        level.save_current_level = broken_save
        with self.assertRaisesRegex(RuntimeError, "Cannot save texture palette"):
            self.commit()
        self.assert_old_palette()
        self.assertEqual(1, len(attempts))

    def test_success_publishes_new_values_and_keeps_existing_assets(self):
        sentinel = object()
        self.editor.assets["/Game/MyOriginalWorld"] = sentinel
        self.commit()
        for name, value in self.values.items():
            self.assertEqual(value, self.palette.get_editor_property(name))
        self.assertIs(sentinel, self.editor.assets["/Game/MyOriginalWorld"])
        self.assertEqual(1, self.editor.level_saves)


if __name__ == "__main__":
    unittest.main()
