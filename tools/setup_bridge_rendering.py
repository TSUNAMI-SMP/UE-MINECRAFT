"""Restore generated UE lighting, migrate vanilla lightmap, preserve user level/assets."""
import pathlib
import re
import unreal

def setup_bridge_rendering(asset_root='/Game/Bridge/Minecraft'):
    if not isinstance(asset_root, str) or not re.fullmatch(r'/Game(?:/[A-Za-z0-9_]+)+', asset_root):
        raise ValueError('Invalid generated rendering asset root')
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
    prefixes = ('M_MinecraftAtlas_', 'M_PlayerSkin_v2_', 'M_MinecraftMob_v2_', 'M_MinecraftDust_v2_')
    dust_updated = False
    migrated = 0
    for path in assets.list_assets(asset_root, True, False):
        name = path.split('/')[-1].split('.')[0]
        if name not in names and not name.startswith(prefixes):
            continue
        material = unreal.load_asset(path)
        if not isinstance(material, unreal.Material):
            continue
        if lighting['LIGHTING_REVISION_PARAMETER'] not in {str(v) for v in editing.get_scalar_parameter_names(material)}:
            if name.startswith('M_MinecraftDust_'):
                # Dust carries light in instance channels 2/3/4, not the actor
                # BridgeLight uniform shared by a complete texture group.
                if not dust_updated:
                    dust['setup_vanilla_effects']()
                    dust_updated = True
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
                use_vertex=('Model_' in name or 'Atlas_' in name) and '/Items/' not in path)
            editing.recompile_material(material)
            if not assets.save_loaded_asset(material, False):
                raise RuntimeError('Cannot save generated lighting migration: ' + path)
            migrated += 1
    lighting['ensure_native_sky_materials'](unreal, editing)
    path = asset_root + '/M_BlockOutline_v1'
    material = unreal.load_asset(path) if assets.does_asset_exist(path) else None
    if material is None:
        material = tools.create_asset('M_BlockOutline_v1', asset_root, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError('Outline asset path is occupied by another asset type')
    editing.delete_all_material_expressions(material)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('two_sided', True)
    black = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -200, 0)
    if black is None:
        raise RuntimeError('Cannot create outline colour expression')
    black.set_editor_property('constant', unreal.LinearColor(0, 0, 0, 1))
    if not editing.connect_material_property(black, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Cannot connect outline emissive')
    editing.recompile_material(material)
    if not assets.save_loaded_asset(material, False):
        raise RuntimeError('Cannot save outline material')
    receiver = receivers[0]
    previous = receiver.get_editor_property('outline_material')
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    try:
        with unreal.ScopedEditorTransaction('Assign Minecraft native outline'):
            receiver.set_editor_property('outline_material', material)
        if not level.save_current_level() or receiver.get_editor_property('outline_material') != material:
            raise RuntimeError('Cannot save/verify the current level with native outline')
    except Exception:
        try:
            receiver.set_editor_property('outline_material', previous)
            if not level.save_current_level():
                getattr(unreal, 'log_warning', unreal.log)('Native outline rollback restored the assignment but could not save the current level.')
        except Exception:
            getattr(unreal, 'log_warning', unreal.log)('Native outline rollback could not restore/save the previous assignment.')
        raise
    # Every generated material and this level was explicitly saved above. Saving
    # all dirty packages here would also save unrelated user assets.
    unreal.log('Bridge rendering ready: lighting revision 2, migrated=' + str(migrated) + ', native OFF sky, black outline. Compare day/night, roof and torch placement.')
