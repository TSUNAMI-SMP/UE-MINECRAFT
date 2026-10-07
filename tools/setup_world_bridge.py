"""Configure the saved current UEBridge level without replacing assets or maps.

UE Python: exec(open("C:/.../setup_world_bridge.py", encoding="utf-8").read())
Native setup may call setup_world_bridge() after validating its complete package.
"""
import pathlib


def setup_world_bridge(create_receiver=True):
    import unreal
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    if not (project / "UEBridge.uproject").is_file() or not (project / "Source/UEBridge/BridgeWorld.h").is_file():
        raise RuntimeError("Run this script in the updated UEBridge project only")
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError("Stop Play before configuring the saved level")
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError("Save the current level first")
    world = editor.get_editor_world()
    if world is None or world.get_path_name().startswith("/Temp/"):
        raise RuntimeError("Save this level to your project before configuring it")
    receiver_class = getattr(unreal, "BridgeReceiver", None)
    if receiver_class is None:
        raise RuntimeError("Build the updated UEBridge before running this script")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, receiver_class)]
    if len(receivers) > 1 or (not receivers and not create_receiver):
        raise RuntimeError("Your saved level must contain exactly one BridgeReceiver; found " + str(len(receivers)))
    assets = unreal.EditorAssetLibrary
    material_path = "/Game/Bridge/M_BridgeBlock_v2"
    material = unreal.load_asset(material_path) if assets.does_asset_exist(material_path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_BridgeBlock_v2", "/Game/Bridge", unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            raise RuntimeError("Cannot create block-color material")
    if not isinstance(material, unreal.Material):
        raise RuntimeError("The block-color asset path is occupied by another asset type")
    editing = unreal.MaterialEditingLibrary
    complete = "BlockColor" in {str(value) for value in editing.get_vector_parameter_names(material)} and editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR) is not None
    if not complete:
        editing.delete_all_material_expressions(material)
        expression = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionVectorParameter, -250, 0)
        if expression is None:
            raise RuntimeError("Cannot create block-color parameter")
        expression.set_editor_property("parameter_name", "BlockColor")
        expression.set_editor_property("default_value", unreal.LinearColor(.5, .5, .5, 1))
        if not unreal.MaterialEditingLibrary.connect_material_property(expression, "", unreal.MaterialProperty.MP_BASE_COLOR):
            raise RuntimeError("Cannot connect block-color material")
        unreal.MaterialEditingLibrary.recompile_material(material)
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError("Cannot save the block-color material")
    spawned = not receivers
    receiver = None
    previous = None
    try:
        with unreal.ScopedEditorTransaction("Configure Minecraft world bridge"):
            receiver = actors.spawn_actor_from_class(receiver_class, unreal.Vector(0, 0, 0)) if spawned else receivers[0]
            if receiver is None:
                raise RuntimeError("Cannot create the BridgeReceiver")
            if spawned:
                receiver.set_actor_label("Minecraft Bridge")
            previous = receiver.get_editor_property("preview_material")
            if previous is None:
                receiver.set_editor_property("preview_material", material)
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
            raise RuntimeError("Cannot save the configured current level")
    except Exception:
        if receiver is not None:
            if spawned:
                actors.destroy_actor(receiver)
            else:
                receiver.set_editor_property("preview_material", previous)
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        raise
    unreal.log("World bridge receiver ready and saved. Existing level, materials, Niagara and Chaos assignments were preserved.")
    return receiver


# exec(open(...).read()) uses the editor's __main__ namespace, while runpy.run_path
# leaves setup explicit so package validation can finish before changing a level.
if __name__ == "__main__":
    setup_world_bridge()
