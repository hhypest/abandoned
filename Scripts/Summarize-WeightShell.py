"""Compare four interleaved pairs of packaged Weight runs and their Insights exports."""
import argparse
import csv
import json
import math
import runpy
from pathlib import Path

budget = runpy.run_path(str(Path(__file__).with_name("Summarize-WeightBudget.py")))
profile = runpy.run_path(str(Path(__file__).with_name("Summarize-VulkanProfile.py")))
gc = budget["gc"]
SHELL = "Training_WeightShellBody"
LOOKUP = "/Game/Abandoned/Collision/SM_WeightShell.SM_WeightShell"
SOLID = "Training_SolidRoomBlock"
PHYSICS = "UActorComponent::CreatePhysicsState"
CULL = "UWorld::UpdateCullDistanceVolumes"
UPDATE = "UWorld::UpdateWorldComponents"
ORDER = ("A1", "B1", "B2", "A2", "B3", "A3", "A4", "B4")


def p95(values):
    return sorted(values)[math.ceil(len(values) * .95) - 1]


def report(run_id, folder, export):
    meta = json.loads((folder / "Run.json").read_text(encoding="utf-8-sig"))
    compound = run_id.startswith("B")
    mode = "Compound" if compound else "Separate"
    if meta["WeightShell"] != mode or meta["ChamberDecor"] != "Instanced":
        raise ValueError(f"Wrong configuration in {run_id}")
    mem = profile["summarize"](folder)
    frames = profile["read_frames"](folder / "Saved/Profiling/CSV" / meta["Profile"])
    commands = [i for i, f in enumerate(frames) if "CsvExecCommand : open /Engine/Maps/Entry?Chamber=Weight" in f["EVENTS"]]
    visits = list(budget["weight_rows"](folder, export))
    if len(commands) != 16 or len(visits) != 16:
        raise ValueError(f"Expected first visit and 15 repeats in {run_id}")
    events = gc["read_trace"](export, {gc["ACTORS"], UPDATE, CULL, SHELL, LOOKUP, SOLID, "BoxComponent", PHYSICS})
    rows = []
    for pos, visit in zip(commands, visits):
        n = visit["Visit"]
        actors = events[gc["ACTORS"]][2 * n - 1]
        if actors.ms != visit["ActorsMs"]:
            raise ValueError(f"Wrong arrival in {run_id}/{n}")
        def inside(parent, timer):
            return [e for e in events[timer] if parent.contains(e)]

        update = gc["one"]([e for e in inside(actors, UPDATE) if e.depth == actors.depth + 1], UPDATE)
        cull = gc["one"]([e for e in inside(update, CULL) if e.depth == update.depth + 1], CULL)
        solid = inside(actors, SOLID)
        if len(solid) != 11 or visit["DecorativeBlocksCount"] != 67:
            raise ValueError("Wrong Weight geometry")
        body = inside(actors, SHELL)
        lookup = inside(actors, LOOKUP)
        if len(body) != int(compound) or len(lookup) != int(compound):
            raise ValueError("Cooked shell trace differs from launch mode")
        if compound:
            physics = [gc["one"](inside(body[0], PHYSICS), PHYSICS)]
        else:
            physics = [gc["one"]([e for box in inside(s, "BoxComponent") for e in inside(box, PHYSICS)], PHYSICS)
                       for s in solid]
        arrival = gc["one"]([i for i in range(pos, pos + 12) if "GC" in
                              (e.strip() for e in frames[i]["EVENTS"].split(";"))], "CSV arrival")
        following = [f["FrameTime"] for f in frames[arrival + 1:arrival + 121]]
        if len(following) != 120:
            raise ValueError("Incomplete recovery window")
        settled = frames[pos + 120:pos + 420]
        if len(settled) != 300:
            raise ValueError("Incomplete settled window")
        total = sum(e.ms for e in [*solid, *body, *lookup])
        rows.append(dict(Run=run_id, Mode=mode, Visit=n, FrameMs=visit["WindowMaxMs"],
                         ArrivalFrameMs=frames[arrival]["FrameTime"], LoadMapMs=visit["LoadMapMs"],
                         ActorsMs=actors.ms, CullMs=cull.ms,
                         ShellTotalMs=total, SolidLoopMs=visit["SolidBlocksMs"],
                         ShellBodyMs=sum(e.ms for e in body), LookupMs=sum(e.ms for e in lookup),
                         PhysicsCount=len(physics), PhysicsMs=sum(e.ms for e in physics),
                         FirstWorldTickMs=visit["FirstWorldTickMs"], NextFrameMs=following[0],
                         Next12MaxMs=max(following[:12]), Next120MaxMs=max(following),
                         Next120P95Ms=p95(following), PurgedObjects=visit["PurgedObjects"],
                         SettledCpuP95Ms=p95([f["CPU"] for f in settled]),
                         SettledGpuP95Ms=p95([f["GPUTime"] for f in settled]),
                         **{k: visit[k] for k in budget["PHASES"] if k != "LoadMapMs"}))
    process = dict(Run=run_id, Mode=mode, Repeats=15, FrameP95Ms=p95([r["FrameMs"] for r in rows[1:]]),
                   InBudget=sum(r["FrameMs"] <= 16.67 for r in rows[1:]), PeakWorkingSetMiB=mem["peakWorkingSetMiB"],
                   PeakPrivateMiB=mem["peakPrivateMiB"], PeakDedicatedMiB=mem["peakDedicatedMiB"],
                   Samples=mem["memorySamples"], ExecutableSHA256=meta["ExecutableSha256"])
    return rows, process


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({k: round(v, 4) if isinstance(v, float) else v for k, v in row.items()})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, help="A1.path, B1.path, ... and corresponding *.events.csv")
    parser.add_argument("output", type=Path, help="existing directory for visits and per-process peaks")
    args = parser.parse_args()
    visits, processes = [], []
    for run in ORDER:
        folder = Path((args.captures / f"{run}.path").read_text(encoding="utf-8-sig").strip())
        rows, process = report(run, folder, args.captures / f"{run}.events.csv")
        visits.extend(rows)
        processes.append(process)
    if len({p["ExecutableSHA256"] for p in processes}) != 1:
        raise ValueError("Executable changed between runs")
    write_csv(args.output / "A07-WeightShellVisits.csv", visits)
    write_csv(args.output / "A07-WeightShellProcesses.csv", processes)
