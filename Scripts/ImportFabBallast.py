"""Run in the UE 5.7 editor after PrepareFabBallast.py. Build both gameplay variants."""
from pathlib import Path

import unreal

source = Path(__file__).resolve().parents[1] / "AssetSource/Fab/Ballast/Prepared"
root = "/Game/Abandoned/Fab/Ballast"
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary


def import_file(filename, folder, options=None):
    name = Path(filename).stem
    path = f"{folder}/{name}"
    if not assets.does_asset_exist(path):
        task = unreal.AssetImportTask()
        task.filename = str(source / filename)
        task.destination_path = folder
        task.destination_name = name
        task.automated = True
        task.save = True
        if options:
            task.options = options
            task.factory = unreal.FbxFactory()
        tools.import_asset_tasks([task])
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError(f"Missing imported {path}")
    return asset


def surface():
    path = root + "/M_Ballast"
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    material = tools.create_asset("M_Ballast", root, unreal.Material, unreal.MaterialFactoryNew())
    diffuse = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -650, -180)
    diffuse.texture = import_file("T_Diffuse.png", root)
    tint = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -650, -400)
    tint.set_editor_property("parameter_name", "PaintTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    paint = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -320, -260)
    edit.connect_material_expressions(diffuse, "RGB", paint, "A")
    edit.connect_material_expressions(tint, "", paint, "B")
    edit.connect_material_property(paint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    normal = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -320, 0)
    normal.texture = import_file("T_Normal.png", root)
    normal.texture.compression_settings = unreal.TextureCompressionSettings.TC_NORMALMAP
    normal.texture.srgb = False
    normal.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    masks = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -320, 310)
    masks.texture = import_file("T_RMS.png", root)
    masks.texture.compression_settings = unreal.TextureCompressionSettings.TC_MASKS
    masks.texture.srgb = False
    masks.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    for texture in (normal.texture, masks.texture):
        assets.save_loaded_asset(texture, False)
    edit.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    for channel, prop in (("R", "MP_ROUGHNESS"), ("G", "MP_METALLIC"), ("B", "MP_SPECULAR")):
        edit.connect_material_property(masks, channel, getattr(unreal.MaterialProperty, prop))
    edit.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE)
    edit.recompile_material(material)
    assets.save_loaded_asset(material, False)
    return material


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    opts = unreal.FbxImportUI()
    opts.automated_import_should_detect_type = False
    opts.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    opts.import_materials = False
    opts.import_textures = False
    opts.static_mesh_import_data.combine_meshes = True
    opts.static_mesh_import_data.auto_generate_collision = False
    material = surface()
    for variant, color in (("Small", (1, 1, 1)), ("Large", (2.4, 0.85, 0.38))):
        folder = f"{root}/{variant}"
        mesh = import_file("SM_Ballast.fbx", folder, opts)
        name = f"MI_Ballast{variant}"
        instance = assets.load_asset(f"{folder}/{name}") if assets.does_asset_exist(f"{folder}/{name}") else tools.create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(instance, material)
        edit.set_material_instance_vector_parameter_value(instance, "PaintTint", unreal.LinearColor(*color, 1))
        edit.update_material_instance(instance)
        assets.save_loaded_asset(instance, False)
        for i in range(len(mesh.static_materials)):
            mesh.set_material(i, instance)
        ns = mesh.get_editor_property("nanite_settings").copy()
        ns.enabled = True
        ns.explicit_tangents = True
        ns.fallback_target = unreal.NaniteFallbackTarget.PERCENT_TRIANGLES
        ns.fallback_percent_triangles = 1.0
        ns.fallback_relative_error = 0.0
        mesh.set_editor_property("nanite_settings", ns)
        assets.save_loaded_asset(mesh, False)
        bounds = mesh.get_bounds().box_extent
        unreal.log(f"FAB_BALLAST {variant} half_extents={bounds} triangles={mesh.get_num_triangles(0)}")
        if min(bounds.x, bounds.y, bounds.z) <= 0 or max(bounds.x, bounds.y, bounds.z) / min(bounds.x, bounds.y, bounds.z) > 1.01:
            raise RuntimeError(f"{variant} visual is not cubic: {bounds}")


main()
