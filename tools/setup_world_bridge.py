"""Run in UE Editor: py "C:/.../setup_world_bridge.py".

Configures the saved, currently open UEBridge test level without replacing it.
Does not import Minecraft assets, delete actors, or change Niagara/Chaos assignments.
"""
import pathlib
import unreal

project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_file_path()))
if project.stem != "UEBridge" or not (project.parent / "Source/UEBridge/BridgeWorld.h").is_file():
    raise RuntimeError("Run this script in the updated UEBridge project only")
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError("Save your open level first, then run this script outside Play")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
receiver_class = unreal.load_class(None, "/Script/UEBridge.BridgeReceiver")
if receiver_class is None:
    raise RuntimeError("Build UEBridge 0.3.0 before running this script")
receivers = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.BridgeReceiver)]
if len(receivers) != 1:
    raise RuntimeError("Your level must contain exactly one BridgeReceiver; found " + str(len(receivers)))
assets = unreal.EditorAssetLibrary
material_path = "/Game/Bridge/M_BridgeBlock"
material = unreal.load_asset(material_path) if assets.does_asset_exist(material_path) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_BridgeBlock", "/Game/Bridge", unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Cannot create block-color material")
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -250, 0)
    expression.set_editor_property("parameter_name", "BlockColor")
    expression.set_editor_property("default_value", unreal.LinearColor(.5, .5, .5, 1))
    unreal.MaterialEditingLibrary.connect_material_property(expression, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not assets.save_loaded_asset(material):
        raise RuntimeError("Cannot save the block-color material")
receiver = receivers[0]
if receiver.get_editor_property("preview_material") is None:
    with unreal.ScopedEditorTransaction("Configure Minecraft world bridge"):
        receiver.set_editor_property("preview_material", material)
    if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
        raise RuntimeError("Cannot save your current level")
unreal.log("World bridge material ready. Press Play, then /uebridge world on and /uebridge video on in Minecraft. Existing materials and VFX assignments were preserved.")
