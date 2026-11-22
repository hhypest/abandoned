"""Configure the installed FosTech static meshes, or inspect them with -InspectFosTechNanite.

Use -DisableFosTechNanite in a separate checkout to rebuild the control package.
Run as an Unreal Editor Python commandlet. No source downloads are required.
"""
import csv
from pathlib import Path

import unreal


def gltf_surface():
    """Use UE's glTF shading function by reference; the imported opaque devices share three PBR maps."""
    folder = "/Game/Abandoned/Materials"
    name = "M_FosTechGLTF"
    assets = unreal.EditorAssetLibrary
    edit = unreal.MaterialEditingLibrary
    path = f"{folder}/{name}"
    if assets.does_asset_exist(path):
        return assets.load_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", True)  # MI_Default_Opaque_DS
    body = edit.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, -420, 0)
    function = "/InterchangeAssets/gltf/MaterialBodies/MF_Default_Body.MF_Default_Body"
    body.set_material_function(unreal.load_object(None, function))
    defaults = (("BaseColorTexture", "/InterchangeAssets/gltf/Textures/T_White_srgb.T_White_srgb"),
                ("MetallicRoughnessTexture", "/InterchangeAssets/gltf/Textures/T_White_Linear.T_White_Linear"),
                ("NormalTexture", "/Engine/EngineMaterials/FlatNormal.FlatNormal"))
    for index, (parameter, texture) in enumerate(defaults):
        node = edit.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -800, index * 150)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("texture", unreal.load_object(None, texture))
        if not edit.connect_material_expressions(node, "", body, parameter):
            raise RuntimeError(f"Could not connect glTF {parameter}")
    # The template overrides the function's specular factor default of 0.5.
    specular = edit.create_material_expression(mat, unreal.MaterialExpressionConstant, -800, 460)
    specular.set_editor_property("r", 1.0)
    if not edit.connect_material_expressions(specular, "", body, "SpecularFactor"):
        raise RuntimeError("Could not connect glTF specular factor")
    for channel, prop in (("BaseColor", "MP_BASE_COLOR"), ("Metallic", "MP_METALLIC"), ("Roughness", "MP_ROUGHNESS"),
                          ("Specular", "MP_SPECULAR"), ("Normal", "MP_NORMAL"), ("Occlusion", "MP_AMBIENT_OCCLUSION"),
                          ("EmissiveColor", "MP_EMISSIVE_COLOR"), ("Anisotropy", "MP_ANISOTROPY"), ("Tangent", "MP_TANGENT")):
        if not edit.connect_material_property(body, channel, getattr(unreal.MaterialProperty, prop)):
            raise RuntimeError(f"Could not connect glTF {channel}")
    edit.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NANITE)
    edit.recompile_material(mat)
    if not assets.save_loaded_asset(mat):
        raise RuntimeError(f"Could not save {path}")
    return mat


def configure_materials(mesh):
    edit = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    for slot in mesh.get_editor_property("static_materials"):
        instance = slot.material_interface
        base = instance.get_base_material()
        if base.get_editor_property("used_with_nanite"):
            continue
        if base.get_path_name() == "/Game/Abandoned/Suit/M_FosTechEquipment.M_FosTechEquipment":
            edit.set_material_usage(base, unreal.MaterialUsage.MATUSAGE_NANITE)
            if not assets.save_loaded_asset(base):
                raise RuntimeError(f"Could not save {base.get_path_name()}")
        elif instance.parent.get_path_name() == "/InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque_DS.MI_Default_Opaque_DS":
            # Preserve the switches inherited from the glTF template as well as the imported maps.
            switches = {s: edit.get_material_instance_static_switch_parameter_value(instance, s)
                        for s in edit.get_static_switch_parameter_names(instance)}
            maps = {key: edit.get_material_instance_texture_parameter_value(instance, key)
                    for key in ("BaseColorTexture", "MetallicRoughnessTexture", "NormalTexture")}
            edit.set_material_instance_parent(instance, gltf_surface())
            for key, value in switches.items():
                if edit.get_material_instance_static_switch_parameter_value(instance, key) != value:
                    edit.set_material_instance_static_switch_parameter_value(instance, key, value, b_update_material_instance=False)
            edit.update_material_instance(instance)
            if any(edit.get_material_instance_texture_parameter_value(instance, k) != v for k, v in maps.items()):
                raise RuntimeError(f"glTF textures changed on {instance.get_path_name()}")
            if not assets.save_loaded_asset(instance):
                raise RuntimeError(f"Could not save {instance.get_path_name()}")
        if not instance.get_base_material().get_editor_property("used_with_nanite"):
            raise RuntimeError(f"Missing Nanite material usage on {instance.get_path_name()}")


def configure_mesh(mesh, enabled):
    configure_materials(mesh)  # Both A and B use the same compiled materials.
    settings = mesh.get_editor_property("nanite_settings").copy()
    full_fallback = unreal.NaniteFallbackTarget.PERCENT_TRIANGLES
    if settings.enabled == enabled and (not enabled or
            (settings.explicit_tangents and settings.fallback_target == full_fallback
             and settings.fallback_percent_triangles == 1.0 and settings.fallback_relative_error == 0.0)):
        return
    settings.enabled = enabled
    if enabled:
        # Preserve the tangent basis of the authored normal maps, including the close-up wrist casing.
        settings.explicit_tangents = True
        settings.fallback_target = full_fallback
        settings.fallback_percent_triangles = 1.0
        settings.fallback_relative_error = 0.0
    mesh.set_editor_property("nanite_settings", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mesh):
        raise RuntimeError(f"Could not save Nanite settings for {mesh.get_path_name()}")


def main():
    flags = unreal.SystemLibrary.get_command_line().split()
    inspect_only = "-InspectFosTechNanite" in flags
    enabled = "-DisableFosTechNanite" not in flags
    inventory = Path(__file__).resolve().parents[1] / "Docs" / "AssetInventory.csv"
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    count = 0
    with inventory.open(encoding="utf-8") as file:
        for row in csv.DictReader(file):
            folder = row["unreal_folder"]
            if not folder or row["device"] == "EQP-00":
                continue
            registry.scan_paths_synchronous([folder])
            meshes = sorted(
                (data.get_asset() for data in registry.get_assets_by_path(folder, recursive=True)
                 if str(data.asset_class_path.asset_name) == "StaticMesh"),
                key=lambda mesh: mesh.get_path_name())
            triangles = sum(mesh.get_static_mesh_description(0).get_triangle_count() for mesh in meshes)
            if not meshes or triangles != int(row["ue_lod0_triangles"]):
                raise RuntimeError(f"{row['device']}: source LOD0 triangle count {triangles} differs from inventory")
            for mesh in meshes:
                if not inspect_only:
                    configure_mesh(mesh, enabled)
                path = mesh.get_path_name()
                settings = mesh.get_editor_property("nanite_settings")
                render_triangles = mesh.get_num_triangles(0)
                data = unreal.AssetRegistryHelpers.create_asset_data(mesh)
                # UE emits a nonzero NaniteTriangles tag only for valid Nanite resources with root data.
                built = int(unreal.AssetRegistryHelpers.get_tag_value(data, "NaniteTriangles"))
                if settings.enabled != (built > 0):
                    raise RuntimeError(f"{path}: Nanite enabled={settings.enabled}, built triangles={built}")
                source = mesh.get_static_mesh_description(0).get_triangle_count()
                if render_triangles != source or (settings.enabled and built != source):
                    raise RuntimeError(f"{path}: source={source}, conventional LOD0={render_triangles}, Nanite={built}")
                for slot in mesh.get_editor_property("static_materials"):
                    material = slot.material_interface
                    if not material or not material.get_base_material().get_editor_property("used_with_nanite"):
                        raise RuntimeError(f"{path}: {slot.material_slot_name} has no Nanite-compatible parent material")
                unreal.log(f"FOSTECH_NANITE_MESH {path} enabled={settings.enabled} built={built} "
                           f"render_lod0={render_triangles} explicit_tangents={settings.explicit_tangents}")
                count += 1
    unreal.log(f"FOSTECH_NANITE_READY meshes={count}")


if __name__ == "__main__":
    main()
