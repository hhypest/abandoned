"""Pair MemStack audit events from Timing Insights with a packaged Travel run."""
import argparse
from bisect import bisect_left, bisect_right
from collections import defaultdict
import csv
from dataclasses import dataclass
import json
import math
from pathlib import Path
import re
import sys


LOAD = "LoadMap_PageWindow"
GC = "LoadMap_FullGC"
PRELOAD = "LoadMap_PreloadWorld"
ACTORS = "UWorld::InitializeActorsForPlay"
DRAIN = "MemStack_TrimFreePages"
LATE = "MemStack_EndOfLoadMapDrain"
FRAME = "FEngineLoop::Tick"
RELEASE = "MemStackDrain/"
REQUESTS = ("MemStack_RequestHeap", "MemStack_RequestReuse")


@dataclass(frozen=True, order=True)
class Span:
    start: float
    end: float
    depth: int

    def contains(self, other):
        return self.start <= other.start <= other.end <= self.end and self.depth < other.depth

    @property
    def ms(self):
        return (self.end - self.start) * 1000


def only(items, label):
    if len(items) != 1:
        raise ValueError(f"Expected one {label}, found {len(items)}")
    return items[0]


def read_events(path):
    events = defaultdict(list)
    request_threads = defaultdict(set)
    releases = []
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"ThreadName", "TimerName", "StartTime", "EndTime", "Depth"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("Missing Timing Insights columns")
        for row in reader:
            name = row["TimerName"]
            if name in REQUESTS:
                request_threads[name].add(row["ThreadName"])
                continue
            if row["ThreadName"] != "GameThread":
                continue
            if name not in (LOAD, GC, PRELOAD, ACTORS, DRAIN, LATE, FRAME) and not name.startswith(RELEASE):
                continue
            span = Span(float(row["StartTime"]), float(row["EndTime"]), int(row["Depth"]))
            if not (math.isfinite(span.end) and 0 <= span.start <= span.end and span.depth >= 0):
                raise ValueError(f"Invalid {name} event")
            if name.startswith(RELEASE):
                count = int(name.removeprefix(RELEASE))
                if count < 0:
                    raise ValueError("Negative drain count")
                releases.append((span, count))
            else:
                events[name].append(span)
    for series in events.values():
        series.sort()
    return events, request_threads, releases


def count_requests(path, windows):
    # A full Insights export can contain millions of requests outside travel.
    # Retain only the phase totals. Windows are disjoint across all arrivals.
    starts = [w[0] for w in windows]
    if any(a[1] > b[0] for a, b in zip(windows, windows[1:])):
        raise ValueError("Overlapping page count windows")
    with path.open(encoding="utf-8-sig", newline="") as stream:
        for event in csv.DictReader(stream):
            kind = event["TimerName"]
            if kind not in REQUESTS:
                continue
            t = float(event["StartTime"])
            if not math.isfinite(t) or t < 0:
                raise ValueError(f"Invalid {kind} timestamp")
            i = bisect_right(starts, t) - 1
            if i >= 0:
                _, end, phase, row = windows[i]
                if t < end:
                    row[phase + kind.removeprefix("MemStack_Request")] += 1


def summarize(folder, trace):
    config = json.loads((folder / "Run.json").read_text(encoding="utf-8-sig"))
    if config["Scenario"] != "Travel" or config["Allocator"] != "Binned2":
        raise ValueError("Expected Binned2 packaged travel")
    count_pages = config["TraceMapPages"]
    deferred = config["DeferMapPageDrain"]
    events, threads, releases = read_events(trace)
    if count_pages != bool(releases) or any(bool(threads[k]) != count_pages for k in REQUESTS):
        raise ValueError("Page instrumentation differs from the requested capture")
    if count_pages and not (set.union(*threads.values()) - {"GameThread"}):
        raise ValueError("Expected page requests from all CPU threads")
    log = (folder / "Game.log").read_text(encoding="utf-8-sig")
    loads = log.split("LogLoad: LoadMap: ")[1:]
    if len(loads) < 4 or len(loads) % 2 or len(events[LOAD]) != len(loads):
        raise ValueError("Incomplete alternating route or LoadMap export")

    rows = []
    windows = []
    ticks = [s.start for s in events[FRAME]]
    for i, (world, part) in enumerate(zip(events[LOAD], loads)):
        chamber = "Weight" if i % 2 else "Stairs"
        url = part.splitlines()[0].split("?")
        if url[0] != "/Engine/Maps/Entry" or [x for x in url if x.startswith("Chamber=")] != ["Chamber=" + chamber]:
            raise ValueError(f"Unexpected route at arrival {i}")
        if i == 0:
            continue
        def inside(name):
            return [s for s in events[name] if world.contains(s)]
        gc, preload, actors = [only(inside(n), n) for n in (GC, PRELOAD, ACTORS)]
        actor_ms = float(only(re.findall(r"LogWorld: Bringing up level for play took: ([\d.]+)", part),
                              "actor initialization time")) * 1000
        if gc.end > preload.start or preload.end > actors.start or abs(actors.ms - actor_ms) > 0.5:
            raise ValueError(f"Trace and log disagree at arrival {i}")
        drains = inside(DRAIN)
        early = [s for s in drains if gc.contains(s)]
        second = [s for s in drains if gc.end <= s.start <= s.end <= preload.start]
        tail = inside(LATE)
        if deferred:
            end_drain = only(tail, LATE)
            if len(drains) != 1 or not end_drain.contains(drains[0]) or end_drain.start < actors.end:
                raise ValueError(f"Expected a single deferred drain at arrival {i}")
        elif not early or len(second) != 1 or len(drains) != len(early) + 1 or tail:
            raise ValueError(f"Expected GC and post-render drains at arrival {i}")

        row = dict(Arrival=i, Chamber=chamber, FullMapMs=world.ms, FullGCMs=gc.ms,
                   GCDrainMs=sum(s.ms for s in early), PostGCDrainMs=sum(s.ms for s in second),
                   EndDrainMs=sum(s.ms for s in tail))
        if count_pages:
            counts = {s: only([count for marker, count in releases if s.contains(marker)], RELEASE) for s in drains}
            row.update(GCReleased=sum(counts[s] for s in early), PostGCReleased=sum(counts[s] for s in second),
                       EndReleased=counts[drains[0]] if deferred else 0)
            frames = events[FRAME][bisect_left(ticks, world.end):][:12]
            if len(frames) != 12 or (i + 1 < len(loads) and frames[-1].end > events[LOAD][i + 1].start):
                raise ValueError(f"Incomplete recovery window at arrival {i}")
            for phase, start, end in (("PostGC", gc.end, preload.start),
                                      ("Build", preload.start, world.end),
                                      ("ReturnTail", world.end, frames[0].start),
                                      ("FirstTick", frames[0].start, frames[0].end),
                                      ("LaterTicks", frames[0].end, frames[-1].end)):
                windows.append((start, end, phase, row))
                row[phase + "Heap"] = row[phase + "Reuse"] = 0
        rows.append(row)
    if count_pages:
        count_requests(trace, windows)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("events", type=Path)
    args = parser.parse_args()
    try:
        rows = summarize(args.run, args.events)
    except (OSError, ValueError, KeyError, csv.Error) as error:
        parser.exit(1, f"{error}\n")
    writer = csv.DictWriter(sys.stdout, rows[0], lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({k: f"{v:.3f}" if isinstance(v, float) else v for k, v in row.items()})


if __name__ == "__main__":
    main()
