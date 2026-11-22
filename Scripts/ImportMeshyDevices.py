"""Run in UE 5.7 with the original GLB downloads in Saved/MeshySources."""
import csv
import hashlib
import json
import struct
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
assets = unreal.EditorAssetLibrary
pipeline = unreal.load_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline")
common = pipeline.get_editor_property("common_meshes_properties")
for key in ("recompute_normals", "recompute_tangents", "remove_degenerates"):
    common.set_editor_property(key, False)
pipeline.mesh_pipeline.set_editor_property("build_nanite", False)
pipeline.mesh_pipeline.set_editor_property("collision", False)
stack = unreal.InterchangePipelineStackOverride()
stack.override_pipelines = [unreal.SoftObjectPath(pipeline.get_path_name()),
                            unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline")]

inventory = Path(__file__).resolve().parents[1] / "Docs" / "AssetInventory.csv"
with inventory.open(encoding="utf-8") as file:
    devices = list(csv.DictReader(file))
for row in devices:
    code = row["device"]
    if "-MeshyEquipmentOnly" in unreal.SystemLibrary.get_command_line().split() and not code.startswith("EQP-"):
        continue
    source = root / "MeshySources" / f"{code}.glb"
    if hashlib.sha256(source.read_bytes()).hexdigest() != row["glb_sha256"]:
        raise ValueError(f"Unexpected source revision: {source.name}")
    if code == "DEV-02":
        split = root / "MeshySources" / "Assembly" / source.name
        runpy.run_path(str(Path(__file__).with_name("SplitMeshyPlate.py")))["split_plate"](source, split)
        source = split
    elif code == "EQP-01":
        casing = root / "MeshySources" / "Assembly" / source.name
        runpy.run_path(str(Path(__file__).with_name("RemoveWristStrap.py")))["remove_wrist_strap"](source, casing)
        source = casing
    with source.open("rb") as stream:
        stream.seek(12)
        length, _ = struct.unpack("<II", stream.read(8))
        gltf = json.loads(stream.read(length))
    triangles = sum(gltf["accessors"][p["indices"]]["count"] // 3 for m in gltf["meshes"] for p in m["primitives"])
    folder = row["unreal_folder"]
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = folder
    task.options = stack
    task.automated = True
    task.replace_existing = True
    task.save = True
    existing = assets.list_assets(folder)
    # Upgrade existing EQP-01 game packages produced before the strap was removed.
    old_casing = code == "EQP-01" and any(
        isinstance(mesh, unreal.StaticMesh) and mesh.get_num_triangles(0) == int(row["source_triangles"])
        for mesh in (unreal.load_asset(path) for path in existing))
    if not existing or old_casing:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    objects = [unreal.load_asset(path) for path in assets.list_assets(folder)]
    static_meshes = [o for o in objects if isinstance(o, unreal.StaticMesh)]
    if len(static_meshes) != len(gltf["meshes"]):
        raise RuntimeError(f"{code}: unexpected mesh count {len(static_meshes)}")
    count = sum(mesh.get_num_triangles(0) for mesh in static_meshes)
    textures = [o for o in objects if isinstance(o, unreal.Texture2D)]
    if count != triangles or len(textures) != len(gltf["images"]) or not all(m.get_material(0) for m in static_meshes):
        raise RuntimeError(f"{code}: {count}/{triangles} triangles, {len(textures)}/{len(gltf['images'])} textures")
    for mesh in static_meshes:
        unreal.log(f"MESHY_DEVICE {code} {mesh.get_path_name()} {mesh.get_num_triangles(0)} triangles {mesh.get_bounds()}")
