"""Join packaged Stairs -> Weight CSV frames with synchronous game-thread phases."""
import argparse
import csv
import json
import runpy
import sys
from pathlib import Path


gc = runpy.run_path(str(Path(__file__).with_name("Summarize-TravelGC.py")))
read_frames = runpy.run_path(str(Path(__file__).with_name("Summarize-VulkanProfile.py")))["read_frames"]
ROOM_SCOPES = {
    "Training_SolidRoomBlock": "SolidBlocks",
    "Training_DecorativeRoomBlock": "DecorativeBlocks",
    "Training_SpawnPuzzleCube": "Cubes",
    "Training_BuildPressurePlate": "Plates",
}
DECORATION_PHASES = ("SpawnActor", "ConstructObject", "RegisterComponent")
TIMERS = {"FEngineLoop::Tick", gc["GC"], gc["ACTORS"], "UWorld_Tick", "Training_InitGame",
          "Training_SaveEntryCheckpoint", "Training_AddLight", "Training_RegisterDecorInstances",
          *ROOM_SCOPES, *DECORATION_PHASES}
PHASES = ("LoadMapMs", "FullGCMs", "VerifyAssumptionsMs", "ReachabilityMs", "GatherMs", "UnhashMs",
          "BeginDestroyMs", "PurgeMs", "FinishFirstPassMs", "DeferredFinishMs", "DeleteMs", "HashShrinkMs",
          "MemoryTrimMs", "BeforeBroadcastMs", "CacheBroadcastMs", "AfterBroadcastMs")


def weight_rows(folder, timing_csv):
    run = json.loads((folder / "Run.json").read_text(encoding="utf-8-sig"))
    if run["Scenario"] != "Travel":
        raise ValueError("Expected a travel run")
    frames = read_frames(folder / "Saved/Profiling/CSV" / run["Profile"])
    if len(frames) != run["CaptureFrames"]:
        raise ValueError("Incomplete CSV capture")
    commands = [i for i, f in enumerate(frames) if "CsvExecCommand : open /Engine/Maps/Entry?Chamber=Weight" in f["EVENTS"]]
    phases = gc["summarize"](folder / "Game.log", timing_csv, memory_tail="phases", outgoing_redraw="quiet")
    weights = [phase for phase in phases if phase["Chamber"] == "Weight"]
    if len(weights) != len(commands):
        raise ValueError("Weight visit count differs between game log and CSV")
    events = gc["read_trace"](timing_csv, TIMERS)
    actors = events[gc["ACTORS"]]
    have_room_markers = any(events[name] for name in ROOM_SCOPES)
    for visit, (command, phase) in enumerate(zip(commands, weights), 1):
        actor = actors[phase["Arrival"]]
        tick = gc["one"]([e for e in events["FEngineLoop::Tick"] if e.contains(actor)], "arrival tick")
        gc["one"]([e for e in events[gc["GC"]] if tick.contains(e) and e.end <= actor.start], "GC in arrival tick")
        init = gc["one"]([e for e in events["Training_InitGame"] if actor.contains(e)], "InitGame")
        world = gc["one"]([e for e in events["UWorld_Tick"] if tick.contains(e) and actor.end <= e.start],
                          "first world tick")
        window = frames[command:command + 12]
        if len(window) != 12:
            raise ValueError("Incomplete transition window")
        frame = gc["one"]([f for f in window if "GC" in (event.strip() for event in f["EVENTS"].split(";"))],
                          "GC frame in transition")
        row = {"Visit": visit, "ArrivalFrameMs": frame["FrameTime"], "WindowMaxMs": max(f["FrameTime"] for f in window),
               "EngineTickMs": tick.ms, **{key: phase[key] for key in PHASES}, "ActorsMs": actor.ms,
               "InitGameMs": init.ms, "FirstWorldTickMs": world.ms}
        checkpoint = gc["one"]([e for e in events["Training_SaveEntryCheckpoint"] if init.contains(e)], "checkpoint")
        row["CheckpointMs"] = checkpoint.ms
        row["LightsMs"] = sum(e.ms for e in events["Training_AddLight"] if init.contains(e))
        for name, label in ROOM_SCOPES.items():
            spans = [e for e in events[name] if init.contains(e)]
            if have_room_markers and not spans:
                raise ValueError(f"Missing {name} on Weight visit {visit}")
            row[label + "Count"] = len(spans) if have_room_markers else ""
            row[label + "Ms"] = sum(e.ms for e in spans) if have_room_markers else ""
        blocks = [e for e in events["Training_DecorativeRoomBlock"] if init.contains(e)]
        batches = [e for e in events["Training_RegisterDecorInstances"] if init.contains(e)]
        mode = (run.get("ChamberDecor") or "").lower()
        if len(batches) > 1 or (mode and (mode == "instanced") != bool(batches)):
            raise ValueError(f"Unexpected decor registration on Weight visit {visit}")
        for timer in DECORATION_PHASES:
            duration = ""
            if blocks and not batches and run["TraceTravelStats"]:
                candidates = [e for e in events[timer] if init.contains(e)]
                direct = [e for e in candidates if any(b.depth + 1 == e.depth and b.contains(e) for b in blocks)]
                if not direct:
                    raise ValueError(f"Incomplete verbose export: missing {timer} inside decorative blocks")
                duration = sum(e.ms for e in direct)
            row["Decorative" + timer + "Ms"] = duration
        row["DecorBatchMs"] = sum(e.ms for e in batches)
        row["TotalDecorMs"] = row["DecorativeBlocksMs"] + row["DecorBatchMs"] if have_room_markers else ""
        row["PurgedObjects"] = phase["Objects"]
        yield row


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_folder", type=Path)
    parser.add_argument("timing_csv", type=Path)
    args = parser.parse_args()
    try:
        rows = list(weight_rows(args.run_folder, args.timing_csv))
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(1, f"{error}\n")
    writer = csv.DictWriter(sys.stdout, fieldnames=rows[0], lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({key: round(value, 4) if isinstance(value, float) else value for key, value in row.items()})
