"""Run ONCE inside UE's editor: py "C:/.../UE-MINECRAFT/tools/setup_ue_demo.py".

Creates a fresh map, floor, PlayerStart, receiver and block-color material.
Does not create Niagara/Geometry Collection; those still need the documented setup.
UE editor execution remains unverified in cloud (no editor installed here).
"""
import pathlib
import unreal

project_dir = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
if not (project_dir / "UEBridge.uproject").is_file() or not (project_dir / "Source/UEBridge/BridgeProtocol.h").is_file():
    raise RuntimeError("Run only inside the new UEBridge project; other projects are not supported")
if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
    raise RuntimeError("Save or discard your current map changes before running this setup")

MAP = "/Game/Maps/BridgeDemo"
MATERIAL = "/Game/Bridge/M_BridgeBlock"
assets = unreal.EditorAssetLibrary
if assets.does_asset_exist(MAP):
    raise RuntimeError("BridgeDemo already exists; preserved without modification. Use manual setup or another fresh project.")
receiver_class = unreal.load_class(None, "/Script/UEBridge.BridgeReceiver")
game_mode = unreal.load_class(None, "/Script/UEBridge.BridgeGameMode")
cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
if receiver_class is None or game_mode is None or cube is None:
    raise RuntimeError("Required classes/engine cube are missing; compile this C++ project before running setup")

material = unreal.load_asset(MATERIAL) if assets.does_asset_exist(MATERIAL) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_BridgeBlock", "/Game/Bridge", unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Cannot create block-color material")
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -250, 0)
    expression.set_editor_property("parameter_name", "BlockColor")
    expression.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(expression, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    assets.save_loaded_asset(material)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.new_level(MAP):
    raise RuntimeError("Cannot create new BridgeDemo level")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -25))
floor.set_actor_label("BridgeDemo_Floor")
floor.static_mesh_component.set_static_mesh(cube)
floor.set_actor_scale3d(unreal.Vector(20, 20, 0.5))
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 100))
actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, -30, 0))
actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 100))

receiver = actors.spawn_actor_from_class(receiver_class, unreal.Vector(0, 0, 0))
receiver.set_editor_property("preview_material", material)
# DefaultEngine.ini already selects BridgeGameMode; do not edit other project settings.
if not level.save_current_level():
    raise RuntimeError("Could not save the new map")
unreal.log("BridgeDemo created. Open this map and set it as startup map. Niagara and Chaos wall are still required; see docs/UE_SETUP.md.")
