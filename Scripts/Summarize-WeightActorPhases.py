"""Break down the game-thread actor scope on a verified alternating Stairs/Weight route."""
import argparse
import csv
from pathlib import Path
import runpy
import sys

budget = runpy.run_path(str(Path(__file__).with_name("Summarize-WeightBudget.py")))
gc = budget["gc"]
UPDATE = "UWorld::UpdateWorldComponents"
CULL = "UWorld::UpdateCullDistanceVolumes"
SOLID = "Training_SolidRoomBlock"
TIMERS = {gc["ACTORS"], "Training_InitGame", UPDATE, CULL, "ForEachObjectOfClass", SOLID,
          "BoxComponent", "StaticMeshComponent", "UActorComponent::CreatePhysicsState",
          "Training_SaveEntryCheckpoint", "Checkpoint_Update", "Checkpoint_Lock"}


def summarize(folder, export):
    events = gc["read_trace"](export, TIMERS)

    def children(parents, timer):
        return [child for parent in parents for child in events[timer]
                if child.depth == parent.depth + 1 and parent.contains(child)]

    def duration(spans):
        return sum(span.ms for span in spans)

    for visit in budget["weight_rows"](folder, export):
        index = visit["Visit"]
        arrival = 2 * index - 1  # The route starts in Stairs, then alternates chambers.
        actors = events[gc["ACTORS"]]
        if arrival >= len(actors) or actors[arrival].ms != visit["ActorsMs"]:
            raise ValueError(f"Unexpected route or actor scope at Weight visit {index}")
        actor = actors[arrival]
        init = gc["one"](children([actor], "Training_InitGame"), "InitGame")
        update = gc["one"](children([actor], UPDATE), "world component update")
        cull = gc["one"](children([update], CULL), "cull distance update")
        enumeration = children([cull], "ForEachObjectOfClass")
        solids = children([init], SOLID)
        boxes = children(solids, "BoxComponent")
        visuals = children(solids, "StaticMeshComponent")
        physics = children(boxes, "UActorComponent::CreatePhysicsState")
        checkpoint = children([init], "Training_SaveEntryCheckpoint")
        lock = children(children(checkpoint, "Checkpoint_Update"), "Checkpoint_Lock")
        if not enumeration or not visuals or not lock or len(solids) != visit["SolidBlocksCount"] or len(solids) < 3:
            raise ValueError(f"Incomplete actor phase export for Weight visit {index}")
        for solid in solids:
            gc["one"]([event for event in physics if solid.contains(event)], "physics state per solid")
        marked = ("SolidBlocksMs", "TotalDecorMs", "CubesMs", "PlatesMs", "CheckpointMs", "LightsMs")
        yield dict(Visit=index, FrameMs=visit["WindowMaxMs"], ActorsMs=actor.ms, UpdateWorldMs=update.ms,
                   CullUpdateMs=cull.ms, EnumerateMs=duration(enumeration), InitGameMs=init.ms,
                   OtherActorMs=actor.ms-init.ms-update.ms, SolidCount=len(solids), SolidMs=duration(solids),
                   BoxMs=duration(boxes), PhysicsStateMs=duration(physics), VisualMs=duration(visuals),
                   Solid1Ms=solids[0].ms, Solid2Ms=solids[1].ms, Solid3Ms=solids[2].ms,
                   Solid3PhysicsMs=duration([event for event in physics if solids[2].contains(event)]),
                   DecorMs=visit["TotalDecorMs"], CubesMs=visit["CubesMs"], PlateMs=visit["PlatesMs"],
                   LightsMs=visit["LightsMs"], CheckpointMs=visit["CheckpointMs"], SlotLockMs=duration(lock),
                   OtherInitMs=init.ms-sum(visit[key] for key in marked))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("events", type=Path)
    args = parser.parse_args()
    try:
        rows = list(summarize(args.run, args.events))
        if not rows:
            raise ValueError("No Weight visits found")
    except (ValueError, OSError, csv.Error) as error:
        parser.exit(1, f"{error}\n")
    writer = csv.DictWriter(sys.stdout, fieldnames=rows[0], lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({k: round(v, 4) if isinstance(v, float) else v for k, v in row.items()})
