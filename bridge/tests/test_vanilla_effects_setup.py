"""Exercise the editor helper's graph and safeguards without claiming UE rendering."""
import contextlib
import importlib.util
import pathlib
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "tools/setup_vanilla_effects.py"
SPEC = importlib.util.spec_from_file_location("setup_vanilla_effects_tested", SCRIPT)
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


class PropertyObject:
    def __init__(self):
        self.properties = {}

    def set_editor_property(self, name, value):
        self.properties[name] = value

    def get_editor_property(self, name):
        return self.properties[name]


class Material(PropertyObject):
    def __init__(self):
        super().__init__()
        self.nodes = []
        self.outputs = {}


class Collection(PropertyObject):
    def __init__(self):
        super().__init__()
        self.properties.update(scalar_parameters=[], vector_parameters=[])


class Expression(PropertyObject):
    def __init__(self):
        super().__init__()
        self.inputs = {}

    def get_class(self):
        return types.SimpleNamespace(get_name=lambda: type(self).__name__)

    def set_editor_property(self, name, value):
        # UE 5.8's real editor rejected default_value on this expression. Keep
        # that observed API contract strict so the helper cannot regress.
        if type(self).__name__ == "PerInstanceCustomData" and name != "data_index":
            raise AttributeError("PerInstanceCustomData has no editor property: " + name)
        if type(self).__name__ == 'ScreenPosition':
            raise AttributeError('ScreenPosition exposes ViewportUV as an output, not an editor mapping property')
        super().set_editor_property(name, value)


class Receiver(PropertyObject):
    def get_editor_property(self, name):
        return self.properties.get(name)


class Editor:
    def __init__(self, project):
        self.playing, self.dirty, self.fail_connections, self.fail_parameters = False, False, False, False
        self.receivers = [Receiver()]
        self.assets, self.saved, self.level_saves, self.recompiled = {}, [], 0, []
        self.api = types.ModuleType("unreal")
        api = self.api
        api.Paths = types.SimpleNamespace(project_dir=lambda: str(project), convert_relative_path_to_full=lambda path: path)
        api.UnrealEditorSubsystem, api.EditorActorSubsystem, api.LevelEditorSubsystem = object(), object(), object()
        subsystems = {
            api.UnrealEditorSubsystem: types.SimpleNamespace(get_game_world=lambda: object() if self.playing else None,
                get_editor_world=lambda: types.SimpleNamespace(get_path_name=lambda: '/Game/Test.Test')),
            api.EditorActorSubsystem: types.SimpleNamespace(get_all_level_actors=lambda: self.receivers),
            api.LevelEditorSubsystem: types.SimpleNamespace(save_current_level=self.save_level),
        }
        api.get_editor_subsystem = subsystems.__getitem__
        api.EditorLoadingAndSavingUtils = types.SimpleNamespace(get_dirty_map_packages=lambda: [object()] if self.dirty else [], save_dirty_packages=lambda *args: True)
        api.BridgeReceiver, api.Material = Receiver, Material
        api.MaterialFactoryNew = object
        api.MaterialParameterCollection = Collection
        api.MaterialParameterCollectionFactoryNew = object
        api.CollectionScalarParameter = api.CollectionVectorParameter = PropertyObject
        api.CustomInput = PropertyObject
        api.CustomMaterialOutputType = types.SimpleNamespace(CMOT_FLOAT3='float3', CMOT_FLOAT2='float2')
        api.MaterialShadingModel = types.SimpleNamespace(MSM_DEFAULT_LIT='lit', MSM_UNLIT='unlit')
        api.load_asset = lambda path: self.assets.get(path, object() if path.startswith("/Engine/") else None)
        api.EditorAssetLibrary = types.SimpleNamespace(does_asset_exist=lambda path: path in self.assets,
            save_loaded_asset=self.save_asset, list_assets=lambda *args: list(self.assets))
        api.AssetToolsHelpers = types.SimpleNamespace(get_asset_tools=lambda: types.SimpleNamespace(create_asset=self.create_asset))
        api.ScopedEditorTransaction = lambda name: contextlib.nullcontext()
        api.BlendMode = types.SimpleNamespace(BLEND_MASKED="masked", BLEND_OPAQUE="opaque", BLEND_TRANSLUCENT="translucent", BLEND_ADDITIVE="additive")
        api.MaterialDomain=types.SimpleNamespace(MD_POST_PROCESS='postprocess')
        api.BlendableLocation=types.SimpleNamespace(BL_SCENE_COLOR_AFTER_TONEMAPPING='aftertonemapping')
        api.SceneTextureId=types.SimpleNamespace(PPI_POST_PROCESS_INPUT0='postprocessinput0')
        api.MaterialProperty = types.SimpleNamespace(MP_BASE_COLOR="base", MP_OPACITY_MASK="mask", MP_OPACITY="opacity", MP_ROUGHNESS="roughness", MP_SPECULAR="specular", MP_EMISSIVE_COLOR="emissive")
        api.LinearColor = lambda *values: values
        api.log = lambda text: None
        for name in ("Time", "TextureCoordinate", "Constant2Vector", "Multiply", "PerInstanceCustomData", "Add",
                     "AppendVector", "VertexInterpolator", "TextureSampleParameter2D", "VectorParameter", "Constant",
                     "ScalarParameter", "CollectionParameter", "VertexNormalWS", "PixelNormalWS", "Custom", "VertexColor", "LinearInterpolate", "Constant3Vector", "ComponentMask", "SceneTexture", "ScreenPosition", "ViewSize", "TextureObjectParameter"):
            setattr(api, "MaterialExpression" + name, type(name, (Expression,), {}))
        api.MaterialEditingLibrary = types.SimpleNamespace(delete_all_material_expressions=self.clear,
            create_material_expression=self.create_expression, connect_material_expressions=self.connect,
            connect_material_property=self.connect_property, recompile_material=self.recompiled.append,
            get_texture_parameter_names=lambda material: [] if self.fail_parameters else self.parameter_names(material, "TextureSampleParameter2D"),
            get_vector_parameter_names=lambda material: self.parameter_names(material, "VectorParameter"),
            get_scalar_parameter_names=lambda material: self.parameter_names(material, "ScalarParameter"),
            get_material_property_input_node=lambda material, prop: material.outputs.get(prop,(None,None))[0])

    @staticmethod
    def parameter_names(material, kind):
        return [node.properties["parameter_name"] for node in material.nodes if type(node).__name__ == kind]

    def create_asset(self, name, folder, kind, factory):
        material = kind()
        self.assets[folder + "/" + name] = material
        return material

    @staticmethod
    def clear(material):
        material.nodes.clear()
        material.outputs.clear()

    @staticmethod
    def create_expression(material, kind, x, y):
        expression = kind()
        expression.material = material
        material.nodes.append(expression)
        return expression

    def connect(self, source, output, target, pin):
        if self.fail_connections:
            return False
        if type(source).__name__ in ("VectorParameter", "VertexColor", "TextureSampleParameter2D") and output not in ("", "RGB", "R", "G", "B", "A"):
            return False  # These expressions have no combined RG or RGBA output pin.
        if type(source).__name__ == "VertexInterpolator" and output:
            return False  # UE interpolators expose an unnamed output, not VertexColor's RGB pin.
        if type(source).__name__ == 'ScreenPosition' and output not in ('ViewportUV','PixelPosition'):
            return False
        if type(target).__name__ == "TextureSampleParameter2D":
            # Texture sample inputs expose UVs, not the C++ field Coordinates.
            # MaterialEditingLibrary accepts an empty name for the first input.
            if pin not in ("", "UVs"):
                return False
            pin = "UVs"
        target.inputs[pin] = (source, output)
        return True

    @staticmethod
    def connect_property(source, output, prop):
        source.material.outputs[prop] = (source, output)
        return True

    def save_asset(self, material, only_dirty):
        self.saved.append(material)
        return True

    def save_level(self):
        self.level_saves += 1
        return True

    def run(self):
        with patch.dict(sys.modules, {"unreal": self.api}):
            return HELPER.setup_vanilla_effects()


def evaluate(node, uv, offsets):
    name = type(node).__name__
    def input_value(pin, default=None):
        connection = node.inputs.get(pin)
        return default if connection is None else evaluate(connection[0], uv, offsets)
    if name == "TextureCoordinate":
        return uv
    if name == "Constant2Vector":
        return (node.properties["r"], node.properties["g"])
    if name == "PerInstanceCustomData":
        return offsets[node.properties["data_index"]]
    if name == "VertexInterpolator":
        return input_value("")
    if name == "AppendVector":
        a, b = input_value("A"), input_value("B")
        return (a if isinstance(a, tuple) else (a,)) + (b if isinstance(b, tuple) else (b,))
    left = input_value("A")
    right = input_value("B", node.properties.get("const_b"))
    operation = (lambda a, b: a + b) if name == "Add" else (lambda a, b: a * b)
    if isinstance(left, tuple):
        return tuple(operation(a, b) for a, b in zip(left, right))
    return operation(left, right)


class VanillaEffectsSetupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        project = pathlib.Path(self.temp.name)
        (project / "UEBridge.uproject").touch()
        (project / "Source/UEBridge").mkdir(parents=True)
        (project / "Source/UEBridge/BridgeVanillaEffects.h").touch()
        (project / "bridge_lighting_materials.py").write_text((SCRIPT.parent / "bridge_lighting_materials.py").read_text())
        (project / "setup_vanilla_effects.py").write_text(SCRIPT.read_text())
        self.editor = Editor(project)

    def test_quarter_sprite_uvs_and_explicit_custom_data_transport(self):
        material = self.editor.run()
        sample = next(node for node in material.nodes if type(node).__name__ == "TextureSampleParameter2D")
        coordinate = sample.inputs["UVs"][0]
        for uv in ((0, 0), (1, 1), (.2, .7)):
            for offset in ((0, 0), (.3, .45), (.749, .749)):
                actual = evaluate(coordinate, uv, offset)
                self.assertAlmostEqual(offset[0] + .25 - .25 * uv[0], actual[0])
                self.assertAlmostEqual(offset[1] + .25 * uv[1], actual[1])
                self.assertTrue(all(0 <= value <= 1 for value in actual))
        interpolation = next(node for node in material.nodes if type(node).__name__ == "VertexInterpolator")
        self.assertEqual("AppendVector", type(interpolation.inputs[""][0]).__name__)
        self.assertEqual((sample, "A"), material.outputs["mask"])
        self.assertEqual("masked", material.properties["blend_mode"])
        self.assertTrue(material.properties["two_sided"])
        self.assertTrue(material.properties["used_with_instanced_static_meshes"])

    def test_native_particle_light_uses_three_explicit_channels_without_altering_uv(self):
        material = self.editor.run()
        indices = sorted(node.properties['data_index'] for node in material.nodes if type(node).__name__ == 'PerInstanceCustomData')
        self.assertEqual([0, 1, 2, 3, 4], indices)
        lightmap = next(node for node in material.nodes if type(node).__name__ == 'Custom' and node.properties.get('description') == 'Bridge native lightmap v1')
        blend = lightmap.inputs['Light'][0]
        light, output = blend.inputs['B']
        self.assertEqual('VertexInterpolator', type(light).__name__)
        self.assertEqual('', output)
        for sky, block, shade in ((0, 0, 1), (1, 0, 1), (.2, .7, .9)):
            self.assertEqual((sky, block, shade), evaluate(light, (.1, .9), (.3, .4, sky, block, shade)))
        use_vertex = blend.inputs['Alpha'][0]
        self.assertEqual('BridgeUseVertexLight', use_vertex.properties['parameter_name'])
        self.assertEqual(1.0, use_vertex.properties['default_value'])
        self.assertEqual('lit', material.properties['shading_model'])
        self.assertIn('base', material.outputs)
        self.assertIn('emissive', material.outputs)
        self.assertIn('specular', material.outputs)
        self.assertEqual('ParticleColor', next(node for node in material.nodes if type(node).__name__ == 'VectorParameter' and node.properties.get('parameter_name') == 'ParticleColor').properties['parameter_name'])
    def test_standalone_render_migration_rebuilds_legacy_dust_with_instance_light(self):
        import runpy
        owned = Material(); user_material = Material()
        self.editor.assets['/Game/Bridge/Minecraft/M_MinecraftDust_v1'] = owned
        self.editor.assets['/Game/Bridge/Minecraft/UserMaterial'] = user_material
        with patch.dict(sys.modules, {'unreal': self.editor.api}):
            runpy.run_path(str(SCRIPT.parent / 'setup_bridge_rendering.py'))['setup_bridge_rendering']()
        self.assertIsNot(owned, self.editor.receivers[0].properties['vanilla_particle_material'])
        self.assertIs(user_material, self.editor.assets['/Game/Bridge/Minecraft/UserMaterial'])
        self.assertEqual([], user_material.nodes)
        current = self.editor.receivers[0].properties['vanilla_particle_material']
        self.assertEqual([0,1,2,3,4], sorted(node.properties['data_index'] for node in current.nodes if type(node).__name__ == 'PerInstanceCustomData'))
        self.assertEqual(2, self.editor.level_saves)
        self.assertIn('outline_material', self.editor.receivers[0].properties)

    def test_outline_level_save_failure_restores_old_assignment_without_saving_other_assets(self):
        import runpy
        old = object()
        receiver = self.editor.receivers[0]
        receiver.set_editor_property('outline_material', old)
        unrelated = []
        self.editor.api.EditorLoadingAndSavingUtils.save_dirty_packages = lambda *args: unrelated.append(args)
        attempts = []
        level = self.editor.api.get_editor_subsystem(self.editor.api.LevelEditorSubsystem)
        def save():
            attempts.append(True)
            return len(attempts) != 1
        level.save_current_level = save
        with patch.dict(sys.modules, {'unreal': self.editor.api}):
            with self.assertRaisesRegex(RuntimeError, 'Cannot save/verify'):
                runpy.run_path(str(SCRIPT.parent / 'setup_bridge_rendering.py'))['setup_bridge_rendering']()
        self.assertIs(old, receiver.get_editor_property('outline_material'))
        self.assertEqual(2, len(attempts))
        self.assertEqual([], unrelated)

    def test_missing_lighting_helper_blocks_asset_edits(self):
        (pathlib.Path(self.temp.name) / 'bridge_lighting_materials.py').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Copy bridge_lighting_materials'):
            self.editor.run()
        self.assertEqual({}, self.editor.assets)
        self.assertEqual([], self.editor.saved)

    def test_retry_reuses_complete_registered_graph_and_saves_receiver_assignment(self):
        material = self.editor.run()
        count = len(material.nodes)
        self.assertIs(material, self.editor.run())
        self.assertEqual(count, len(material.nodes))
        self.assertEqual(3, len(self.editor.assets))
        self.assertIs(material, self.editor.receivers[0].properties["vanilla_particle_material"])
        self.assertIsNotNone(self.editor.receivers[0].properties["vanilla_death_poof_material"])
        self.assertEqual(2, self.editor.level_saves)

    def test_play_and_unsaved_level_block_changes(self):
        for field in ("playing", "dirty"):
            setattr(self.editor, field, True)
            with self.assertRaises(RuntimeError):
                self.editor.run()
            setattr(self.editor, field, False)
            self.assertEqual({}, self.editor.assets)

    def test_receiver_count_must_be_exactly_one(self):
        for receivers in ([], [Receiver(), Receiver()]):
            self.editor.receivers = receivers
            with self.assertRaisesRegex(RuntimeError, "exactly one"):
                self.editor.run()
            self.assertEqual({}, self.editor.assets)

    def test_occupied_asset_is_preserved(self):
        import hashlib
        occupied = object()
        helper = pathlib.Path(self.temp.name) / 'bridge_lighting_materials.py'
        revision = hashlib.sha256(b'dust-import-v2\0' + helper.read_bytes()).hexdigest()[:12]
        self.editor.assets["/Game/Bridge/Minecraft/M_MinecraftDust_v2_" + revision] = occupied
        with self.assertRaisesRegex(RuntimeError, "another asset type"):
            self.editor.run()
        self.assertEqual([], self.editor.saved)
        self.assertEqual(0, self.editor.level_saves)
        self.assertIs(occupied, next(iter(self.editor.assets.values())))

    def test_failed_graph_or_parameter_validation_does_not_assign_receiver(self):
        for field in ("fail_connections", "fail_parameters"):
            setattr(self.editor, field, True)
            with self.assertRaises(RuntimeError):
                self.editor.run()
            setattr(self.editor, field, False)
            self.assertEqual({}, self.editor.receivers[0].properties)
            self.assertTrue(all(isinstance(asset, Collection) for asset in self.editor.saved))
            self.assertEqual(0, self.editor.level_saves)

    def test_failed_new_graph_preserves_previously_registered_material(self):
        import hashlib
        previous = Material()
        sentinel = object()
        previous.nodes.append(sentinel)
        self.editor.receivers[0].properties['vanilla_particle_material'] = previous
        self.editor.assets['/Game/Bridge/Minecraft/M_MinecraftDust_v1'] = previous
        self.editor.fail_connections = True
        with self.assertRaises(RuntimeError):
            self.editor.run()
        self.assertIs(previous, self.editor.receivers[0].properties['vanilla_particle_material'])
        self.assertEqual([sentinel], previous.nodes)


if __name__ == "__main__":
    unittest.main()
