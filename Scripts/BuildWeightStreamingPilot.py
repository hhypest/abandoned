"""Create the A-02 test sublevel with the project's 11-box Weight shell."""
import unreal

level_path = "/Game/Abandoned/Verification/WeightShellPilot"
mesh = unreal.load_asset("/Game/Abandoned/Collision/SM_WeightShell")
if not mesh:
    raise RuntimeError("Weight shell mesh is missing")
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(level_path):
    raise RuntimeError("Pilot map already exists")
if not levels.new_level(level_path, is_partitioned_world=False):
    raise RuntimeError("Could not create pilot sublevel")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
shell = actors.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
if not isinstance(shell, unreal.StaticMeshActor):
    raise RuntimeError("Could not place the Weight shell")
shell.set_actor_label("Weight shell")
shell.static_mesh_component.set_collision_profile_name("BlockAll")
if not levels.save_current_level():
    raise RuntimeError("Could not save pilot sublevel")
unreal.log("Saved WeightShellPilot")
