"""Check original shader generation with a graph mock; not UE compilation."""
import types
import unittest
import setup_realistic_physics as realistic


class Node:
    def __init__(self): self.properties = {}
    def set_editor_property(self, key, value): self.properties[key] = value


class Material(Node): pass


class OriginalMaterialGraph(unittest.TestCase):
    def test_all_variants_connect_save_and_reuse_without_bundled_assets(self):
        assets, nodes, connections, compiled = {}, [], {}, []
        def load(path): return assets.get(path)
        def create(name, root, *_):
            value = Material(); assets[root + '/' + name] = value; return value
        def expression(material, kind, *_):
            value = kind(); value.material = material; nodes.append(value); return value
        def wire(source, output, target, pin):
            kind = target.kind
            if kind == 'TextureSampleParameter2D':
                return pin == 'UVs'
            if kind == 'Custom':
                return pin in {value.properties['input_name'] for value in target.properties['inputs']}
            if kind == 'Multiply':
                return pin in ('A', 'B')
            if kind == 'ComponentMask':
                return pin == ''
            return False
        def clear(material):
            nodes[:] = [node for node in nodes if node.material is not material]
        def connect(source, _, prop):
            connections[(id(source.material), prop)] = source; return True
        properties = types.SimpleNamespace(**{key: key for key in ('MP_BASE_COLOR', 'MP_EMISSIVE_COLOR', 'MP_ROUGHNESS', 'MP_SPECULAR', 'MP_OPACITY', 'MP_NORMAL', 'MP_REFRACTION')})
        unreal = types.SimpleNamespace(
            EditorAssetLibrary=types.SimpleNamespace(make_directory=lambda *_: None, save_loaded_asset=lambda *_: True),
            AssetToolsHelpers=types.SimpleNamespace(get_asset_tools=lambda: types.SimpleNamespace(create_asset=create)),
            Material=Material, MaterialFactoryNew=Node, CustomInput=Node,
            MaterialEditingLibrary=types.SimpleNamespace(create_material_expression=expression, connect_material_expressions=wire, delete_all_material_expressions=clear, connect_material_property=connect, get_material_property_input_node=lambda material, prop: connections.get((id(material), prop)), recompile_material=compiled.append),
            MaterialShadingModel=types.SimpleNamespace(MSM_DEFAULT_LIT='lit', MSM_UNLIT='unlit'),
            BlendMode=types.SimpleNamespace(BLEND_TRANSLUCENT='translucent'), MaterialProperty=properties,
            TranslucencyLightingMode=types.SimpleNamespace(TLM_SURFACE_PER_PIXEL_LIGHTING='surface'),
            CustomMaterialOutputType=types.SimpleNamespace(CMOT_FLOAT2='float2', CMOT_FLOAT3='float3'),
            LinearColor=lambda *args: args, load_asset=load,
        )
        for name in ('ScalarParameter', 'VectorParameter', 'TextureSampleParameter2D', 'TextureCoordinate', 'Custom', 'Multiply', 'WorldPosition', 'VertexColor', 'ComponentMask'):
            setattr(unreal, 'MaterialExpression' + name, type(name, (Node,), {'kind': name}))
        texture = types.SimpleNamespace(blueprint_get_size_x=lambda: 16, blueprint_get_size_y=lambda: 512)
        sprites = {key: texture for key in ('block/sand', 'block/tnt_side', 'block/obsidian', 'block/water_still', 'block/lava_still')}
        first = realistic.setup_realistic_materials(unreal, sprites)
        self.assertEqual(len(first), 14)
        self.assertEqual(len(compiled), 14)
        sample = next(node for node in nodes if node.kind == 'TextureSampleParameter2D')
        uv = next(node for node in nodes if node.kind == 'TextureCoordinate')
        self.assertFalse(wire(uv, '', sample, 'Coordinates'))
        self.assertTrue(wire(uv, '', sample, 'UVs'))
        self.assertTrue(all(m.properties['used_with_instanced_static_meshes'] for m in first))
        for kind in ('Sand', 'Tnt', 'Rock', 'Water', 'Lava', 'Fire', 'Smoke'):
            for style, shading in (('Simple', 'unlit'), ('Lit', 'lit')):
                material = assets[f'/Game/Bridge/Realistic/M_Realistic{kind}_{style}_v1']
                self.assertEqual(material.properties['shading_model'], shading)
                self.assertIn((id(material), 'MP_EMISSIVE_COLOR'), connections)
        self.assertIn((id(assets['/Game/Bridge/Realistic/M_RealisticWater_Lit_v1']), 'MP_REFRACTION'), connections)
        self.assertEqual(assets['/Game/Bridge/Realistic/M_RealisticWater_Lit_v1'].properties['translucency_lighting_mode'], 'surface')
        self.assertTrue(assets['/Game/Bridge/Realistic/M_RealisticWater_Lit_v1'].properties['screen_space_reflections'])
        self.assertTrue(any('Clock*8' in node.properties.get('code', '') for node in nodes))
        self.assertEqual(realistic.setup_realistic_materials(unreal, sprites), first)
        self.assertEqual(len(compiled), 14)


if __name__ == '__main__': unittest.main()
