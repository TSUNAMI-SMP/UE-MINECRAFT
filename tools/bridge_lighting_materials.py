"""Generated-only material wiring: UE PBR ON, native lightmap emissive OFF.

This helper is also imported by the atlas importer. No level, texture or user
material is deleted. Parameter collection values are per UE world at runtime.
"""

COLLECTION_PATH = '/Game/Bridge/Minecraft/MPC_BridgeLighting_v1'
SCALARS = {'BridgeVanillaMode': 0.0, 'BridgeSkyFactor': 1.0, 'BridgeBlockFactor': 1.5,
           'BridgeAmbient': 0.0, 'BridgeGamma': 0.5, 'BridgeNightVision': 0.0,
           'BridgeDarkness': 0.0, 'BridgeDarkenWorld': 0.0}


def ensure_lighting_collection(unreal):
    assets = unreal.EditorAssetLibrary
    value = unreal.load_asset(COLLECTION_PATH) if assets.does_asset_exist(COLLECTION_PATH) else None
    if value is None:
        value = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'MPC_BridgeLighting_v1', '/Game/Bridge/Minecraft', unreal.MaterialParameterCollection,
            unreal.MaterialParameterCollectionFactoryNew())
    if not isinstance(value, unreal.MaterialParameterCollection):
        raise RuntimeError('Generated lighting collection path is occupied by another asset')
    # Keep existing parameter GUIDs. Regenerating them disconnects collection expressions.
    changed = False
    scalars = list(value.get_editor_property('scalar_parameters'))
    vectors = list(value.get_editor_property('vector_parameters'))
    def add(parameters, name, default, cls):
        nonlocal changed
        if any(str(p.get_editor_property('parameter_name')) == name for p in parameters):
            return
        p = cls(); p.set_editor_property('parameter_name', name); p.set_editor_property('default_value', default)
        # UE's struct constructor creates the GUID; its id is not an editable property.
        parameters.append(p)
        changed = True
    for name, default in SCALARS.items():
        add(scalars, name, default, unreal.CollectionScalarParameter)
    for name in ('BridgeSkyColor', 'BridgeAmbientColor'):
        add(vectors, name, unreal.LinearColor(1, 1, 1, 1), unreal.CollectionVectorParameter)
    if changed:
        value.set_editor_property('scalar_parameters', scalars)
        value.set_editor_property('vector_parameters', vectors)
        if not assets.save_loaded_asset(value, False):
            raise RuntimeError('Cannot save generated lighting collection')
    return value


# The active 1.21.11 lightmap's environment factors arrive via localhost metadata.
# This is an algebraic transfer, not an exported Minecraft shader/resource asset.
LIGHTMAP_CODE = r'''
float sky = saturate(Light.r);
float block = saturate(Light.g);
float b = block / max(4.0 - 3.0 * block, 0.001) * BlockFactor;
float s = sky / max(4.0 - 3.0 * sky, 0.001) * SkyFactor;
float3 c = float3(b, b * ((b * .6 + .4) * .6 + .4), b * (b * b * .6 + .4));
c = (lerp(c, AmbientColor.rgb, Ambient) + SkyColor.rgb * s) * .96 + .03;
if (Ambient <= 0.00001) c *= lerp(float3(1,1,1), float3(.7,.6,.6), DarkenWorld);
float maximum = max(max(c.r,c.g),max(c.b,.00001));
if (NightVision > 0 && maximum < 1) c = lerp(c,c / maximum,NightVision);
if (Ambient <= 0.00001) c -= Darkness;
c = saturate(c);
maximum = max(max(c.r,c.g),max(c.b,.00001));
float3 bright = c * (1 - pow(1 - maximum,4)) / maximum;
c = lerp(c,bright,Gamma) * .96 + .03;
float3 lightLinearRGB = lerp(c / 12.92, pow((c + .055) / 1.055, 2.4), step(.04045,c));
return lightLinearRGB * pow(saturate(Light.b),2.2);
'''


def wire_vanilla_lighting(unreal, editing, material, pixel_rgb, vertex_node=None, use_vertex=True, vertex_output='RGB'):
    """Connect colour outputs; preserves texture alpha and caller's UV/tint graph."""
    collection = ensure_lighting_collection(unreal)
    def node(cls):
        result = editing.create_material_expression(material, cls, 0, 0)
        if result is None:
            raise RuntimeError('Cannot create generated lighting expression')
        return result
    def wire(source, target, pin='', output=''):
        if not editing.connect_material_expressions(source, output, target, pin):
            raise RuntimeError('Cannot wire generated lighting: ' + pin)
    def parameter(name, default):
        if name == 'BridgeSpecular':
            getter = getattr(editing, 'get_material_property_input_node', None)
            existing = getter(material, unreal.MaterialProperty.MP_SPECULAR) if getter else None
            if isinstance(existing, unreal.MaterialExpressionScalarParameter):
                existing.set_editor_property('parameter_name', name); existing.set_editor_property('default_value', default)
                return existing
        result = node(unreal.MaterialExpressionScalarParameter)
        result.set_editor_property('parameter_name', name); result.set_editor_property('default_value', default)
        return result
    def global_parameter(name):
        result = node(unreal.MaterialExpressionCollectionParameter)
        result.set_editor_property('collection', collection); result.set_editor_property('parameter_name', name)
        return result
    mode = global_parameter('BridgeVanillaMode')
    # Keep the old public scalar in the generated graph without allowing it to
    # override the shared mode and leave a previous OFF setting stuck on return.
    legacy = parameter('BridgeUnlit', 0.0)
    ignored = node(unreal.MaterialExpressionMultiply); ignored.set_editor_property('const_b', 0.0); wire(legacy, ignored, 'A')
    mode_sum = node(unreal.MaterialExpressionAdd); wire(mode, mode_sum, 'A'); wire(ignored, mode_sum, 'B')
    actor = node(unreal.MaterialExpressionVectorParameter)
    actor.set_editor_property('parameter_name', 'BridgeLight'); actor.set_editor_property('default_value', unreal.LinearColor(1, 0, 1, 1))
    # Entity meshes use the same ambient light plus cardinal diffuse shade. Terrain
    # has its own per-corner AO/shade payload and bypasses this uniform actor branch.
    # The actor branch reaches pixel emissive: VertexNormalWS is a vertex-only input.
    normal = node(unreal.MaterialExpressionPixelNormalWS)
    actor_shade = node(unreal.MaterialExpressionCustom)
    actor_shade.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    actor_shade.set_editor_property('inputs', [unreal.CustomInput(input_name='WorldNormal')])
    actor_shade.set_editor_property('code',
        'float3 n = normalize(WorldNormal); float3 a = abs(n); '
        'float shade = (a.y*.6 + a.x*.8 + a.z*(n.z >= 0 ? 1.0 : .5)) / max(a.x+a.y+a.z,.00001); '
        'return float3(1,1,shade);')
    wire(normal, actor_shade, 'WorldNormal')
    shaded_actor = node(unreal.MaterialExpressionMultiply); wire(actor, shaded_actor, 'A'); wire(actor_shade, shaded_actor, 'B')
    vertex = vertex_node or node(unreal.MaterialExpressionVertexColor)
    blend = node(unreal.MaterialExpressionLinearInterpolate)
    wire(shaded_actor, blend, 'A'); wire(vertex, blend, 'B', vertex_output); wire(parameter('BridgeUseVertexLight', float(use_vertex)), blend, 'Alpha')
    lightmap = node(unreal.MaterialExpressionCustom)
    lightmap.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    lightmap.set_editor_property('description', 'Bridge native lightmap v1')
    lightmap.set_editor_property('code', LIGHTMAP_CODE)
    inputs = [('Light', blend), ('SkyFactor', global_parameter('BridgeSkyFactor')), ('BlockFactor', global_parameter('BridgeBlockFactor')),
              ('Ambient', global_parameter('BridgeAmbient')), ('Gamma', global_parameter('BridgeGamma')),
              ('NightVision', global_parameter('BridgeNightVision')), ('Darkness', global_parameter('BridgeDarkness')),
              ('DarkenWorld', global_parameter('BridgeDarkenWorld')), ('SkyColor', global_parameter('BridgeSkyColor')),
              ('AmbientColor', global_parameter('BridgeAmbientColor'))]
    lightmap.set_editor_property('inputs', [unreal.CustomInput(input_name=name) for name, _ in inputs])
    for name, source in inputs:
        wire(source, lightmap, name)
    vanilla = node(unreal.MaterialExpressionMultiply); wire(pixel_rgb, vanilla, 'A'); wire(lightmap, vanilla, 'B')
    zero = node(unreal.MaterialExpressionConstant); zero.set_editor_property('r', 0.0)
    base = node(unreal.MaterialExpressionLinearInterpolate)
    wire(pixel_rgb, base, 'A'); wire(zero, base, 'B'); wire(mode_sum, base, 'Alpha')
    emission = node(unreal.MaterialExpressionLinearInterpolate)
    wire(zero, emission, 'A'); wire(vanilla, emission, 'B'); wire(mode_sum, emission, 'Alpha')
    # Restore UE's ordinary dielectric reflection for ON. OFF has none.
    specular = node(unreal.MaterialExpressionLinearInterpolate)
    wire(parameter('BridgeSpecular', .5), specular, 'A'); wire(zero, specular, 'B'); wire(mode_sum, specular, 'Alpha')
    for expression, prop in ((base, unreal.MaterialProperty.MP_BASE_COLOR),
                             (emission, unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                             (specular, unreal.MaterialProperty.MP_SPECULAR)):
        if not editing.connect_material_property(expression, '', prop):
            raise RuntimeError('Cannot connect generated lighting output')
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    return lightmap


def load_material_helpers(unreal, namespace=None):
    """Resolve next to imported source or UEBridge.uproject even with exec(open())."""
    import pathlib
    import runpy
    source = (namespace or {}).get('__file__')
    path = pathlib.Path(source).with_name('bridge_lighting_materials.py') if source else pathlib.Path(
        unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'bridge_lighting_materials.py'
    if not path.is_file():
        raise RuntimeError('Copy bridge_lighting_materials.py next to UEBridge.uproject first')
    return runpy.run_path(str(path))
