"""Create the bridge's local block-dust material; run outside Play in a saved test level.

The player importer calls this helper. It contains no Minecraft texture or sound data.
"""
import pathlib


def setup_vanilla_effects():
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / "UEBridge.uproject").is_file() or not (project / "Source/UEBridge/BridgeVanillaEffects.h").is_file():
        raise RuntimeError("Use the updated UEBridge 0.8.0 project")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play before setting up vanilla particles")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save your level before setting up vanilla particles")
    receiver_class = getattr(unreal, "BridgeReceiver", None)
    if receiver_class is None:
        raise RuntimeError("Build UEBridge 0.8.0 first")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, receiver_class)]
    if len(receivers) != 1:
        raise RuntimeError("The current saved level must contain exactly one BridgeReceiver")

    assets = unreal.EditorAssetLibrary
    editing = unreal.MaterialEditingLibrary
    name, folder = "M_MinecraftDust_v1", "/Game/Bridge/Minecraft"
    path = folder + "/" + name
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Dust material asset path is occupied by another asset type")
    # Rebuild this generated asset on retries, including after a partially failed graph edit.
    editing.delete_all_material_expressions(material)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.1)

    def node(cls):
        expression = editing.create_material_expression(material, cls, 0, 0)
        if expression is None:
            raise RuntimeError("Cannot create dust material expression")
        return expression

    def wire(source, target, pin="", output=""):
        if not editing.connect_material_expressions(source, output, target, pin):
            raise RuntimeError("Cannot connect dust material: " + source.get_class().get_name() + ":" + output + " -> " + target.get_class().get_name() + ":" + pin)

    coordinate = node(unreal.MaterialExpressionTextureCoordinate)
    tile_scale = node(unreal.MaterialExpressionConstant2Vector)
    tile_scale.set_editor_property("r", -0.25)
    tile_scale.set_editor_property("g", 0.25)
    tile_uv = node(unreal.MaterialExpressionMultiply)
    wire(coordinate, tile_uv, "A")
    wire(tile_scale, tile_uv, "B")
    offsets = []
    for index in range(2):
        custom = node(unreal.MaterialExpressionPerInstanceCustomData)
        custom.set_editor_property("data_index", index)
        # UE 5.8 does not expose a default_value editor property here. Both
        # instance channels are explicitly supplied by BridgeVanillaEffects.
        offsets.append(custom)
    mirrored_u = node(unreal.MaterialExpressionAdd)
    mirrored_u.set_editor_property("const_b", 0.25)
    wire(offsets[0], mirrored_u, "A")
    offset_uv = node(unreal.MaterialExpressionAppendVector)
    wire(mirrored_u, offset_uv, "A")
    wire(offsets[1], offset_uv, "B")
    # Instance custom data originates in the vertex shader. Explicit interpolation
    # also works on engine builds that reject direct pixel-shader access. Transfer
    # just the instance offsets; retain texture UVs in the pixel stage for sampling.
    interpolated_offsets = node(unreal.MaterialExpressionVertexInterpolator)
    wire(offset_uv, interpolated_offsets)
    final_uv = node(unreal.MaterialExpressionAdd)
    wire(tile_uv, final_uv, "A")
    wire(interpolated_offsets, final_uv, "B")

    sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
    sample.set_editor_property("parameter_name", "ParticleTexture")
    default_texture = unreal.load_asset("/Engine/EngineResources/DefaultTexture.DefaultTexture")
    if default_texture is None:
        raise RuntimeError("Engine default texture is missing")
    sample.set_editor_property("texture", default_texture)
    wire(final_uv, sample, "Coordinates")
    color = node(unreal.MaterialExpressionVectorParameter)
    color.set_editor_property("parameter_name", "ParticleColor")
    color.set_editor_property("default_value", unreal.LinearColor(.6, .6, .6, 1))
    product = node(unreal.MaterialExpressionMultiply)
    wire(sample, product, "A", "RGB")
    wire(color, product, "B")
    if not editing.connect_material_property(product, "", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("Cannot connect dust Base Color")
    if not editing.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError("Cannot connect dust Opacity Mask")
    roughness = node(unreal.MaterialExpressionConstant)
    roughness.set_editor_property("r", 1.0)
    if not editing.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS):
        raise RuntimeError("Cannot connect dust Roughness")
    editing.recompile_material(material)
    texture_parameters = {str(value) for value in editing.get_texture_parameter_names(material)}
    vector_parameters = {str(value) for value in editing.get_vector_parameter_names(material)}
    if "ParticleTexture" not in texture_parameters or "ParticleColor" not in vector_parameters:
        raise RuntimeError("Dust material parameters are incomplete; run setup_vanilla_effects() again")
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError("Cannot save dust material")
    with unreal.ScopedEditorTransaction("Configure Minecraft vanilla particles"):
        receivers[0].set_editor_property("vanilla_particle_material", material)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Cannot save vanilla particle assignment")
    unreal.log("Minecraft dust material assigned and saved; Play then /uebridge status checks texture readiness and generated/registered particle counts")
    return material
