"""Author the campaign passage sublevel. Runtime metrics come from VanguardPassageLayout."""
import unreal

path = "/Game/Abandoned/Chambers/L_Passage"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(path):
    if not levels.load_level(path):
        raise RuntimeError("Could not load the passage")
elif not levels.new_level(path, is_partitioned_world=False):
    raise RuntimeError("Could not create the passage")
segments = [actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.VanguardSegment)]
if not segments:
    segment = actors.spawn_actor_from_class(unreal.VanguardSegment, unreal.Vector(0, 0, 0))
    segment.set_actor_label("Vanguard passage")
elif len(segments) != 1:
    raise RuntimeError("Expected a single passage segment")
else:
    segment = segments[0]
if not segment.bake_passage_room():
    raise RuntimeError("Could not bake passage geometry")
if not levels.save_current_level():
    raise RuntimeError("Could not save the passage")
unreal.log("Saved L_Passage with Vanguard segment")
