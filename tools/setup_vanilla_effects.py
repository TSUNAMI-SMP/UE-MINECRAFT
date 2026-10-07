"""Create the bridge's local block-dust material; run outside Play in a saved test level.

The player importer calls this helper. It contains no Minecraft texture or sound data.
"""
import pathlib
import hashlib


def setup_vanilla_effects(explosion_system_path=None):
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / "UEBridge.uproject").is_file() or not (project / "Source/UEBridge/BridgeVanillaEffects.h").is_file():
        raise RuntimeError("Use the updated UEBridge project")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play before setting up vanilla particles")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save your level before setting up vanilla particles")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or world.get_path_name().startswith("/Temp/"):
        raise RuntimeError("Save the current level to your project before setting up particles")
    receiver_class = getattr(unreal, "BridgeReceiver", None)
    if receiver_class is None:
        raise RuntimeError("Build the updated UEBridge first")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, receiver_class)]
    if len(receivers) != 1:
        raise RuntimeError("The current saved level must contain exactly one BridgeReceiver")
    explosion = None
    if explosion_system_path is not None:
        explosion = unreal.load_asset(explosion_system_path)
        if not isinstance(explosion, unreal.NiagaraSystem):
            raise RuntimeError("The supplied explosion asset is not a Niagara System: " + str(explosion_system_path))

    import runpy
    lighting_helper = project / "bridge_lighting_materials.py"
    if not lighting_helper.is_file():
        raise RuntimeError("Copy bridge_lighting_materials.py next to UEBridge.uproject first")
    lighting = runpy.run_path(str(lighting_helper))
    assets = unreal.EditorAssetLibrary
    editing = unreal.MaterialEditingLibrary
    revision = hashlib.sha256(b"dust-import-v2\0" + lighting_helper.read_bytes()).hexdigest()[:12]
    name, folder = "M_MinecraftDust_v2_" + revision, "/Game/Bridge/Minecraft"
    path = folder + "/" + name
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Dust material asset path is occupied by another asset type")
    complete = "ParticleTexture" in {str(value) for value in editing.get_texture_parameter_names(material)} and "ParticleColor" in {str(value) for value in editing.get_vector_parameter_names(material)} and all(editing.get_material_property_input_node(material, prop) is not None for prop in (unreal.MaterialProperty.MP_OPACITY_MASK, unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_EMISSIVE_COLOR))
    if complete:
        if not assets.save_loaded_asset(material, False):
            raise RuntimeError("Cannot save dust material")
        _assign_effects(unreal, receivers[0], material, explosion)
        return material
    # This revisioned graph is staged independently of any previously registered
    # dust material. Failed repairs leave the old receiver/material untouched.
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
    # Texture samples expose the first input as UVs, not the C++ field name
    # Coordinates. MaterialEditingLibrary's empty-name convention selects the
    # first input without relying on editor pin labels.
    wire(final_uv, sample)
    color = node(unreal.MaterialExpressionVectorParameter)
    color.set_editor_property("parameter_name", "ParticleColor")
    color.set_editor_property("default_value", unreal.LinearColor(.6, .6, .6, 1))
    product = node(unreal.MaterialExpressionMultiply)
    wire(sample, product, "A", "RGB")
    wire(color, product, "B")
    # Keep the proven UV channels 0/1 intact. Three explicit custom-data channels
    # carry native sky/block/shade per particle instead of one light for the group.
    native_light = []
    for index in range(2, 5):
        custom = node(unreal.MaterialExpressionPerInstanceCustomData)
        custom.set_editor_property("data_index", index)
        native_light.append(custom)
    levels = node(unreal.MaterialExpressionAppendVector)
    wire(native_light[0], levels, "A")
    wire(native_light[1], levels, "B")
    light_rgb = node(unreal.MaterialExpressionAppendVector)
    wire(levels, light_rgb, "A")
    wire(native_light[2], light_rgb, "B")
    interpolated_light = node(unreal.MaterialExpressionVertexInterpolator)
    wire(light_rgb, interpolated_light)
    lighting['wire_vanilla_lighting'](unreal, editing, material, product,
        vertex_node=interpolated_light, use_vertex=True, vertex_output='')
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
    _assign_effects(unreal, receivers[0], material, explosion)
    return material


def _assign_effects(unreal, receiver, material, explosion):
    previous = receiver.get_editor_property("vanilla_particle_material")
    previous_explosion = receiver.get_editor_property("explosion_system")
    try:
        with unreal.ScopedEditorTransaction("Configure Minecraft vanilla particles"):
            receiver.set_editor_property("vanilla_particle_material", material)
            if explosion is not None:
                receiver.set_editor_property("explosion_system", explosion)
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level() or receiver.get_editor_property("vanilla_particle_material") != material:
            raise RuntimeError("Cannot save/verify vanilla particle assignment")
    except Exception:
        receiver.set_editor_property("vanilla_particle_material", previous)
        receiver.set_editor_property("explosion_system", previous_explosion)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        raise
    unreal.log("Minecraft dust material assigned and saved; check particle readiness in the Bridge diagnostics")
    if receiver.get_editor_property("explosion_system") is None:
        getattr(unreal, "log_warning", unreal.log)("Niagara explosion is not assigned. Dust is ready; supply a Niagara System with setup_vanilla_effects('/Game/.../NS_Explosion') for Niagara TNT VFX. Existing VFX assignments were preserved.")
