"""Create or refresh the four configured ballast placements in the A-02 pilot."""
import re
from pathlib import Path

import unreal

level_path = "/Game/Abandoned/Verification/WeightShellPilot"
mesh = unreal.load_asset("/Game/Abandoned/Collision/SM_WeightShell")
if not mesh:
    raise RuntimeError("Weight shell mesh is missing")
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(level_path):
    if not levels.load_level(level_path):
        raise RuntimeError("Could not open pilot level")
    shells = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)
              and a.static_mesh_component.static_mesh == mesh]
    if len(shells) != 1:
        raise RuntimeError("Expected one Weight shell in the pilot level")
else:
    if not levels.new_level(level_path, is_partitioned_world=False):
        raise RuntimeError("Could not create pilot sublevel")
    shell = actors.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
    shell.set_actor_label("Weight shell")
    shell.static_mesh_component.set_collision_profile_name("BlockAll")

config = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_config_dir())) / "DefaultGame.ini"
section = config.read_text(encoding="utf-8").split("[/Script/Abandoned.EntryWeightLayout]", 1)[1].split("[", 1)[0]
rows = [line for line in section.splitlines() if line.startswith("+CubeSpawns=")]
if not rows:
    raise RuntimeError("Missing Weight ballast layout")
existing = {a.get_actor_label(): a for a in actors.get_all_level_actors() if isinstance(a, unreal.PuzzleCube)}
for index, line in enumerate(rows):
    match = re.fullmatch(r"\+CubeSpawns=\(Center=\(X=([-.0-9]+),Y=([-.0-9]+),Z=([-.0-9]+)\),Type=(Small|Large),Outlet=\(.*\)\)", line)
    if not match:
        raise RuntimeError(f"Unrecognized ballast placement: {line}")
    position = unreal.Vector(*map(float, match.groups()[:3]))
    label = f"Weight ballast {index + 1}"
    cube = existing.pop(label, None)
    if cube is None:
        cube = actors.spawn_actor_from_class(unreal.PuzzleCube, position)
    cube.set_actor_label(label)
    cube.set_actor_location(position, False, True)
    cube.set_editor_property("placed_type", getattr(unreal.PuzzleCubeType, match[4].upper()))
    code = "01A" if match[4] == "Small" else "01B"
    visual_mesh = unreal.load_asset(f"/Game/Abandoned/Meshy/DEV_{code}/DEV-{code}/StaticMeshes/DEV-{code}")
    if not visual_mesh:
        raise RuntimeError(f"Missing ballast mesh DEV-{code}")
    cube.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(visual_mesh)
    cube.set_editor_property("placed_ballast", True)
if existing:
    raise RuntimeError("Unexpected ballast actors in pilot")
rooms = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.ChamberAssembly)]
if len(rooms) > 1:
    raise RuntimeError("Expected one Weight device assembly")
if not rooms:
    room = actors.spawn_actor_from_class(unreal.ChamberAssembly, unreal.Vector(0, 0, 0))
    room.set_actor_label("Weight devices")
else:
    room = rooms[0]
for prop, code, name in (
    ("plate_base_mesh", "02", "PlateBase"), ("plate_top_mesh", "02", "PlateTop"),
    ("door_leaf_mesh", "13", "DEV-13"), ("door_frame_mesh", "12", "DEV-12"),
    ("switch_mesh", "03", "DEV-03"),
):
    asset = unreal.load_asset(f"/Game/Abandoned/Meshy/DEV_{code}/DEV-{code}/StaticMeshes/{name}")
    if not asset:
        raise RuntimeError(f"Missing Weight device mesh {name}")
    room.set_editor_property(prop, asset)
if not levels.save_current_level():
    raise RuntimeError("Could not save pilot sublevel")
unreal.log(f"Saved WeightShellPilot with {len(rows)} ballast actors")
