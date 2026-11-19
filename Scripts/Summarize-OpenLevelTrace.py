"""Split the UE 5.7.4 Stairs -> Weight travel run using GameThread timing events (seconds)."""
import argparse
import csv
import math
import re
import sys
from collections import defaultdict
from contextlib import nullcontext
from dataclasses import dataclass
from pathlib import Path


UNLOAD = "LoadMap_UnloadWorld"
AUDIO = "LoadMap_AudioFlush"
GC = "LoadMap_FullGC"
PRELOAD = "LoadMap_PreloadWorld"
BEGIN_PLAY = "LoadMap_BeginPlay"
ACTORS = "UWorld::InitializeActorsForPlay"
ROUTE = ["Stairs", "Weight", "Stairs", "Weight"]


@dataclass(frozen=True)
class Span:
    start: float
    end: float
    depth: int

    @property
    def ms(self):
        return (self.end - self.start) * 1000


def read_loads(path):
    urls = []
    loads = []
    actors = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if "LogLoad: LoadMap: " in line:
            if len(urls) != len(loads):
                raise ValueError("A new map load started before the previous load completed")
            url = line.split("LogLoad: LoadMap: ", 1)[1].split("?")
            chambers = [option[8:] for option in url[1:] if option.startswith("Chamber=")]
            if url[0] != "/Engine/Maps/Entry" or len(chambers) != 1:
                raise ValueError(f"Unexpected map or chamber in {line}")
            urls.append(chambers[0])
        match = re.search(r"LogWorld: Bringing up level for play took: ([\d.]+)", line)
        if match:
            if len(actors) != len(urls) - 1 or len(loads) != len(actors):
                raise ValueError("Unexpected actor initialization in Game.log")
            actors.append(float(match[1]) * 1000)
        match = re.search(r"LogLoad: Took ([\d.]+) seconds to LoadMap\(/Engine/Maps/Entry\)", line)
        if match:
            if len(actors) != len(loads) + 1:
                raise ValueError("Map completed without actor initialization")
            loads.append(float(match[1]) * 1000)
    if urls != ROUTE or len(loads) != len(ROUTE):
        raise ValueError("Expected startup in Stairs followed by Weight, Stairs, Weight")
    return loads, actors


def read_events(path):
    names = {UNLOAD, AUDIO, GC, PRELOAD, BEGIN_PLAY, ACTORS}
    events = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if not {"ThreadName", "TimerName", "StartTime", "EndTime", "Depth"}.issubset(reader.fieldnames or []):
            raise ValueError("Missing timing export columns (ThreadName, TimerName, StartTime, EndTime, Depth)")
        for row in reader:
            name = row["TimerName"]
            if row["ThreadName"] != "GameThread" or name not in names:
                continue
            span = Span(float(row["StartTime"]), float(row["EndTime"]), int(row["Depth"]))
            if not all(math.isfinite(t) for t in (span.start, span.end)) or span.end < span.start:
                raise ValueError(f"Invalid {name} interval")
            events[name].append(span)
    for name in names:
        events[name].sort(key=lambda span: span.start)
    return events


def summarize(log, trace):
    loads, actor_times = read_loads(log)
    events = read_events(trace)
    # GC can run independently. Match its unique occurrence between unload and preload.
    for name in (UNLOAD, AUDIO, PRELOAD, ACTORS, BEGIN_PLAY):
        if len(events[name]) != len(ROUTE):
            raise ValueError(f"Expected {len(ROUTE)} GameThread scopes named {name}, got {len(events[name])}")
    rows = []
    last_end = 0
    for i, visit in enumerate(("Startup", "First", "Return", "Repeat")):
        unload, audio, preload, actors, begin = (events[name][i] for name in (UNLOAD, AUDIO, PRELOAD, ACTORS, BEGIN_PLAY))
        if not (unload.start <= audio.start and audio.end <= unload.end and audio.depth > unload.depth):
            raise ValueError(f"{visit}: audio flush is outside the teardown scope")
        if not (last_end <= unload.start and unload.end <= preload.start and
                preload.end <= actors.start and actors.end <= begin.start):
            raise ValueError(f"{visit}: unexpected order or overlapping scopes")
        gcs = [span for span in events[GC] if unload.end <= span.start and span.end <= preload.start]
        if len(gcs) != 1:
            raise ValueError(f"{visit}: expected one synchronous GC between teardown and preload")
        # The UE log starts its actor timer just inside InitializeActorsForPlay. Checking each
        # visit catches mixed log/trace pairs while allowing for timestamp precision and logging.
        if abs(actors.ms - actor_times[i]) > 0.5:
            raise ValueError(f"{visit}: actor times differ by >0.5 ms, check the log/trace pair")
        create = (begin.end - preload.start) * 1000
        other = loads[i] - unload.ms - gcs[0].ms - create
        if other < -0.001:
            raise ValueError(f"{visit}: measured phases exceed the logged LoadMap time")
        last_end = begin.end
        if visit in ("First", "Repeat"):
            rows.append(dict(Visit=visit, LoadMapMs=loads[i], UnloadOtherMs=unload.ms - audio.ms,
                             AudioFlushMs=audio.ms, FullGCMs=gcs[0].ms, CreateWorldMs=create,
                             OtherMs=other, BeginPlayMs=begin.ms))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("timing_events", type=Path)
    parser.add_argument("--output", type=Path, help="Write CSV here (default stdout)")
    args = parser.parse_args()
    try:
        rows = summarize(args.log, args.timing_events)
    except (OSError, ValueError, csv.Error) as exc:
        parser.exit(1, f"{exc}\n")
    output = args.output.open("w", newline="", encoding="utf-8") if args.output else nullcontext(sys.stdout)
    with output as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({name: f"{value:.3f}" if isinstance(value, float) else value for name, value in row.items()})


if __name__ == "__main__":
    main()
