"""Prepare the hidden Weight shell body in UE 5.7 Editor."""
import re
import runpy
from itertools import product
from pathlib import Path

import unreal


ASSET = "/Game/Abandoned/Collision/SM_WeightShell"
config = (Path(unreal.Paths.project_config_dir()) / "DefaultGame.ini").read_text(encoding="utf-8-sig")
section = config.split("[/Script/Abandoned.EntryWeightLayout]", 1)[1].split("\n[", 1)[0]
boxes = []


def vector(line, key):
    match = re.search(rf"{key}=\(X=([^,]+),Y=([^,]+),Z=([^)]+)\)", line)
    if not match:
        raise ValueError(f"Missing {key} in Weight block: {line}")
    return unreal.Vector(*map(float, match.groups()))


# Reuse the project's ASCII FBX writer. Only the simple boxes participate in queries.
geometry = runpy.run_path(str(Path(__file__).with_name("BuildMantleSuit.py")))["Mesh"]()
for line in section.splitlines():
    if line.startswith("+Room=") and "bCollision=False" not in line:
        center, size = vector(line, "Center"), vector(line, "Size")
        corners = [geometry.point((center.x + x * size.x, center.y + y * size.y, center.z + z * size.z),
                                  (1, 1, 1, 1)) for x, y, z in product((-0.5, 0.5), repeat=3)]
        for a, b, c, d in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
                           (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            for triangle in ((a, b, c), (a, c, d)):
                geometry.face([corners[i] for i in triangle], ((0, 0), (1, 0), (1, 1)))
        box = unreal.KBoxElem()
        for prop, value in (("center", center), ("x", size.x), ("y", size.y), ("z", size.z)):
            box.set_editor_property(prop, value)
        boxes.append(box)

if len(boxes) != 11:
    raise ValueError("Check the Weight layout before regenerating the 11-box experiment")
source = Path(unreal.Paths.project_saved_dir()).resolve() / "SM_WeightShell.fbx"
geometry.export_fbx(source)
options = unreal.FbxImportUI()
options.automated_import_should_detect_type = False
options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
options.import_materials = False
options.import_textures = False
options.static_mesh_import_data.auto_generate_collision = False
task = unreal.AssetImportTask()
task.filename = str(source)
task.destination_path = ASSET.rsplit("/", 1)[0]
task.automated = True
task.replace_existing = True
task.options = options
task.factory = unreal.FbxFactory()
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = unreal.EditorAssetLibrary.load_asset(ASSET)
if not mesh or not task.get_objects():
    raise RuntimeError("Failed to import the Weight shell")
body = mesh.get_editor_property("body_setup")
body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
aggregate = unreal.KAggregateGeom()
aggregate.set_editor_property("box_elems", boxes)
body.set_editor_property("agg_geom", aggregate)
if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
    raise RuntimeError("Failed to save shell")
unreal.log(f"WEIGHT_SHELL_READY {len(boxes)} boxes")
