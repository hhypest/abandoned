"""Install or upgrade the rigged EQP-00 sleeve for the separate EQP-01 terminal in UE 5.7."""
import hashlib
from pathlib import Path

import unreal


folder = "/Game/Abandoned/Meshy/EQP_00"
target = folder + "/SK_EQP00/SkeletalMeshes/SK_EQP00"
saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
source = saved / "BodyRig/EQP01Mount/SK_EQP00.glb"
assets = unreal.EditorAssetLibrary

expected_hash = "1fa2556398b3666895ca493de9c361e26ec09a5fb11cf1a3e7e6a98a5501711f"
installed = assets.does_asset_exist(target)
upgrade = installed and int(assets.get_tag_values(target)["Triangles"]) == 207919
if source.exists() or not installed or upgrade:
    if hashlib.sha256(source.read_bytes()).hexdigest() != expected_hash:
        raise ValueError("Unexpected working skin. Use RigFirstPersonSuit.py on the approved GLB.")
if upgrade:
    # Replace only the data of LOD 0; the same skeleton, material and texture
    # packages remain referenced by gameplay and preserve their import settings.
    mesh = assets.load_asset(target)
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    if subsystem.import_lod(mesh, 0, str(source)) != 0:
        raise RuntimeError("Could not update EQP-00 LOD 0")
elif not installed:
    pipeline = unreal.load_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline")
    for key in ("recompute_normals", "recompute_tangents", "remove_degenerates"):
        pipeline.common_meshes_properties.set_editor_property(key, False)
    pipeline.mesh_pipeline.set_editor_property("create_physics_asset", False)
    stack = unreal.InterchangePipelineStackOverride()
    stack.override_pipelines = [
        unreal.SoftObjectPath(pipeline.get_path_name()),
        unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline"),
    ]
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.options = stack
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

mesh = unreal.load_asset(target)
data = mesh.get_editor_property("asset_import_data")
# ImportLOD updates the per-LOD filename only. Ordinary editor Reimport uses
# the asset's own source filename and must resolve to the same working GLB.
if not Path(data.get_first_filename()).as_posix().endswith("/BodyRig/EQP01Mount/SK_EQP00.glb"):
    if not source.is_file():
        raise RuntimeError(f"Regenerate the approved EQP-00 working copy at {source}")
    mesh.modify()
    data.scripted_add_filename(str(source), 0, "")
    if not assets.save_loaded_asset(mesh):
        raise RuntimeError("Could not save EQP-00 import provenance")
elif upgrade and not assets.save_loaded_asset(mesh):
    raise RuntimeError("Could not save the updated EQP-00 LOD")
ref = mesh.skeleton.get_reference_pose()
names = {str(name) for name in ref.get_bone_names()}
triangles = int(assets.get_tag_values(target)["Triangles"])
screen = ref.get_ref_bone_pose("terminal_l", unreal.AnimPoseSpaces.WORLD)
maps = [unreal.load_asset(p) for p in assets.list_assets(folder + "/SK_EQP00/Textures")]
material = unreal.load_asset(folder + "/SK_EQP00/Materials/BakedMaterial")
if (triangles != 208112 or len(names) != 49 or "terminal_l" not in names
        or screen.translation.distance(unreal.Vector(48.6, 7.05, 33.6)) > .01
        or len(mesh.materials) != 1 or mesh.materials[0].material_interface != material
        or len(maps) != 3 or not all(isinstance(t, unreal.Texture2D)
                                     and t.blueprint_get_size_x() == 4096
                                     and t.blueprint_get_size_y() == 4096 for t in maps)):
    raise RuntimeError(
        f"EQP-00 import mismatch: {triangles} triangles, {len(names)} bones, {screen}, {maps}")
operation = "upgraded LOD 0" if upgrade else ("validated existing" if installed else "imported and validated")
unreal.log(
    f"EQP00_IMPORT_READY {operation} {target} triangles={triangles} bones={len(names)} maps={len(maps)} glass={screen}")
