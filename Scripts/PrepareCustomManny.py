"""Import the extracted cpawn2.zip from Fab and bind its four textures for the local fit."""
import hashlib
from pathlib import Path

import unreal

source = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "AssetSource/CustomMannyV2/Cpawn2"
fbx = source / "CustomPawn.fbx"
if hashlib.sha256(fbx.read_bytes()).hexdigest() != "6d3615cec48ab492dfae13be8a03c18240403076e92e24f34ef684ccf4f3a07f":
    raise ValueError("Unexpected CustomPawn FBX")
folder = "/Game/AbandonedLocal/CustomMannyV2"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
materials = unreal.MaterialEditingLibrary
textures = ("D", "E", "N", "ORM")
files = [fbx] + [source / f"T_Custom_{suffix}.png" for suffix in textures]
for file in files:
    if assets.does_asset_exist(f"{folder}/{file.stem}"):
        continue
    task = unreal.AssetImportTask()
    task.filename = str(file)
    task.destination_path = folder
    task.automated = task.save = True
    if file == fbx:
        options = unreal.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
        options.import_as_skeletal = True
        options.import_materials = options.import_textures = options.import_animations = False
        options.create_physics_asset = False
        task.options = options
    tools.import_asset_tasks([task])

mesh = assets.load_asset(f"{folder}/CustomPawn")
if not isinstance(mesh, unreal.SkeletalMesh) or len(mesh.materials) != 1:
    raise ValueError("Expected the CustomPawn skeletal mesh with one material slot")
name = "M_CustomPawn"
material = (assets.load_asset(f"{folder}/{name}") if assets.does_asset_exist(f"{folder}/{name}")
            else tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew()))
materials.delete_all_material_expressions(material)
properties = unreal.MaterialProperty
for row, suffix in enumerate(textures):
    texture = assets.load_asset(f"{folder}/T_Custom_{suffix}")
    if not isinstance(texture, unreal.Texture2D):
        raise ValueError(f"Missing texture {suffix}")
    sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    if suffix in ("N", "ORM"):
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP
                                    if suffix == "N" else unreal.TextureCompressionSettings.TC_MASKS)
        sampler = (unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if suffix == "N"
                   else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        assets.save_loaded_asset(texture, False)
    sample = materials.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, row * 240)
    sample.texture = texture
    sample.sampler_type = sampler
    channels = {"D": [("RGB", properties.MP_BASE_COLOR)], "E": [("RGB", properties.MP_EMISSIVE_COLOR)],
                "N": [("RGB", properties.MP_NORMAL)], "ORM": [("R", properties.MP_AMBIENT_OCCLUSION),
                ("G", properties.MP_ROUGHNESS), ("B", properties.MP_METALLIC)]}
    for channel, prop in channels[suffix]:
        if not materials.connect_material_property(sample, channel, prop):
            raise RuntimeError(f"Could not connect {suffix}.{channel}")
materials.recompile_material(material)
slots = mesh.materials
slots[0].material_interface = material
mesh.materials = slots
if not all(assets.save_loaded_asset(asset, False) for asset in (material, mesh)):
    raise RuntimeError("Could not save CustomPawn material or mesh")
unreal.log(f"CUSTOM_MANNY_READY {mesh.get_path_name()} bones={len(mesh.skeleton.get_reference_pose().get_bone_names())}")
