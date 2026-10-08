"""Generated-only material wiring: UE PBR ON, native lightmap emissive OFF.

This helper is also imported by the atlas importer. No level, texture or user
material is deleted. Parameter collection values are per UE world at runtime.
"""

COLLECTION_PATH = '/Game/Bridge/Minecraft/MPC_BridgeLighting_v1'
LIGHTING_REVISION_PARAMETER = 'BridgeLightingRevision_v5'
SCALARS = {'BridgeVanillaMode': 0.0, 'BridgeSkyFactor': 1.0, 'BridgeBlockFactor': 1.5,
           'BridgeAmbient': 0.0, 'BridgeGamma': 0.5, 'BridgeNightVision': 0.0,
           'BridgeDarkness': 0.0, 'BridgeDarkenWorld': 0.0, 'BridgeDiffuseLight1Y': 1.0}


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
// This is display RGB, just like the 16x16 RGBA8 lightmap sampled by terrain.
// Face shade precedes the entity overlay; lightmap multiplication follows it.
// The source lightmap render target is RGBA8, not a float colour multiplier.
return floor(saturate(c)*255.0+.5)/255.0;
'''


DISPLAY_TO_LINEAR = 'float3 c=saturate(Color.rgb); return lerp(c/12.92,pow((c+.055)/1.055,2.4),step(.04045,c));'
LINEAR_TO_DISPLAY = 'float3 c=max(Color.rgb,0); return lerp(c*12.92,1.055*pow(c,1.0/2.4)-.055,step(.0031308,c));'


def _color_transfer(unreal, editing, material, source, code, output=''):
    expression = editing.create_material_expression(material, unreal.MaterialExpressionCustom, 0, 0)
    if expression is None:
        raise RuntimeError('Cannot create native colour transfer')
    expression.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    entry = unreal.CustomInput(); entry.set_editor_property('input_name', 'Color')
    expression.set_editor_property('inputs', [entry]); expression.set_editor_property('code', code)
    if not editing.connect_material_expressions(source, output, expression, 'Color'):
        raise RuntimeError('Cannot connect native colour transfer')
    return expression


def texture_display_rgb(unreal, editing, material, sample, output='RGB'):
    """Undo UE's sRGB sample decode before vanilla's display-space tint product."""
    return _color_transfer(unreal, editing, material, sample, LINEAR_TO_DISPLAY, output)


def wire_vanilla_lighting(unreal, editing, material, pixel_rgb, vertex_node=None, use_vertex=True, vertex_output='', pixel_display=False):
    """Connect colour outputs; preserves texture alpha and caller's UV/tint graph."""
    collection = ensure_lighting_collection(unreal)
    def node(cls):
        result = editing.create_material_expression(material, cls, 0, 0)
        if result is None:
            raise RuntimeError('Cannot create generated lighting expression')
        return result
    def wire(source, target, pin='', output=''):
        if not editing.connect_material_expressions(source, output, target, pin):
            raise RuntimeError('Cannot wire generated lighting: ' + type(source).__name__ + '.' + (output or '<first output>') + ' -> ' + type(target).__name__ + '.' + (pin or '<first input>'))
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
    def custom_input(name):
        # UE5.8 exposes this struct with a zero-argument constructor.
        result = unreal.CustomInput()
        result.set_editor_property('input_name', name)
        return result
    # Minecraft terrain/entity shaders multiply their RGBA8 colours directly;
    # separately decoding both multipliers is not equivalent near dark pixels.
    display_pixel = pixel_rgb if pixel_display else texture_display_rgb(unreal, editing, material, pixel_rgb, '')
    raw_display_pixel=display_pixel
    hurt_color = node(unreal.MaterialExpressionVectorParameter)
    hurt_color.set_editor_property('parameter_name', 'BridgeHurtColor'); hurt_color.set_editor_property('default_value', unreal.LinearColor(1, 0, 0, 1))
    # OverlayTexture's red ARGB value is 0xb2ff0000; entity.fsh mixes
    # red*(1-178/255) + already diffuse-shaded texture*(178/255).
    hurt_alpha = node(unreal.MaterialExpressionMultiply); wire(parameter('BridgeHurt', 0.0), hurt_alpha, 'A'); hurt_alpha.set_editor_property('const_b', 77/255)
    hurt_pixel = node(unreal.MaterialExpressionLinearInterpolate)
    wire(display_pixel, hurt_pixel, 'A'); wire(hurt_color, hurt_pixel, 'B'); wire(hurt_alpha, hurt_pixel, 'Alpha')
    display_pixel = hurt_pixel
    pixel_rgb = _color_transfer(unreal, editing, material, display_pixel, DISPLAY_TO_LINEAR)
    world_mode = global_parameter('BridgeVanillaMode')
    view_mode = parameter('BridgeViewLight', 0.0)
    one = node(unreal.MaterialExpressionConstant); one.set_editor_property('r', 1.0)
    mode = node(unreal.MaterialExpressionLinearInterpolate)
    wire(world_mode, mode, 'A'); wire(one, mode, 'B'); wire(view_mode, mode, 'Alpha')
    # Keep the old public scalar in the generated graph without allowing it to
    # override the shared mode and leave a previous OFF setting stuck on return.
    legacy = parameter('BridgeUnlit', 0.0)
    ignored = node(unreal.MaterialExpressionMultiply); ignored.set_editor_property('const_b', 0.0); wire(legacy, ignored, 'A')
    revision = parameter(LIGHTING_REVISION_PARAMETER, 5.0)
    revision_ignored = node(unreal.MaterialExpressionMultiply); revision_ignored.set_editor_property('const_b', 0.0); wire(revision, revision_ignored, 'A')
    zero_compat = node(unreal.MaterialExpressionAdd); wire(ignored, zero_compat, 'A'); wire(revision_ignored, zero_compat, 'B')
    mode_sum = node(unreal.MaterialExpressionAdd); wire(mode, mode_sum, 'A'); wire(zero_compat, mode_sum, 'B')
    actor = node(unreal.MaterialExpressionVectorParameter)
    actor.set_editor_property('parameter_name', 'BridgeLight'); actor.set_editor_property('default_value', unreal.LinearColor(1, 0, 1, 1))
    # Entity meshes use the same ambient light plus cardinal diffuse shade. Terrain
    # has its own per-corner AO/shade payload and bypasses this uniform actor branch.
    # The actor branch reaches pixel emissive: VertexNormalWS is a vertex-only input.
    normal = node(unreal.MaterialExpressionPixelNormalWS)
    actor_shade = node(unreal.MaterialExpressionCustom)
    actor_shade.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    def view_axis(name, default):
        value=node(unreal.MaterialExpressionVectorParameter)
        value.set_editor_property('parameter_name',name);value.set_editor_property('default_value',unreal.LinearColor(*default))
        return value
    axes=[('ViewForward',view_axis('BridgeViewForward',(1,0,0,0))),
          ('ViewRight',view_axis('BridgeViewRight',(0,1,0,0))),
          ('ViewUp',view_axis('BridgeViewUp',(0,0,1,0))),
          ('UseView',view_mode)]
    actor_shade.set_editor_property('inputs', [custom_input('WorldNormal'),custom_input('Light1Y')]+[custom_input(name) for name,_ in axes])
    actor_shade.set_editor_property('code',
        'float3 viewN=float3(dot(WorldNormal,ViewForward.rgb),dot(WorldNormal,ViewRight.rgb),dot(WorldNormal,ViewUp.rgb)); '
        'float3 normal=lerp(WorldNormal,viewN,saturate(UseView)); '
        'float3 n=normalize(float3(-normal.y,normal.z,normal.x)); '
        'float3 l0=normalize(float3(.2,1,-.7)),l1=normalize(float3(-.2,Light1Y,.7)); '
        'float shade=min(1.0,.4+.6*(max(dot(l0,n),0)+max(dot(l1,n),0))); '
        'return float3(1,1,shade);')
    wire(normal, actor_shade, 'WorldNormal')
    for name,axis in axes: wire(axis,actor_shade,name)
    wire(global_parameter('BridgeDiffuseLight1Y'),actor_shade,'Light1Y')
    shaded_actor = node(unreal.MaterialExpressionMultiply); wire(actor, shaded_actor, 'A'); wire(actor_shade, shaded_actor, 'B')
    # VertexColor exposes RGB/R/G/B/A, not RGBA. An empty output name selects
    # its first (RGB) output for sky light, block light and AO/shade.
    # Item/entity graphs without a terrain payload use the actor branch.
    if use_vertex:
        vertex = vertex_node or node(unreal.MaterialExpressionVertexColor)
        blend = node(unreal.MaterialExpressionLinearInterpolate)
        wire(shaded_actor, blend, 'A'); wire(vertex, blend, 'B', vertex_output); wire(parameter('BridgeUseVertexLight', 1.0), blend, 'Alpha')
    else:
        blend = shaded_actor
    shade=node(unreal.MaterialExpressionComponentMask)
    for channel,enabled in (('r',False),('g',False),('b',True),('a',False)):
        shade.set_editor_property(channel,enabled)
    wire(blend,shade)
    shaded_pixel=node(unreal.MaterialExpressionMultiply);wire(raw_display_pixel,shaded_pixel,'A');wire(shade,shaded_pixel,'B')
    wire(shaded_pixel,hurt_pixel,'A')
    lightmap = node(unreal.MaterialExpressionCustom)
    lightmap.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    lightmap.set_editor_property('description', 'Bridge native lightmap v1')
    lightmap.set_editor_property('code', LIGHTMAP_CODE)
    inputs = [('Light', blend), ('SkyFactor', global_parameter('BridgeSkyFactor')), ('BlockFactor', global_parameter('BridgeBlockFactor')),
              ('Ambient', global_parameter('BridgeAmbient')), ('Gamma', global_parameter('BridgeGamma')),
              ('NightVision', global_parameter('BridgeNightVision')), ('Darkness', global_parameter('BridgeDarkness')),
              ('DarkenWorld', global_parameter('BridgeDarkenWorld')), ('SkyColor', global_parameter('BridgeSkyColor')),
              ('AmbientColor', global_parameter('BridgeAmbientColor'))]
    lightmap.set_editor_property('inputs', [custom_input(name) for name, _ in inputs])
    for name, source in inputs:
        wire(source, lightmap, name)
    display_lit = node(unreal.MaterialExpressionMultiply); wire(display_pixel, display_lit, 'A'); wire(lightmap, display_lit, 'B')
    vanilla = _color_transfer(unreal, editing, material, display_lit, DISPLAY_TO_LINEAR)
    zero = node(unreal.MaterialExpressionConstant); zero.set_editor_property('r', 0.0)
    base = node(unreal.MaterialExpressionLinearInterpolate)
    wire(pixel_rgb, base, 'A'); wire(zero, base, 'B'); wire(mode_sum, base, 'Alpha')
    emission = node(unreal.MaterialExpressionLinearInterpolate)
    ambient = node(unreal.MaterialExpressionMultiply); wire(vanilla, ambient, 'A'); wire(parameter('BridgeLitAmbient', .45), ambient, 'B')
    wire(ambient, emission, 'A'); wire(vanilla, emission, 'B'); wire(mode_sum, emission, 'Alpha')
    # Vanilla textures have no PBR reflection. Keep the optional parameter at zero.
    specular = node(unreal.MaterialExpressionLinearInterpolate)
    wire(parameter('BridgeSpecular', 0.0), specular, 'A'); wire(zero, specular, 'B'); wire(mode_sum, specular, 'Alpha')
    for expression, prop in ((base, unreal.MaterialProperty.MP_BASE_COLOR),
                             (emission, unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                             (specular, unreal.MaterialProperty.MP_SPECULAR)):
        if not editing.connect_material_property(expression, '', prop):
            raise RuntimeError('Cannot connect generated lighting output')
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    return lightmap


def ensure_native_sky_materials(unreal, editing=None):
    """Only generated NativeSky assets; vanilla mode needs no lit atmosphere."""
    assets = unreal.EditorAssetLibrary
    editing = editing or unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    default_texture = unreal.load_asset('/Engine/EngineResources/DefaultTexture.DefaultTexture')
    if default_texture is None:
        raise RuntimeError('The engine default texture is unavailable for native celestial materials')
    result = {}
    for name, celestial in (('M_NativeSky_v1', False), ('M_NativeCelestial_v1', True)):
        path = '/Game/Bridge/Minecraft/' + name
        material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
        if material is None:
            material = tools.create_asset(name, '/Game/Bridge/Minecraft', unreal.Material, unreal.MaterialFactoryNew())
        if not isinstance(material, unreal.Material):
            raise RuntimeError('Generated native sky path is occupied by another asset: ' + path)
        editing.delete_all_material_expressions(material)
        material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        material.set_editor_property('two_sided', True)
        # RenderPipelines.POSITION_TEX_COLOR_CELESTIAL uses OVERLAY (SRC_ALPHA,
        # ONE), including fully opaque black around the vanilla sun. Ordinary
        # alpha blending replaces the sky with that black rectangle.
        material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE if celestial else unreal.BlendMode.BLEND_OPAQUE)
        if celestial:
            sample = editing.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -300, 0)
            sample.set_editor_property('parameter_name', 'CelestialTexture')
            # A default texture is necessary to compile a texture parameter. The
            # active resource-pack sun/moon replaces it at runtime via the UI palette.
            sample.set_editor_property('texture', default_texture)
            uv = editing.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -600, 0)
            scale = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 100)
            scale.set_editor_property('parameter_name', 'CelestialUVScale'); scale.set_editor_property('default_value', unreal.LinearColor(1, 1, 0, 0))
            multiply = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -450, 0)
            offset = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 200)
            offset.set_editor_property('parameter_name', 'CelestialUVOffset'); offset.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 0))
            add = editing.create_material_expression(material, unreal.MaterialExpressionAdd, -350, 0)
            # VectorParameter has RGB/R/G/B/A outputs, not a combined RG pin.
            # Explicit masks retain the two UV channels before multiply/add.
            scale_uv = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -500, 100)
            offset_uv = editing.create_material_expression(material, unreal.MaterialExpressionComponentMask, -500, 200)
            for mask in (scale_uv, offset_uv):
                if mask is None:
                    raise RuntimeError('Cannot create native celestial UV component mask')
                for channel, enabled in (('r', True), ('g', True), ('b', False), ('a', False)):
                    mask.set_editor_property(channel, enabled)
            for source, output, target, pin in ((scale, '', scale_uv, ''), (offset, '', offset_uv, ''), (uv, '', multiply, 'A'), (scale_uv, '', multiply, 'B'), (multiply, '', add, 'A'), (offset_uv, '', add, 'B'), (add, '', sample, 'UVs')):
                if not editing.connect_material_expressions(source, output, target, pin):
                    if target != sample or not editing.connect_material_expressions(source, output, target, 'Coordinates'):
                        raise RuntimeError('Cannot wire generated celestial UV: ' + pin)
            opacity = editing.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -300, 300)
            opacity.set_editor_property('parameter_name', 'CelestialOpacity'); opacity.set_editor_property('default_value', 1.0)
            alpha = editing.create_material_expression(material, unreal.MaterialExpressionMultiply, -150, 300)
            if not editing.connect_material_expressions(sample, 'A', alpha, 'A') or not editing.connect_material_expressions(opacity, '', alpha, 'B'):
                raise RuntimeError('Cannot wire native celestial weather opacity')
            if not editing.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY):
                raise RuntimeError('Cannot connect native celestial opacity')
            background = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -300, -100)
            background.set_editor_property('parameter_name', 'NativeSkyColor')
            background.set_editor_property('default_value', unreal.LinearColor(.47,.65,1,1))
            # UE adds in linear scene colour, while Minecraft's OVERLAY adds
            # display RGB. Supply the linear difference for the same result.
            source = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -100, 0)
            source.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
            entries = []
            for key in ('TextureColor','Background','Alpha'):
                entry = unreal.CustomInput();entry.set_editor_property('input_name',key);entries.append(entry)
            source.set_editor_property('inputs',entries)
            source.set_editor_property('code',
                'float3 t=max(TextureColor.rgb,0); t=lerp(t*12.92,1.055*pow(t,1.0/2.4)-.055,step(.0031308,t)); '
                'float3 b=saturate(Background.rgb), c=saturate(b+t*Alpha); '
                'float3 bl=lerp(b/12.92,pow((b+.055)/1.055,2.4),step(.04045,b)); '
                'float3 cl=lerp(c/12.92,pow((c+.055)/1.055,2.4),step(.04045,c)); '
                'return max(cl-bl,0)/max(Alpha,.00001);')
            for origin,out,pin in ((sample,'RGB','TextureColor'),(background,'','Background'),(alpha,'','Alpha')):
                if not editing.connect_material_expressions(origin,out,source,pin):
                    raise RuntimeError('Cannot connect celestial display-space blend')
            output = ''
        else:
            # Lightmap SKY_LIGHT_COLOR_VISUAL is white at noon; the background
            # must use the distinct SKY_COLOR_VISUAL exported with the world.
            color = editing.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -300, 0)
            color.set_editor_property('parameter_name', 'NativeSkyColor')
            color.set_editor_property('default_value', unreal.LinearColor(.47, .65, 1, 1))
            conversion = editing.create_material_expression(material, unreal.MaterialExpressionCustom, -150, 0)
            conversion.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
            entry = unreal.CustomInput(); entry.set_editor_property('input_name', 'Color')
            conversion.set_editor_property('inputs', [entry])
            conversion.set_editor_property('code', 'float3 c=saturate(Color.rgb); return lerp(c/12.92,pow((c+.055)/1.055,2.4),step(.04045,c));')
            if not editing.connect_material_expressions(color, '', conversion, 'Color'):
                raise RuntimeError('Cannot wire native sky colour')
            source, output = conversion, ''
        if not editing.connect_material_property(source, output, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
            raise RuntimeError('Cannot connect native sky emissive')
        editing.recompile_material(material)
        if not assets.save_loaded_asset(material, False):
            raise RuntimeError('Cannot save native sky material: ' + path)
        result[name] = material
    # Procedural stars and the 16-segment sunrise fan use the same exported
    # environment attributes as SkyRendering.updateRenderState.
    for name, stars in (('M_NativeStars_v1',True),('M_NativeSunrise_v1',False)):
        path='/Game/Bridge/Minecraft/'+name
        material=unreal.load_asset(path) if assets.does_asset_exist(path) else None
        if material is None:
            material=tools.create_asset(name,'/Game/Bridge/Minecraft',unreal.Material,unreal.MaterialFactoryNew())
        if not isinstance(material,unreal.Material):
            raise RuntimeError('Generated native sky path is occupied by another asset: '+path)
        editing.delete_all_material_expressions(material)
        material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
        material.set_editor_property('two_sided',True)
        material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE if stars else unreal.BlendMode.BLEND_TRANSLUCENT)
        parameter=editing.create_material_expression(material,unreal.MaterialExpressionScalarParameter if stars else unreal.MaterialExpressionVectorParameter,0,0)
        parameter.set_editor_property('parameter_name','StarBrightness' if stars else 'SunriseColor')
        parameter.set_editor_property('default_value',0.0 if stars else unreal.LinearColor(0,0,0,0))
        if stars:
            background=editing.create_material_expression(material,unreal.MaterialExpressionVectorParameter,0,0)
            background.set_editor_property('parameter_name','NativeSkyColor');background.set_editor_property('default_value',unreal.LinearColor(.47,.65,1,1))
            source=editing.create_material_expression(material,unreal.MaterialExpressionCustom,0,0)
            source.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
            entries=[]
            for key in ('Brightness','Background'):
                entry=unreal.CustomInput();entry.set_editor_property('input_name',key);entries.append(entry)
            source.set_editor_property('inputs',entries)
            source.set_editor_property('code','float3 b=saturate(Background.rgb),c=saturate(b+Brightness*Brightness); return lerp(c/12.92,pow((c+.055)/1.055,2.4),step(.04045,c))-lerp(b/12.92,pow((b+.055)/1.055,2.4),step(.04045,b));')
            editing.connect_material_expressions(parameter,'',source,'Brightness');editing.connect_material_expressions(background,'',source,'Background')
            opacity=editing.create_material_expression(material,unreal.MaterialExpressionConstant,0,0);opacity.set_editor_property('r',1.)
        else:
            source=_color_transfer(unreal,editing,material,parameter,DISPLAY_TO_LINEAR)
            vertex=editing.create_material_expression(material,unreal.MaterialExpressionVertexColor,0,0)
            opacity=editing.create_material_expression(material,unreal.MaterialExpressionMultiply,0,0)
            editing.connect_material_expressions(parameter,'A',opacity,'A');editing.connect_material_expressions(vertex,'A',opacity,'B')
        if not editing.connect_material_property(source,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR) or not editing.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY):
            raise RuntimeError('Cannot connect procedural sky output')
        editing.recompile_material(material)
        if not assets.save_loaded_asset(material,False):raise RuntimeError('Cannot save procedural sky: '+path)
        result[name]=material
    return result


def ensure_native_inverse_hud_material(unreal, editing=None):
    """Minecraft CROSSHAIR/GUI_INVERT composition after scene colour output."""
    editing = editing or unreal.MaterialEditingLibrary
    path = '/Game/Bridge/Minecraft/M_NativeInverseHud_v1'
    assets = unreal.EditorAssetLibrary
    default_texture = unreal.load_asset('/Engine/EngineResources/DefaultTexture.DefaultTexture')
    if default_texture is None:
        raise RuntimeError('Default texture unavailable for inverse HUD material')
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'M_NativeInverseHud_v1', '/Game/Bridge/Minecraft', unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError('Generated inverse HUD path is occupied by another asset')
    editing.delete_all_material_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    location=getattr(unreal.BlendableLocation,'BL_SCENE_COLOR_AFTER_TONEMAPPING',None)
    if location is None:
        location=getattr(unreal.BlendableLocation,'BL_AFTER_TONEMAPPING',None)
    if location is None:
        raise RuntimeError('This engine exposes no after-tonemapping blendable location')
    material.set_editor_property('blendable_location', location)
    def node(cls):
        value = editing.create_material_expression(material, cls, 0, 0)
        if value is None:
            raise RuntimeError('Cannot create inverse HUD expression')
        return value
    def wire(source, target, pin, output=''):
        if not editing.connect_material_expressions(source, output, target, pin):
            raise RuntimeError('Cannot connect inverse HUD input: ' + pin)
    scene = node(unreal.MaterialExpressionSceneTexture)
    scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    uv = node(unreal.MaterialExpressionScreenPosition)
    size = node(unreal.MaterialExpressionViewSize)
    custom = node(unreal.MaterialExpressionCustom)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('description', 'Minecraft 1.21.11 inverse crosshair blend')
    connections = [('Scene', scene, 'Color'), ('UV', uv, 'ViewportUV'), ('ViewportSize', size, '')]
    code = 'float3 destination=Scene.rgb; float2 pixel=UV.xy*ViewportSize.xy;\n'
    for slot in range(3):
        texture = node(unreal.MaterialExpressionTextureObjectParameter)
        texture.set_editor_property('parameter_name', 'InverseTexture' + str(slot))
        texture.set_editor_property('texture', default_texture)
        rect = node(unreal.MaterialExpressionVectorParameter)
        rect.set_editor_property('parameter_name', 'InverseRect' + str(slot))
        rect.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 0))
        crop = node(unreal.MaterialExpressionVectorParameter)
        crop.set_editor_property('parameter_name', 'InverseUV' + str(slot))
        crop.set_editor_property('default_value', unreal.LinearColor(0, 0, 1, 1))
        # VectorParameter's first output is RGB, not RGBA. Append its alpha
        # explicitly: the fourth coordinate holds sprite height/UV height.
        rgba = []
        for parameter in (rect, crop):
            append = node(unreal.MaterialExpressionAppendVector)
            wire(parameter, append, 'A', 'RGB'); wire(parameter, append, 'B', 'A')
            rgba.append(append)
        connections.extend([(f'Texture{slot}', texture, ''), (f'Rect{slot}', rgba[0], ''), (f'Crop{slot}', rgba[1], '')])
        code += f'''if(Rect{slot}.z>0 && Rect{slot}.w>0) {{
float2 p=(pixel-Rect{slot}.xy)/Rect{slot}.zw;
if(all(p>=0) && all(p<1)) {{
float4 texel=Texture2DSample(Texture{slot},Texture{slot}Sampler,Crop{slot}.xy+p*Crop{slot}.zw);
float3 source=lerp(texel.rgb*12.92,1.055*pow(max(texel.rgb,0),1.0/2.4)-.055,step(.0031308,texel.rgb));
float3 inverted=source*(1-destination)+destination*(1-source);
destination=lerp(destination,inverted,texel.a);
}} }}\n'''
    custom.set_editor_property('code', code + 'return destination;')
    inputs = []
    for name, source, output in connections:
        entry = unreal.CustomInput(); entry.set_editor_property('input_name', name); inputs.append(entry)
    custom.set_editor_property('inputs', inputs)
    for name, source, output in connections:
        wire(source, custom, name, output)
    if not editing.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Cannot connect inverse HUD output')
    editing.recompile_material(material)
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError('Cannot save inverse HUD material')
    return material


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
