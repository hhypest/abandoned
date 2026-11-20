"""Measure UE 5.7.4 synchronous travel GC on the game thread, in milliseconds."""
import argparse
import csv
import math
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


GC = "LoadMap_FullGC"
UNLOAD = "LoadMap_UnloadWorld"
PRELOAD = "LoadMap_PreloadWorld"
ACTORS = "UWorld::InitializeActorsForPlay"
PURGE = "IncrementalPurgeGarbage"
FINISH = "ConditionalFinishDestroy"
DEFERRED = "ConditionalFinishDestroyDeferred"
DELETE = "DestroyObjects"
LOCK = "AcquireGCLock"
REACH = "FRealtimeGC_PerformReachabilityAnalysis"
TIMERS = {GC, UNLOAD, PRELOAD, ACTORS, PURGE, FINISH, DEFERRED, DELETE, LOCK, REACH}


@dataclass(frozen=True, order=True)
class Event:
    start: float
    end: float
    depth: int

    @property
    def ms(self):
        return (self.end - self.start) * 1000

    def contains(self, event):
        return self.start <= event.start <= event.end <= self.end and self.depth < event.depth


def read_trace(path):
    events = defaultdict(list)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = csv.DictReader(stream)
        for column in ("ThreadName", "TimerName", "StartTime", "EndTime", "Depth"):
            if column not in (rows.fieldnames or ()):
                raise ValueError(f"Missing column {column}")
        for row in rows:
            name = row["TimerName"]
            if name in TIMERS and row["ThreadName"] == "GameThread":
                event = Event(float(row["StartTime"]), float(row["EndTime"]), int(row["Depth"]))
                if not (math.isfinite(event.start) and math.isfinite(event.end)
                        and 0 <= event.start <= event.end and event.depth >= 0):
                    raise ValueError(f"Invalid interval for {name}")
                events[name].append(event)
    for entries in events.values():
        entries.sort()
    return events


def one(entries, label):
    if len(entries) != 1:
        raise ValueError(f"Expected exactly one {label}, found {len(entries)}")
    return entries[0]


def summarize(log, trace):
    # Pair the log and the trace by the actor timer on each visit, as in the OpenLevel report.
    parts = log.read_text(encoding="utf-8-sig").split("LogLoad: LoadMap: ")[1:]
    events = read_trace(trace)
    if len(parts) < 4 or len(parts) % 2 or any(len(events[key]) != len(parts) for key in (UNLOAD, PRELOAD, ACTORS)):
        raise ValueError("Expected a complete alternating run with at least two visits to Weight")
    rows = []
    for index, part in enumerate(parts):
        chamber = "Weight" if index % 2 else "Stairs"
        url = part.splitlines()[0].split("?")
        if url[0] != "/Engine/Maps/Entry" or [x for x in url if x.startswith("Chamber=")] != ["Chamber=" + chamber]:
            raise ValueError(f"Unexpected route at arrival {index}: {url}")
        load = one(re.findall(r"LogLoad: Took ([\d.]+) seconds to LoadMap\(/Engine/Maps/Entry\)", part), "LoadMap duration")
        during_load = part.split("LogLoad: Took ", 1)[0]
        actors = one(re.findall(r"LogWorld: Bringing up level for play took: ([\d.]+)", during_load), "actor duration")
        unload, preload, actor = (events[key][index] for key in (UNLOAD, PRELOAD, ACTORS))
        if not unload.end <= preload.start <= preload.end <= actor.start or abs(float(actors) * 1000 - actor.ms) > .5:
            raise ValueError(f"Actor timing or phase order mismatch at arrival {index}")
        if index == 0:
            continue
        gc = one([s for s in events[GC] if unload.end <= s.start <= s.end <= preload.start], GC)
        # A pending incremental purge can finish before this collection starts marking.
        collection = during_load.rsplit("LogGarbage: Collecting garbage", 1)
        if len(collection) != 2:
            raise ValueError(f"Missing start of garbage collection at arrival {index}")
        objects = int(one(re.findall(r"LogGarbage: GC purged (\d+) objects", collection[1]), "purge object count"))
        tail = re.findall(r"LogGarbage: Display: Deferred GC tail: (\S+) [^\r\n]*? \([\d.]+ ms, initially (\d+) pending\)",
                          collection[1])
        last_class, pending = one(tail, "deferred GC tail") if tail else ("", "")
        purges = [s for s in events[PURGE] if gc.contains(s)]
        if not purges:
            raise ValueError(f"Missing purge at arrival {index}")
        purge = purges[-1]
        reach = one([s for s in events[REACH] if gc.contains(s)], REACH)
        if reach.end > purge.start:
            raise ValueError(f"Reachability overlaps the final purge at arrival {index}")
        finish, deferred, delete = [one([s for s in events[k] if purge.contains(s)], k) for k in (FINISH, DEFERRED, DELETE)]
        if not finish.end <= deferred.start <= deferred.end <= delete.start:
            raise ValueError("Overlapping or out-of-order purge phases")
        locks = [s for s in events[LOCK] if gc.contains(s)]
        if not locks or any(s.end > purge.start for s in locks) or any(a.end > b.start for a, b in zip(locks, locks[1:])):
            raise ValueError("Missing, overlapping or out-of-order GC lock scopes")
        rows.append(dict(Arrival=index, Chamber=chamber, Objects=objects, LoadMapMs=float(load) * 1000,
                         FullGCMs=gc.ms, AcquireLockMs=sum(s.ms for s in locks), ReachabilityMs=reach.ms, PurgeMs=purge.ms,
                         FinishFirstPassMs=finish.ms, DeferredFinishMs=deferred.ms, DeleteMs=delete.ms,
                         InitiallyPending=pending, LastReadyClass=last_class))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("trace_csv", type=Path)
    args = parser.parse_args()
    try:
        rows = summarize(args.log, args.trace_csv)
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(1, f"{error}\n")
    writer = csv.DictWriter(sys.stdout, fieldnames=rows[0], lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({k: f"{v:.3f}" if isinstance(v, float) else v for k, v in row.items()})


if __name__ == "__main__":
    main()
