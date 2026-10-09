"""Original procedural PBR materials for the UE-only extension.

No Minecraft sprite, downloaded texture or third-party material is sampled.
These graphs approximate sand grains, molten crust and water microstructure;
they do not replace the fluid solver with SPH/FLIP.
"""
REALISTIC_ITEMS = (
    ("uebridge:realistic_sand", "minecraft:sand", "リアリスティック砂", 64),
    ("uebridge:realistic_tnt", "minecraft:tnt", "リアリスティックTNT", 64),
    ("uebridge:realistic_water_bucket", "minecraft:water_bucket", "リアリスティック水入りバケツ", 1),
    ("uebridge:realistic_lava_bucket", "minecraft:lava_bucket", "リアリスティック溶岩入りバケツ", 1),
)

NOISE = """
float hash3(float3 p) { p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z); }
float noise3(float3 p) {
 float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(lerp(hash3(i),hash3(i+float3(1,0,0)),f.x),lerp(hash3(i+float3(0,1,0)),hash3(i+float3(1,1,0)),f.x),f.y),
 lerp(lerp(hash3(i+float3(0,0,1)),hash3(i+float3(1,0,1)),f.x),lerp(hash3(i+float3(0,1,1)),hash3(i+float3(1,1,1)),f.x),f.y),f.z);
}
"""


def _surface_code(kind):
    # Local struct functions are supported inside UE Custom expressions.
    pre = "struct Noise {" + NOISE + "}; Noise n;\n"
    if kind == 'Sand':
        return pre + "float v=n.noise3(P*1.9);float grit=pow(n.hash3(floor(P*4)),14);return float4(lerp(float3(.22,.14,.065),float3(.64,.48,.25),v)+grit*.12,1);"
    if kind == 'Tnt':
        return pre + "float3 q=frac(P*.01);float band=step(.37,q.z)*step(q.z,.63);float v=n.noise3(P*.7);return float4(lerp(float3(.35,.008,.004)*( .8+v*.2),float3(.07,.06,.05),band),1);"
    if kind == 'Rock':
        return pre + "float v=n.noise3(P*.25);return float4(lerp(float3(.009,.007,.01),float3(.055,.038,.06),v*v),1);"
    if kind == 'Water':
        return pre + "float fresnel=pow(1-saturate(abs(V.z)),4);return float4(lerp(float3(.018,.065,.085),float3(.13,.23,.27),fresnel),.2+fresnel*.65);"
    if kind == 'Lava':
        return pre + "float3 q=P*.035+float3(Clock*.025,-Clock*.012,0);float f=n.noise3(q)+n.noise3(q*2.7)*.32;float crust=smoothstep(.60,.83,f);float3 hot=lerp(float3(1.8,.06,.003),float3(2.2,.6,.03),saturate(f));return float4(lerp(hot,float3(.014,.009,.006),crust),1);"
    if kind == 'Fire':
        return pre + "float d=n.noise3(P*.022+float3(0,0,-Clock*2));float wisps=n.noise3(P*.061+float3(Clock*.3,0,-Clock*4));return float4(lerp(float3(.8,.018,.001),float3(5,1.9,.12),d),saturate((d*wisps-.15)*2));"
    return pre + "float d=n.noise3(P*.015+float3(Clock*.12,0,-Clock*.45));float f=n.noise3(P*.042+float3(0,Clock*.16,-Clock));return float4(lerp(float3(.017,.014,.012),float3(.13,.12,.105),d),smoothstep(.16,.70,d*f)*.55);"


def setup_realistic_niagara(unreal):
    """Use an installed engine explosion template; never substitute an unrelated system."""
    destination='/Game/Bridge/Realistic/NS_RealisticExplosion'
    assets=unreal.EditorAssetLibrary
    if assets.does_asset_exist(destination):
        system=unreal.load_asset(destination)
        if not isinstance(system,unreal.NiagaraSystem):
            raise RuntimeError('Realistic explosion path is occupied by a non-Niagara asset')
        return system
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    candidates=[]
    for asset in registry.get_assets_by_path('/NiagaraFluids',recursive=True):
        name=str(asset.asset_name).lower()
        if name in ('grid3d_gas_explosion','grid3d_gas_explosion_cine'):
            candidates.append(asset)
    candidates.sort(key=lambda asset: str(asset.asset_name))
    for candidate in candidates:
        source=candidate.get_asset()
        if not isinstance(source,unreal.NiagaraSystem): continue
        system=assets.duplicate_asset(source.get_path_name(),destination)
        if system is not None and assets.save_loaded_asset(system,False):
            unreal.log('Realistic explosion Niagara template: '+source.get_path_name())
            return system
        raise RuntimeError('Cannot save the local Niagara explosion template')
    unreal.log_warning('Realistic Niagara explosion template unavailable. Procedural smoke/fire fallback active. Install Niagara Fluids or provide /Game/Bridge/Realistic/NS_RealisticExplosion; this fallback is not a volumetric Niagara simulation.')
    return None


def setup_realistic_materials(unreal, sprites):
    # sprites intentionally unused: real items must not use Minecraft textures.
    assets, editing = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
    root = '/Game/Bridge/Realistic'
    assets.make_directory(root)
    created = []
    for kind in ('Sand', 'Tnt', 'Rock', 'Water', 'Lava', 'Fire', 'Smoke'):
        for lit in (False, True):
            name = 'M_Realistic%s_%s_v2' % (kind, 'Lit' if lit else 'Simple')
            path = root + '/' + name
            material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
            if material is None:
                material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
            if not isinstance(material, unreal.Material):
                raise RuntimeError('Cannot create realistic material ' + name)
            if not editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
                editing.delete_all_material_expressions(material)
                translucent = kind in ('Water', 'Fire', 'Smoke')
                material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT if lit and kind not in ('Fire', 'Smoke') else unreal.MaterialShadingModel.MSM_UNLIT)
                material.set_editor_property('two_sided', translucent or kind == 'Lava')
                material.set_editor_property('tangent_space_normal', False)
                material.set_editor_property('used_with_instanced_static_meshes', True)
                material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_OPAQUE)
                if kind == 'Water' and lit:
                    material.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
                    material.set_editor_property('screen_space_reflections', True)
                def node(cls):
                    result = editing.create_material_expression(material, cls, 0, 0)
                    if result is None: raise RuntimeError('Cannot create realistic shader node ' + str(cls))
                    return result
                def wire(source, target, pin=''):
                    if not editing.connect_material_expressions(source, '', target, pin):
                        raise RuntimeError('Realistic shader connection failed: ' + pin)
                def prop(source, target):
                    if not editing.connect_material_property(source, '', target):
                        raise RuntimeError('Realistic material property failed: ' + str(target))
                def scalar(name, value):
                    result = node(unreal.MaterialExpressionScalarParameter)
                    result.set_editor_property('parameter_name', name)
                    result.set_editor_property('default_value', float(value))
                    return result
                def custom(code, output, inputs):
                    result = node(unreal.MaterialExpressionCustom)
                    result.set_editor_property('code', code)
                    result.set_editor_property('output_type', output)
                    params=[]
                    for key, source in inputs:
                        value=unreal.CustomInput();value.set_editor_property('input_name',key);params.append(value)
                    result.set_editor_property('inputs',params)
                    for key,source in inputs: wire(source,result,key)
                    return result
                position=node(unreal.MaterialExpressionWorldPosition)
                clock=scalar('PhysicalClock',0)
                view=node(unreal.MaterialExpressionCameraVectorWS)
                surface=custom(_surface_code(kind),unreal.CustomMaterialOutputType.CMOT_FLOAT4,[('P',position),('Clock',clock),('V',view)])
                rgb=node(unreal.MaterialExpressionComponentMask)
                for key in ('r','g','b','a'): rgb.set_editor_property(key,key!='a')
                wire(surface,rgb)
                alpha=node(unreal.MaterialExpressionComponentMask)
                for key in ('r','g','b','a'): alpha.set_editor_property(key,key=='a')
                wire(surface,alpha)
                zero=scalar('PhysicalZero',0)
                prop(rgb if lit else zero,unreal.MaterialProperty.MP_BASE_COLOR)
                emission=rgb
                if lit and kind in ('Sand','Tnt','Rock','Water'):
                    emission=node(unreal.MaterialExpressionMultiply);wire(rgb,emission,'A');wire(scalar('PhysicalAmbient',.15),emission,'B')
                prop(emission,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
                prop(scalar('PhysicalRoughness',.075 if kind=='Water' else .95 if kind=='Sand' else .72),unreal.MaterialProperty.MP_ROUGHNESS)
                prop(scalar('PhysicalSpecular',.65 if kind=='Water' else .12),unreal.MaterialProperty.MP_SPECULAR)
                if translucent:
                    opacity=node(unreal.MaterialExpressionMultiply);wire(alpha,opacity,'A');wire(scalar('PhysicalOpacity',1),opacity,'B')
                    prop(opacity,unreal.MaterialProperty.MP_OPACITY)
                if kind == 'Water':
                    normal=custom('float s=Quality>.5?.13:.055;return normalize(float3(s*(cos(P.x*.025+Clock*2)+.4*cos(P.y*.11-Clock*3)),s*(sin(P.y*.031-Clock*1.5)+.4*sin(P.x*.09+Clock*4)),1));',unreal.CustomMaterialOutputType.CMOT_FLOAT3,[('P',position),('Clock',clock),('Quality',scalar('PhysicalQuality',1))])
                    prop(normal,unreal.MaterialProperty.MP_NORMAL)
                    if lit: prop(scalar('PhysicalRefraction',1.333),unreal.MaterialProperty.MP_REFRACTION)
                editing.recompile_material(material)
            if not assets.save_loaded_asset(material, False):
                raise RuntimeError('Cannot save realistic material ' + name)
            created.append(material)
    setup_realistic_niagara(unreal)
    return created
