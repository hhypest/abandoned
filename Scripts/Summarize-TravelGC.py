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
HASH = "ShrinkUObjectHashTables"
TRIM = "FMemory::Trim"
BROADCAST = "FTaskGraphInterface_BroadcastSlow"
DELEGATES = "Memory_TrimDelegates"
PAGES = "MemStack_TrimFreePages"
IO = "IoDispatcher_TrimAllocators"
OS_PAGES = "Binned2_ReleaseCachedOSPages"
STAT_DELEGATES = "STAT_FMemory_Trim_Broadcast"
STAT_ALLOCATOR = "STAT_FMemory_Trim_GMalloc"
DRAW = "FViewport_Draw"
SUBMIT = "BeginRenderingViewFamily"
TRAVERSALS = {"parallel": "GC_ParallelReferenceTraversal", "serial": "GC_SerialReferenceTraversal"}
TIMERS = {DRAW, SUBMIT, GC, UNLOAD, PRELOAD, ACTORS, PURGE, FINISH, DEFERRED, DELETE, LOCK, REACH, HASH, TRIM, BROADCAST,
          DELEGATES, PAGES, IO, OS_PAGES, STAT_DELEGATES, STAT_ALLOCATOR, *TRAVERSALS.values()}


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


def memory_tail_phases(events, gc, purge, mode, allocator):
    def child(parent, timer):
        return one([s for s in events[timer] if parent.contains(s)], timer)

    # A pending incremental purge can issue its own trim before marking starts.
    shrink = one([s for s in events[HASH] if gc.contains(s) and s.start >= purge.end], HASH)
    trim = one([s for s in events[TRIM] if gc.contains(s) and s.start >= shrink.end], TRIM)
    broadcast = child(trim, BROADCAST)
    result = dict(HashShrinkMs=shrink.ms, MemoryTrimMs=trim.ms,
                  BeforeBroadcastMs=(broadcast.start - trim.start) * 1000,
                  CacheBroadcastMs=broadcast.ms, AfterBroadcastMs=(trim.end - broadcast.end) * 1000)
    if mode == "stats":
        delegates, heap = child(trim, STAT_DELEGATES), child(trim, STAT_ALLOCATOR)
        if delegates.end > heap.start or not heap.contains(broadcast):
            raise ValueError("Invalid allocator statistics inside trim")
        result.update(TrimDelegatesMs=delegates.ms, AllocatorTrimMs=heap.ms)
    elif mode == "owners":
        delegates = child(trim, DELEGATES)
        pages, io = child(delegates, PAGES), child(delegates, IO)
        if delegates.end > broadcast.start or min(pages.end, io.end) > max(pages.start, io.start):
            raise ValueError("Overlapping memory trim callbacks")
        os_pages = [s for s in events[OS_PAGES] if trim.contains(s)]
        result.update(TrimDelegatesMs=delegates.ms, MemStackPagesMs=pages.ms, IoAllocatorsMs=io.ms)
        if allocator == "Binned2":
            release = one(os_pages, OS_PAGES)
            if release.start < broadcast.end:
                raise ValueError("OS page release overlaps the cache broadcast")
            result["ReleaseCachedOSPagesMs"] = release.ms
        elif os_pages:
            raise ValueError("Unexpected Binned2 page release in Binned3 capture")
        else:
            result["ReleaseCachedOSPagesMs"] = ""
    return result


def summarize(log, trace, expect_traversal=None, memory_tail=None, outgoing_redraw=None):
    # Pair the log and the trace by the actor timer on each visit, as in the OpenLevel report.
    text = log.read_text(encoding="utf-8-sig")
    allocator = None
    if memory_tail == "owners":
        allocator = one(re.findall(r"^LogInit: Allocator: (Binned[23])\s*$", text, re.MULTILINE), "Binned allocator")
    parts = text.split("LogLoad: LoadMap: ")[1:]
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
        if outgoing_redraw:
            if ("quiet" in url) != (outgoing_redraw == "quiet"):
                raise ValueError(f"Expected {outgoing_redraw} in the travel URL at arrival {index}")
            draws = [s for s in events[DRAW] if unload.contains(s)]
            submissions = [s for s in events[SUBMIT] if unload.contains(s)]
            expected = int(outgoing_redraw == "redraw")
            if len(draws) != expected or len(submissions) != expected or (draws and not draws[0].contains(submissions[0])):
                raise ValueError(f"Expected {outgoing_redraw} during world teardown at arrival {index}")
            # Both modes must still submit the incoming world. This also rejects an export missing both timers.
            until = events[UNLOAD][index + 1].start if index + 1 < len(parts) else math.inf
            first = next((s for s in events[DRAW] if actor.end <= s.start < until), None)
            if first is None or not any(first.contains(s) for s in events[SUBMIT]):
                raise ValueError(f"Missing incoming world view submission at arrival {index}")
            rows[-1].update(OutgoingDraws=len(draws), SubmittedViewFamilies=len(submissions),
                            OutgoingDrawMs=sum(s.ms for s in draws))
        if memory_tail:
            rows[-1].update(memory_tail_phases(events, gc, purge, memory_tail, allocator))
        if expect_traversal:
            mode, span = one([(mode, s) for mode, name in TRAVERSALS.items()
                              for s in events[name] if reach.contains(s)], "reference traversal")
            if mode != expect_traversal:
                raise ValueError(f"Expected {expect_traversal} traversal at arrival {index}, found {mode}")
            rows[-1].update(Traversal=mode, TraversalMs=span.ms)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("trace_csv", type=Path)
    parser.add_argument("--expect-traversal", choices=TRAVERSALS,
                        help="Require the matching UE SmallFullGC marker on every transition")
    parser.add_argument("--memory-tail", nargs="?", const="phases", choices=("phases", "stats", "owners"),
                        help="Measure final Binned2/3 trim, optionally with verbose UE stats or MemoryTrimScopes")
    parser.add_argument("--outgoing-redraw", choices=("redraw", "quiet"),
                        help="Verify outgoing and incoming world view submissions during chamber travel")
    args = parser.parse_args()
    try:
        rows = summarize(args.log, args.trace_csv, args.expect_traversal, args.memory_tail, args.outgoing_redraw)
    except (OSError, ValueError, csv.Error) as error:
        parser.exit(1, f"{error}\n")
    writer = csv.DictWriter(sys.stdout, fieldnames=rows[0], lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({k: f"{v:.3f}" if isinstance(v, float) else v for k, v in row.items()})


if __name__ == "__main__":
    main()
