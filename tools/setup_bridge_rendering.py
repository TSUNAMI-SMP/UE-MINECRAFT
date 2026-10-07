"""Restore generated UE lighting, migrate vanilla lightmap, preserve user level/assets."""
import pathlib
import unreal

def setup_bridge_rendering():
    project = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    if not (project / 'UEBridge.uproject').is_file():
        raise RuntimeError('Use the updated UEBridge project only')
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None or unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        raise RuntimeError('Stop Play and save your level first')
    receivers = [a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a, unreal.BridgeReceiver)]
    if len(receivers) != 1:
        raise RuntimeError('The saved level needs exactly one BridgeReceiver')
    import runpy
    helper = project / 'bridge_lighting_materials.py'
    if not helper.is_file():
        raise RuntimeError('Copy bridge_lighting_materials.py next to UEBridge.uproject first')
    dust_helper = project / 'setup_vanilla_effects.py'
    if not dust_helper.is_file():
        raise RuntimeError('Copy setup_vanilla_effects.py next to UEBridge.uproject first')
    lighting = runpy.run_path(str(helper))
    dust = runpy.run_path(str(dust_helper))
    lighting['ensure_lighting_collection'](unreal)
    assets, tools, editing = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools(), unreal.MaterialEditingLibrary
    names = ('M_MinecraftModel_Masked_v2', 'M_MinecraftModel_Translucent_v2', 'M_MinecraftFaces_v4', 'M_PlayerSkin_v1', 'M_MinecraftMob_v1', 'M_MinecraftDust_v1')
    for path in assets.list_assets('/Game/Bridge/Minecraft', True, False):
        if path.split('/')[-1].split('.')[0] not in names:
            continue
        material = unreal.load_asset(path)
        if not isinstance(material, unreal.Material):
            continue
        if 'BridgeUseVertexLight' not in {str(v) for v in editing.get_scalar_parameter_names(material)}:
            if path.split('/')[-1].split('.')[0] == 'M_MinecraftDust_v1':
                # Dust carries light in instance channels 2/3/4, not the actor
                # BridgeLight uniform shared by a complete texture group.
                dust['setup_vanilla_effects']()
                continue
            pixel = editing.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
            if pixel is None:
                raise RuntimeError('Generated material lacks a colour graph. Re-import visuals: ' + path)
            if isinstance(pixel, unreal.MaterialExpressionLinearInterpolate):
                # 0.9/0.10 model masters: BaseColor=lerp(texture*tint,0,BridgeUnlit).
                sources = editing.get_inputs_for_material_expression(material, pixel)
                if not sources:
                    raise RuntimeError('Cannot recover generated colour graph: ' + path)
                pixel = sources[0]
            lighting['wire_vanilla_lighting'](unreal, editing, material, pixel,
                use_vertex='Model_' in path and '/Items/' not in path)
            editing.recompile_material(material)
            if not assets.save_loaded_asset(material, False):
                raise RuntimeError('Cannot save generated lighting migration: ' + path)
    path = '/Game/Bridge/Minecraft/M_BlockOutline_v1'
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if material is None:
        material = tools.create_asset('M_BlockOutline_v1', '/Game/Bridge/Minecraft', unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError('Outline asset path is occupied by another asset type')
    editing.delete_all_material_expressions(material)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('two_sided', True)
    black = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -200, 0)
    black.set_editor_property('constant', unreal.LinearColor(0, 0, 0, 1))
    if not editing.connect_material_property(black, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Cannot connect outline emissive')
    editing.recompile_material(material)
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError('Cannot save outline material')
    with unreal.ScopedEditorTransaction('Assign Minecraft native outline'):
        receivers[0].set_editor_property('outline_material', material)
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level():
            raise RuntimeError('Cannot save the current level')
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log('UE-lit ON and native lightmap OFF materials, black native outline and shared lighting environment ready. Play then compare day/night and torch placement.')
