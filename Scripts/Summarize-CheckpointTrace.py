"""Report the checkpoint and actor timings of one alternating Stairs/Weight run (UE Insights CSV)."""
import argparse
import csv
import json
import math
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


ACTORS = "UWorld::InitializeActorsForPlay"
OPERATIONS = ("Directory", "Lock", "Read", "Encode", "TempOpen", "TempWrite", "Flush",
              "TempClose", "Replace", "Cleanup", "Unlock")
PUBLISH = "Checkpoint_Publish"
WAIT = "Checkpoint_WaitForPublication"
NAMES = {ACTORS, PUBLISH, WAIT, "Training_InitGame", "Training_AddLight", "Training_SaveEntryCheckpoint",
         "Training_ArrivalReady", "Checkpoint_Update", *("Checkpoint_" + name for name in OPERATIONS)}


@dataclass(frozen=True)
class Span:
    start: float
    end: float
    thread: str
    depth: int

    @property
    def ms(self):
        return (self.end - self.start) * 1000

    def contains(self, other):
        return (self.thread == other.thread and self.depth < other.depth
                and self.start <= other.start <= other.end <= self.end)


def read_log(path, visits):
    text = path.read_text(encoding="utf-8-sig")
    urls = re.findall(r"LogLoad: LoadMap: ([^\r\n]+)", text)
    route = []
    for url in urls:
        parts = url.split("?")
        chambers = [option.removeprefix("Chamber=") for option in parts[1:] if option.startswith("Chamber=")]
        if parts[0] != "/Engine/Maps/Entry" or len(chambers) != 1:
            raise ValueError(f"Unexpected URL: {url}")
        route.append(chambers[0])
    ready = re.findall(r"LogTrainingChamber: Display: Training chamber ready\. (\w+),", text)
    saved = re.findall(r"LogCampaignCheckpoint: Display: Saved campaign checkpoint v2: (\w+) ", text)
    actors = [float(value) * 1000 for value in re.findall(r"LogWorld: Bringing up level for play took: ([\d.]+)", text)]
    loads = [float(value) * 1000 for value in re.findall(r"LogLoad: Took ([\d.]+) seconds to LoadMap\(/Engine/Maps/Entry\)", text)]
    expected = ["Stairs", "Weight"] * visits
    if route != expected or ready != route or saved != route:
        raise ValueError("The requested route, ready chambers and published checkpoints must match")
    if len(actors) != len(route) or len(loads) != len(route):
        raise ValueError("Incomplete actor or LoadMap durations")
    return route, actors, loads


def read_events(path):
    events = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        needed = {"ThreadName", "TimerName", "StartTime", "EndTime", "Depth"}
        if not needed.issubset(reader.fieldnames or []):
            raise ValueError(f"Missing timing export columns: {sorted(needed - set(reader.fieldnames or []))}")
        for row in reader:
            name = row["TimerName"]
            if name not in NAMES:
                continue
            span = Span(float(row["StartTime"]), float(row["EndTime"]), row["ThreadName"], int(row["Depth"]))
            if not (math.isfinite(span.start) and math.isfinite(span.end) and 0 <= span.start <= span.end):
                raise ValueError(f"Invalid {name} timing")
            events[name].append(span)
    for spans in events.values():
        spans.sort(key=lambda span: span.start)
    return events


def summarize(log, timing_events, visits):
    run = json.loads((log.parent / "Run.json").read_text(encoding="utf-8-sig"))
    if run["Scenario"] != "Travel" or not re.fullmatch(r"[0-9a-fA-F]{64}", run["ExecutableSha256"]):
        raise ValueError("Expected a packaged travel run with the executable SHA-256")
    route, actor_log, loads = read_log(log, visits)
    events = read_events(timing_events)
    per_visit = (ACTORS, "Training_InitGame", "Training_SaveEntryCheckpoint", "Training_ArrivalReady", "Checkpoint_Update")
    for name in per_visit:
        if len(events[name]) != len(route):
            raise ValueError(f"Expected one {name} per arrival, got {len(events[name])}")
    publishes = events[PUBLISH]
    if publishes and len(publishes) != len(route):
        raise ValueError("Expected one publication per accepted checkpoint")
    rows = []
    for i, chamber in enumerate(route):
        actors, init, save, ready, update = (events[name][i] for name in per_visit)
        if actors.thread != "GameThread" or not (actors.contains(init) and init.contains(save) and init.contains(ready)):
            raise ValueError(f"Arrival {i}: actor setup, save or ready scope is outside its parent")
        if abs(actors.ms - actor_log[i]) > 0.5:
            raise ValueError(f"Arrival {i}: actor timing differs from the log, check the capture pair")
        lights = [light for light in events["Training_AddLight"] if init.contains(light)]
        if not lights or not lights[-1].end <= save.start <= save.end <= ready.start:
            raise ValueError(f"Arrival {i}: lights, save and ready are missing or out of order")
        if not save.contains(update):
            raise ValueError(f"Arrival {i}: checkpoint update is outside SaveEntryCheckpoint")
        # Multiple captures may assign different workers to publication. The writer lock
        # serializes commits, so their start order matches that of the accepted requests.
        publish = publishes[i] if publishes else update
        if publishes and (publish.thread == "GameThread" or publish.start < update.start):
            raise ValueError(f"Arrival {i}: unexpected asynchronous publication")
        next_save = events["Training_SaveEntryCheckpoint"][i + 1].start if i + 1 < len(route) else math.inf
        if publishes and i + 1 < len(publishes) and publish.end > publishes[i + 1].start:
            raise ValueError(f"Arrival {i}: overlapping publication jobs")
        phases = {}
        end = update.start
        for operation in OPERATIONS:
            parent = update if operation in ("Directory", "Lock", "Read", "Encode") else publish
            scopes = [span for span in events["Checkpoint_" + operation] if parent.contains(span)]
            if len(scopes) != 1 or scopes[0].start < end:
                raise ValueError(f"Arrival {i}: expected one ordered {operation} inside its parent")
            phases[operation + "Ms"] = scopes[0].ms
            end = scopes[0].end
        waits = [wait for wait in events[WAIT] if save.start <= wait.start < next_save]
        if any(wait.thread != "GameThread" for wait in waits):
            raise ValueError(f"Arrival {i}: publication wait outside GameThread")
        rows.append(dict(Run=log.parent.name, Profile=run["Profile"], ExecutableSha256=run["ExecutableSha256"],
                         Arrival=i, Chamber=chamber, LoadMapMs=loads[i], ActorsMs=actors.ms,
                         InitGameMs=init.ms, LastLightToReadyMs=(ready.start - lights[-1].end) * 1000,
                         SaveGameThreadMs=save.ms, ReadyMs=ready.ms, UpdateMs=update.ms,
                         PublishThread=publish.thread, WorkerPublishMs=publish.ms if publishes else 0.0,
                         SaveToPublishEndMs=(publish.end - save.start) * 1000,
                         WaitCount=len(waits), GameThreadWaitMs=sum((wait.ms for wait in waits), 0.0),
                         **phases))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_log", type=Path)
    parser.add_argument("timing_events", type=Path)
    parser.add_argument("--weight-visits", type=int, choices=range(2, 21), default=16, metavar="2..20")
    args = parser.parse_args()
    try:
        rows = summarize(args.game_log, args.timing_events, args.weight_visits)
    except (OSError, KeyError, ValueError, csv.Error) as exc:
        parser.exit(1, f"{exc}\n")
    writer = csv.DictWriter(sys.stdout, fieldnames=rows[0], lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({key: f"{value:.4f}" if isinstance(value, float) else value for key, value in row.items()})


if __name__ == "__main__":
    main()
