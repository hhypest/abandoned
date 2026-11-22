"""Configure the installed FosTech static meshes, or inspect them with -InspectFosTechNanite.

Use -DisableFosTechNanite in a separate checkout to rebuild the control package.
Run as an Unreal Editor Python commandlet. No source downloads are required.
"""
import csv
from pathlib import Path

import unreal


def configure_mesh(mesh, enabled):
    settings = mesh.get_editor_property("nanite_settings").copy()
    if settings.enabled == enabled and (not enabled or settings.explicit_tangents):
        return
    settings.enabled = enabled
    if enabled:
        # Preserve the tangent basis of the authored normal maps, including the close-up wrist casing.
        settings.explicit_tangents = True
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
                unreal.log(f"FOSTECH_NANITE_MESH {path} enabled={settings.enabled} built={built} "
                           f"render_lod0={render_triangles} explicit_tangents={settings.explicit_tangents}")
                count += 1
    unreal.log(f"FOSTECH_NANITE_READY meshes={count}")


if __name__ == "__main__":
    main()
