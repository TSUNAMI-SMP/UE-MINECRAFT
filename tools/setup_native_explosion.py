"""Create a local emissive sprite master for vanilla TNT, independent of Niagara."""
import pathlib


def setup_native_explosion():
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / "UEBridge.uproject").is_file():
        raise RuntimeError("Run native explosion setup in the updated UEBridge project")
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play before setting up native explosion sprites")
    assets, editing = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
    folder, name = "/Game/Bridge/Native", "M_NativeExplosion_v1"
    path = folder + "/" + name
    material = unreal.load_asset(path)
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Native explosion material path is occupied by another asset type")
    complete = "ExplosionTexture" in {str(value) for value in editing.get_texture_parameter_names(material)} and "ExplosionTint" in {str(value) for value in editing.get_vector_parameter_names(material)} and all(editing.get_material_property_input_node(material, prop) is not None for prop in (unreal.MaterialProperty.MP_EMISSIVE_COLOR, unreal.MaterialProperty.MP_OPACITY))
    if not complete:
        editing.delete_all_material_expressions(material)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        material.set_editor_property("two_sided", True)
        def node(cls):
            result = editing.create_material_expression(material, cls, 0, 0)
            if result is None:
                raise RuntimeError("Cannot create native explosion material expression")
            return result
        sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
        sample.set_editor_property("parameter_name", "ExplosionTexture")
        default = unreal.load_asset("/Engine/EngineResources/DefaultTexture.DefaultTexture")
        if default is None:
            raise RuntimeError("The engine default texture is unavailable")
        sample.set_editor_property("texture", default)
        tint = node(unreal.MaterialExpressionVectorParameter)
        tint.set_editor_property("parameter_name", "ExplosionTint")
        tint.set_editor_property("default_value", unreal.LinearColor(1,1,1,1))
        color, alpha = node(unreal.MaterialExpressionMultiply), node(unreal.MaterialExpressionMultiply)
        for source, output, target, pin in ((sample,"RGB",color,"A"),(tint,"RGB",color,"B"),(sample,"A",alpha,"A"),(tint,"A",alpha,"B")):
            if not editing.connect_material_expressions(source, output, target, pin):
                raise RuntimeError("Cannot connect native explosion material")
        if not editing.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR) or not editing.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY):
            raise RuntimeError("Cannot connect native explosion color/alpha")
        editing.recompile_material(material)
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError("Cannot save native explosion material")
    unreal.log("Native TNT sprite material ready; local exported vanilla explosion frames are used when Niagara is unset")
    return material
