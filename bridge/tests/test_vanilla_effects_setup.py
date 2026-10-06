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


class Material(PropertyObject):
    def __init__(self):
        super().__init__()
        self.nodes = []
        self.outputs = {}


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
        super().set_editor_property(name, value)


class Receiver(PropertyObject):
    pass


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
            api.UnrealEditorSubsystem: types.SimpleNamespace(get_game_world=lambda: object() if self.playing else None),
            api.EditorActorSubsystem: types.SimpleNamespace(get_all_level_actors=lambda: self.receivers),
            api.LevelEditorSubsystem: types.SimpleNamespace(save_current_level=self.save_level),
        }
        api.get_editor_subsystem = subsystems.__getitem__
        api.EditorLoadingAndSavingUtils = types.SimpleNamespace(get_dirty_map_packages=lambda: [object()] if self.dirty else [])
        api.BridgeReceiver, api.Material = Receiver, Material
        api.MaterialFactoryNew = object
        api.load_asset = lambda path: self.assets.get(path, object() if path.startswith("/Engine/") else None)
        api.EditorAssetLibrary = types.SimpleNamespace(does_asset_exist=lambda path: path in self.assets,
            save_loaded_asset=self.save_asset)
        api.AssetToolsHelpers = types.SimpleNamespace(get_asset_tools=lambda: types.SimpleNamespace(create_asset=self.create_asset))
        api.ScopedEditorTransaction = lambda name: contextlib.nullcontext()
        api.BlendMode = types.SimpleNamespace(BLEND_MASKED="masked")
        api.MaterialProperty = types.SimpleNamespace(MP_BASE_COLOR="base", MP_OPACITY_MASK="mask", MP_ROUGHNESS="roughness")
        api.LinearColor = lambda *values: values
        api.log = lambda text: None
        for name in ("TextureCoordinate", "Constant2Vector", "Multiply", "PerInstanceCustomData", "Add",
                     "AppendVector", "VertexInterpolator", "TextureSampleParameter2D", "VectorParameter", "Constant"):
            setattr(api, "MaterialExpression" + name, type(name, (Expression,), {}))
        api.MaterialEditingLibrary = types.SimpleNamespace(delete_all_material_expressions=self.clear,
            create_material_expression=self.create_expression, connect_material_expressions=self.connect,
            connect_material_property=self.connect_property, recompile_material=self.recompiled.append,
            get_texture_parameter_names=lambda material: [] if self.fail_parameters else self.parameter_names(material, "TextureSampleParameter2D"),
            get_vector_parameter_names=lambda material: self.parameter_names(material, "VectorParameter"))

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
        return (input_value("A"), input_value("B"))
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
        self.editor = Editor(project)

    def test_quarter_sprite_uvs_and_explicit_custom_data_transport(self):
        material = self.editor.run()
        sample = next(node for node in material.nodes if type(node).__name__ == "TextureSampleParameter2D")
        coordinate = sample.inputs["Coordinates"][0]
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

    def test_retry_repairs_owned_asset_and_saves_receiver_assignment(self):
        material = self.editor.run()
        count = len(material.nodes)
        material.nodes.append(object())
        self.assertIs(material, self.editor.run())
        self.assertEqual(count, len(material.nodes))
        self.assertEqual(1, len(self.editor.assets))
        self.assertIs(material, self.editor.receivers[0].properties["vanilla_particle_material"])
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
        occupied = object()
        self.editor.assets["/Game/Bridge/Minecraft/M_MinecraftDust_v1"] = occupied
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
            self.assertEqual([], self.editor.saved)
            self.assertEqual(0, self.editor.level_saves)


if __name__ == "__main__":
    unittest.main()
