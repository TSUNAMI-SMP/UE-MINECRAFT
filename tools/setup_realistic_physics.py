"""Generate original UE extension shaders from locally exported pack textures.

No water/explosion animation or Minecraft texture is bundled in the download.
This is finite-volume water with a procedural wave/foam shader, not Niagara FLIP.
"""

REALISTIC_ITEMS = (
    ("uebridge:realistic_sand", "minecraft:sand", "リアリスティック砂", 64),
    ("uebridge:realistic_tnt", "minecraft:tnt", "リアリスティックTNT", 64),
    ("uebridge:realistic_water_bucket", "minecraft:water_bucket", "リアリスティック水入りバケツ", 1),
    ("uebridge:realistic_lava_bucket", "minecraft:lava_bucket", "リアリスティック溶岩入りバケツ", 1),
)


def setup_realistic_materials(unreal, sprites):
    assets, editing = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
    root = "/Game/Bridge/Realistic"
    assets.make_directory(root)
    keys = dict(Sand="block/sand", Tnt="block/tnt_side", Rock="block/obsidian", Water="block/water_still", Lava="block/lava_still")
    fallback = dict(Sand=(.68, .54, .32), Tnt=(.6, .04, .02), Rock=(.08, .05, .12), Water=(.02, .14, .27), Lava=(1., .15, .005), Fire=(1., .36, .03), Smoke=(.09, .08, .07))
    created = []
    for kind in fallback:
        for lit in (False, True):
            name = "M_Realistic%s_%s_v1" % (kind, "Lit" if lit else "Simple")
            material = unreal.load_asset(root + "/" + name)
            if material is None:
                material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, root, unreal.Material, unreal.MaterialFactoryNew())
            if not isinstance(material, unreal.Material):
                raise RuntimeError("Cannot create realistic material " + name)
            if not editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
                # A previous failed import can leave an incomplete generated graph.
                editing.delete_all_material_expressions(material)
                material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT if lit else unreal.MaterialShadingModel.MSM_UNLIT)
                material.set_editor_property("two_sided", kind in ("Water", "Lava", "Fire", "Smoke"))
                material.set_editor_property("tangent_space_normal", False)
                material.set_editor_property("used_with_instanced_static_meshes", True)
                if kind in ("Water", "Fire", "Smoke"):
                    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
                if kind == "Water" and lit:
                    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
                    material.set_editor_property("screen_space_reflections", True)
                def node(cls):
                    return editing.create_material_expression(material, cls, 0, 0)
                def wire(source, target, pin=""):
                    if not editing.connect_material_expressions(source, "", target, pin):
                        raise RuntimeError("Realistic shader connection failed: " + pin)
                def prop(source, target):
                    if not editing.connect_material_property(source, "", target):
                        raise RuntimeError("Realistic material property failed: " + str(target))
                def scalar(name, value):
                    result = node(unreal.MaterialExpressionScalarParameter)
                    result.set_editor_property("parameter_name", name)
                    result.set_editor_property("default_value", float(value))
                    return result
                zero = scalar("PhysicalZero", 0)
                color = node(unreal.MaterialExpressionVectorParameter)
                color.set_editor_property("parameter_name", "PhysicalColor")
                color.set_editor_property("default_value", unreal.LinearColor(*fallback[kind], 1))
                texture = sprites.get(keys.get(kind, ""))
                if texture and (kind != "Water" or not lit):
                    sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
                    sample.set_editor_property("parameter_name", "PhysicalTexture")
                    sample.set_editor_property("texture", texture)
                    uv = node(unreal.MaterialExpressionTextureCoordinate)
                    if kind in ("Lava", "Water"):
                        # Active pack's square frames stacked vertically. Shader
                        # clock is driven by the physics/replay clock, not FPS.
                        frames = max(1, texture.blueprint_get_size_y() // max(1, texture.blueprint_get_size_x()))
                        clock = scalar("PhysicalClock", 0)
                        anim = node(unreal.MaterialExpressionCustom)
                        anim.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
                        anim.set_editor_property("code", "return float2(frac(UV.x),(frac(UV.y)+fmod(floor(Clock*8),%d))/%d.0);" % (frames, frames))
                        inputs = []
                        for key in ("UV", "Clock"):
                            value = unreal.CustomInput(); value.set_editor_property("input_name", key); inputs.append(value)
                        anim.set_editor_property("inputs", inputs); wire(uv, anim, "UV"); wire(clock, anim, "Clock"); wire(anim, sample, "UVs")
                    else:
                        wire(uv, sample, "UVs")
                    color = sample
                prop(color if lit else zero, unreal.MaterialProperty.MP_BASE_COLOR)
                emission = color
                if lit and kind not in ("Lava", "Fire", "Smoke"):
                    ambient=node(unreal.MaterialExpressionMultiply);wire(color,ambient,"A");wire(scalar("PhysicalAmbient",.08),ambient,"B");emission=ambient
                if kind == "Fire":
                    gain = node(unreal.MaterialExpressionMultiply); wire(color, gain, "A"); wire(scalar("PhysicalGlow", 8 if lit else 1), gain, "B"); emission = gain
                prop(emission, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
                prop(scalar("PhysicalRoughness", .055 if kind == "Water" and lit else .88), unreal.MaterialProperty.MP_ROUGHNESS)
                prop(scalar("PhysicalSpecular", .9 if kind == "Water" and lit else .15), unreal.MaterialProperty.MP_SPECULAR)
                if kind in ("Water", "Fire", "Smoke"):
                    prop(scalar("PhysicalOpacity", .72 if kind == "Water" else .8), unreal.MaterialProperty.MP_OPACITY)
                if kind == "Water" and lit:
                    position = node(unreal.MaterialExpressionWorldPosition)
                    clock, quality = scalar("PhysicalClock", 0), scalar("PhysicalQuality", 1)
                    normal = node(unreal.MaterialExpressionCustom)
                    normal.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
                    normal.set_editor_property("code", "float s=Quality>.5?.18:0;return normalize(float3(-s*cos(P.x*.013+Clock*2),-s*sin(P.y*.017-Clock*1.5),1));")
                    inputs=[]
                    for key in ("P", "Clock", "Quality"):
                        value=unreal.CustomInput(); value.set_editor_property("input_name",key); inputs.append(value)
                    normal.set_editor_property("inputs",inputs)
                    for key, source in zip(("P", "Clock", "Quality"), (position, clock, quality)): wire(source,normal,key)
                    prop(normal,unreal.MaterialProperty.MP_NORMAL)
                    foam=node(unreal.MaterialExpressionVertexColor)
                    mask=node(unreal.MaterialExpressionComponentMask); mask.set_editor_property("r",True); wire(foam,mask)
                    prop(mask,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
                    prop(scalar("PhysicalRefraction",1.333),unreal.MaterialProperty.MP_REFRACTION)
                editing.recompile_material(material)
            if not assets.save_loaded_asset(material, False):
                raise RuntimeError("Cannot save realistic material " + name)
            created.append(material)
    return created
