"""UE 5.7 editor import of the owner's Fab files. Prepared source stays under AssetSource."""
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[1] / "AssetSource/Dev20Ending"
dest = "/Game/AbandonedLocal/Dev20Ending"
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
for filename in ("SM_ExpeditionSidearm.fbx", "S_Dev20Ending.wav"):
    task = unreal.AssetImportTask()
    task.filename = str(root / filename)
    task.destination_path = dest
    task.destination_name = Path(filename).stem
    task.automated = task.replace_existing = True
    task.save = False
    if filename.endswith(".fbx"):
        opts = unreal.FbxImportUI()
        opts.automated_import_should_detect_type = False
        opts.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
        opts.import_materials = opts.import_textures = False
        opts.static_mesh_import_data.combine_meshes = True
        opts.static_mesh_import_data.auto_generate_collision = False
        task.factory = unreal.FbxFactory()
        task.options = opts
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.load_asset(f"{dest}/{task.destination_name}")
    if not task.imported_object_paths or not asset:
        raise RuntimeError(f"Missing {filename}")
    if isinstance(asset, unreal.StaticMesh):
        name = "MI_ServiceMetal"
        surface = unreal.EditorAssetLibrary.load_asset(dest + "/" + name) if unreal.EditorAssetLibrary.does_asset_exist(dest + "/" + name) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, dest, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        edit = unreal.MaterialEditingLibrary
        edit.set_material_instance_parent(surface, unreal.load_asset("/Game/Abandoned/Materials/M_AbandonedSurface"))
        edit.set_material_instance_vector_parameter_value(surface, "DiffuseColor", unreal.LinearColor(.085, .092, .099, 1))
        edit.set_material_instance_scalar_parameter_value(surface, "Roughness", .48)
        edit.set_material_instance_scalar_parameter_value(surface, "Metallic", .68)
        edit.update_material_instance(surface)
        if not unreal.EditorAssetLibrary.save_loaded_asset(surface, False):
            raise RuntimeError("Could not save the equipment material")
        for i in range(len(asset.static_materials)):
            asset.set_material(i, surface)
        unreal.log(f"DEV20_SIDEARM bounds={asset.get_bounds()} triangles={asset.get_num_triangles(0)}")
    else:
        unreal.log(f"DEV20_ENDING_AUDIO duration={asset.duration}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")
