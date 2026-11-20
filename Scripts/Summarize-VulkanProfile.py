"""Summarize one completed Measure-VulkanPackage.ps1 run (Python 3 standard library)."""
import csv
import json
import math
import re
import sys
from pathlib import Path

THREADS = ("GameThreadTime", "RenderThreadTime", "RHIThreadTime")
COLUMNS = ("FrameTime", *THREADS, "GPUTime")


def read_frames(path):
    frames = []
    metadata = {}
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.reader(stream)
        header = next(reader)
        indices = {key: header.index(key) for key in (*COLUMNS, "EVENTS")}
        for row in reader:
            # UE appends a second header and a non-frame metadata record.
            if row[0] == "EVENTS":
                continue
            if row[0] == "[HasHeaderRowAtEnd]":
                metadata = dict(zip(row[::2], row[1::2]))
                continue
            frame = {key: float(row[indices[key]]) for key in COLUMNS}
            frame["CPU"] = max(frame[key] for key in THREADS)
            frame["EVENTS"] = row[indices["EVENTS"]]
            frames.append(frame)
    expected = {"[systemresolution.resx]": "1920", "[systemresolution.resy]": "1080",
                "[shaderplatform]": "VULKAN_SM6", "[config]": "Development"}
    if any(metadata.get(key) != value for key, value in expected.items()):
        raise ValueError("CSV metadata does not match the measured render profile")
    return frames


def metrics(frames):
    if not frames:
        raise ValueError("Empty frame window")
    result = {"frames": len(frames)}
    for key in (*COLUMNS, "CPU"):
        times = sorted(frame[key] for frame in frames)
        if any(not math.isfinite(value) or value < 0 for value in times) or times[-1] <= 0:
            raise ValueError(f"Invalid {key} measurements")
        result[key] = [times[math.ceil(len(times) * p) - 1] for p in (0.95, 0.99)]
    return result


def summarize(folder):
    run = json.loads((folder / "Run.json").read_text(encoding="utf-8-sig"))
    scenario = run["Scenario"]
    frames = read_frames(folder / "Saved/Profiling/CSV" / run["Profile"])
    if run["CaptureFrames"] and len(frames) != run["CaptureFrames"]:
        raise ValueError("Incomplete frame capture")
    if scenario == "Travel":
        weights = [i for i, frame in enumerate(frames)
                   if "CsvExecCommand : open /Engine/Maps/Entry?Chamber=Weight" in frame["EVENTS"]]
        if len(weights) < 2:
            raise ValueError("Missing Weight visit")
        log = (folder / "Game.log").read_text(encoding="utf-8-sig")
        loads = [float(s) * 1000 for s in re.findall(r"LogLoad: Took ([\d.]+) seconds to LoadMap\(/Engine/Maps/Entry\)", log)]
        if len(loads) != len(weights) * 2:
            raise ValueError("World-load count differs from the captured Weight visits")
        result = {}
        for visit, (i, load) in enumerate(zip(weights, loads[1::2]), start=1):
            name = "First" if visit == 1 else "Repeat" if visit == 2 else f"Visit{visit}"
            result[name] = metrics(frames[i + 120:i + 420])
            result[name]["LoadMapMs"] = load
            result[name]["MaxTransitionFrameMs"] = max(f["FrameTime"] for f in frames[i:i + 12])
            # Include the command lead-in, travel and the next 120 rendered frames.
            result[name]["TravelWindowMs"] = sum(f["FrameTime"] for f in frames[i - 3:i + 12])
            result[name]["MaxRecoveryFrameMs"] = max(f["FrameTime"] for f in frames[i + 12:i + 132])
    elif scenario == "Campaign":
        result = {"Campaign": metrics(frames[120:])}
    else:
        ready = [i for i, frame in enumerate(frames) if "PortalProfileReady" in frame["EVENTS"]]
        if len(ready) != 1 or ready[0] + 120 > len(frames) - 1000:
            raise ValueError("Portal was not stable for the measured window")
        result = {scenario: metrics(frames[-1000:])}
    with (folder / "Memory.csv").open(encoding="utf-8-sig", newline="") as source:
        memory = list(csv.DictReader(source))
    result["peakWorkingSetMiB"] = max(int(row["PeakWorkingSetBytes"]) for row in memory) / 2**20
    if "PrivateBytes" in memory[0]:
        result["peakPrivateMiB"] = max(int(row["PrivateBytes"]) for row in memory) / 2**20
    result["peakDedicatedMiB"] = max(int(row["DedicatedBytes"] or 0) for row in memory) / 2**20
    result["memorySamples"] = len(memory)
    result["exeSha256"] = run["ExecutableSha256"]
    return result


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: Summarize-VulkanProfile.py RUN_DIRECTORY [RUN_DIRECTORY ...]")
    print(json.dumps({Path(name).name: summarize(Path(name)) for name in sys.argv[1:]}, indent=2))
