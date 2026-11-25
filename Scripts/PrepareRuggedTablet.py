"""Prepare the owner's extracted Rugged Tablet 0.1 and its PBR textures for the local fit."""
import hashlib
from pathlib import Path
import unreal

source = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "AssetSource/RuggedTablet"
fbx = source / "Rugged Tablet-fbx.fbx"
if hashlib.sha256(fbx.read_bytes()).hexdigest() != "5b67f8fcec71372c1168e15e98321a6a38a89cf1df5910c5d6839312dacba416":
    raise ValueError("Unexpected Rugged Tablet 0.1 FBX")
folder = "/Game/AbandonedLocal/RuggedTablet"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary


def import_local(path, name, options=None):
    if not assets.does_asset_exist(f"{folder}/{name}"):
        task = unreal.AssetImportTask()
        task.filename = str(path)
        task.destination_path = folder
        task.destination_name = name
        task.options = options
        task.automated = task.save = True
        tools.import_asset_tasks([task])
    return assets.load_asset(f"{folder}/{name}")


unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
options = unreal.FbxImportUI()
options.automated_import_should_detect_type = False
options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
options.import_as_skeletal = options.import_materials = options.import_textures = False
options.static_mesh_import_data.combine_meshes = True
options.static_mesh_import_data.auto_generate_collision = False
mesh = import_local(fbx, "SM_RuggedTablet", options)
if not isinstance(mesh, unreal.StaticMesh):
    raise ValueError("Expected the tablet static mesh")
name = "M_RuggedTablet"
material = (assets.load_asset(f"{folder}/{name}") if assets.does_asset_exist(f"{folder}/{name}")
            else tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew()))
edit.delete_all_material_expressions(material)
props = unreal.MaterialProperty
for i, (suffix, prop) in enumerate((("BaseColor", props.MP_BASE_COLOR), ("Normal", props.MP_NORMAL),
                                    ("Roughness", props.MP_ROUGHNESS), ("Metallic", props.MP_METALLIC),
                                    ("Emissive", props.MP_EMISSIVE_COLOR))):
    tex = import_local(source / "textures" / f"tablet_DefaultMaterial_{suffix}.png", f"T_Tablet_{suffix}")
    if not isinstance(tex, unreal.Texture2D):
        raise ValueError(f"Missing tablet map {suffix}")
    sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    if suffix in ("Normal", "Metallic", "Roughness"):
        normal = suffix == "Normal"
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP if normal else unreal.TextureCompressionSettings.TC_MASKS)
        sampler = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
        assets.save_loaded_asset(tex, False)
    sample = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, i * 220)
    sample.texture = tex
    sample.sampler_type = sampler
    if not edit.connect_material_property(sample, "R" if suffix in ("Metallic", "Roughness") else "RGB", prop):
        raise RuntimeError(f"Could not connect {suffix}")
edit.recompile_material(material)
for slot in range(len(mesh.static_materials)):
    mesh.set_material(slot, material)
if not all(assets.save_loaded_asset(asset, False) for asset in (material, mesh)):
    raise RuntimeError("Could not save the tablet fit")
unreal.log(f"TABLET_READY {mesh.get_path_name()} bounds={mesh.get_bounding_box()} slots={len(mesh.static_materials)}")
