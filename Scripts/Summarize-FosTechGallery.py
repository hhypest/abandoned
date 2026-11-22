"""Compare uncapped packaged 1080p FosTech gallery runs in acquisition order."""
import csv
import hashlib
import json
import re
import runpy
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path
from statistics import median

profile = runpy.run_path(str(Path(__file__).with_name("Summarize-VulkanProfile.py")))


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sample_time(log, marker):
    times = re.findall(r"^\[([\d.:-]+)\].*LogFosTechGallery: Display: FosTech\.Sample" + marker,
                       log, flags=re.MULTILINE)
    if len(times) != 1:
        raise ValueError(f"Invalid {marker} timestamp")
    return datetime.strptime(times[0], "%Y.%m.%d-%H.%M.%S:%f").replace(tzinfo=timezone.utc)


def summarize(directories):
    packages, series, frames_by_variant = {}, [], {}
    for folder in directories:
        run = load_json(folder / "Run.json")
        receipt = load_json(folder / "PackageReceipt.json")
        if run["Scenario"] != "FosTechGallery" or run["StockDisplay"] or run["ExecutableSha256"] != receipt["ExecutableSha256"]:
            raise ValueError(f"Unexpected run or receipt in {folder}")
        log = (folder / "Game.log").read_text(encoding="utf-8-sig")
        count, = re.findall(r"FosTech\.SampleBegin meshes=13 nanite=(0|13)\b", log)
        variant = "on" if count == "13" else "off"
        if receipt != packages.setdefault(variant, receipt):
            raise ValueError("Package changed within variant")
        csv_path = folder / "Saved/Profiling/CSV" / run["Profile"]
        frames = profile["read_frames"](csv_path)
        start, = [i for i, f in enumerate(frames) if "FosTech.SampleBegin" in f["EVENTS"]]
        end, = [i for i, f in enumerate(frames) if "FosTech.SampleEnd" in f["EVENTS"]]
        if end - start != 300:
            raise ValueError("Incomplete gallery frame interval")
        window = frames[start:end]
        frames_by_variant.setdefault(variant, []).extend(window)
        first, last = (sample_time(log, marker) for marker in ("Begin", "End"))
        memory_path = folder / "Memory.csv"
        with memory_path.open(encoding="utf-8-sig", newline="") as stream:
            samples = list(csv.DictReader(stream))
        memory = [int(s["DedicatedBytes"]) / 2**20 for s in samples
                  if first <= datetime.fromisoformat(s["SampleUtc"].replace("Z", "+00:00")) <= last]
        if not memory or min(memory) <= 0:
            raise ValueError("Missing per-process WDDM readings")
        screenshot = folder / "Saved/Screenshots/Windows/FosTech-Gallery.png"
        if struct.unpack(">II", screenshot.read_bytes()[16:24]) != (1920, 1080):
            raise ValueError("Screenshot was not captured at 1080p")
        series.append(dict(run=folder.name, nanite=variant, timings=profile["metrics"](window),
                           wddmSamples=len(memory), medianDedicatedMiB=median(memory),
                           maxDedicatedMiB=max(memory),
                           lifetimeMaxDedicatedMiB=max(int(s["DedicatedBytes"] or 0) for s in samples) / 2**20,
                           csvSha256=digest(csv_path), memorySha256=digest(memory_path), screenshotSha256=digest(screenshot)))
    if set(packages) != {"off", "on"} or packages["off"]["ExecutableSha256"] != packages["on"]["ExecutableSha256"]:
        raise ValueError("Need two variants with the same executable")
    if packages["off"]["ContentContainers"] == packages["on"]["ContentContainers"]:
        raise ValueError("Expected distinct asset containers")
    if len({r["run"] for r in series}) != len(series) or len(frames_by_variant["off"]) != len(frames_by_variant["on"]):
        raise ValueError("Need distinct runs and balanced sample counts")
    return dict(packages=packages, timingColumns=["p95_ms", "p99_ms"],
                pooled={v: profile["metrics"](f) for v, f in frames_by_variant.items()}, runs=series)


if __name__ == "__main__":
    print(json.dumps(summarize([Path(arg) for arg in sys.argv[1:]]), indent=2))
