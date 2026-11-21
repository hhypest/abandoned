"""Summarize stock Win64 Vulkan runs from one Windows boot (nearest rank, milliseconds)."""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import runpy

profile = runpy.run_path(str(Path(__file__).with_name("Summarize-VulkanProfile.py")))


def pct(values, fraction):
    if not values or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("Invalid timing sample")
    return sorted(values)[math.ceil(len(values) * fraction) - 1]


def describe(values, name):
    return {name + "P95Ms": round(pct(values, .95), 4), name + "P99Ms": round(pct(values, .99), 4)}


def summarize(folder):
    meta = json.loads((folder / "Run.json").read_text(encoding="utf-8-sig"))
    if (meta["Scenario"] != "Travel" or not meta["StockDisplay"] or meta["MapPageReserve"] is not None
            or meta["WeightShell"] or any(meta[key] for key in ("PreloadWeightShell", "InstanceWeightShellVisuals",
            "DisableRenderThreadLag", "DisableSmallGCPolicy", "RedrawOutgoingChamber", "DisableChamberMeshReuse"))):
        raise ValueError("Stock display and stock geometry required")
    log = (folder / "Game.log").read_text(encoding="utf-8-sig")
    rows = profile["read_frames"](folder / "Saved/Profiling/CSV" / meta["Profile"])
    commands = [i for i, row in enumerate(rows) if "CsvExecCommand : open /Engine/Maps/Entry?Chamber=Weight" in row["EVENTS"]]
    if (len(rows) != meta["CaptureFrames"] or len(commands) < 2
            or re.findall(r"Weight shell mode=(\w+)", log) != ["Separate"] * len(commands)
            or re.findall(r"Room decor mode=(\w+)", log) != ["instanced"] * (2 * len(commands))
            or 'r.VSync = "1"' not in log or 't.MaxFPS = "60"' not in log or 'LogExit: Exiting.' not in log
            or re.search(r"Fatal error:|Ensure condition failed|Found [1-9]\d* unfreed allocations", log)):
        raise ValueError("Incomplete stock route or invalid shutdown")
    init = re.search(r"^\[([\d.\-:]+)\].*?Engine is initialized\. Leaving FEngineLoop::Init", log, re.M)
    if not init:
        raise ValueError("Missing startup boundary")
    ready = datetime.strptime(init[1], "%Y.%m.%d-%H.%M.%S:%f").replace(tzinfo=timezone.utc)
    started = datetime.fromisoformat(meta["ProcessStartUtc"].replace("Z", "+00:00"))
    startup = (ready - started).total_seconds()
    if startup <= 0:
        raise ValueError("Invalid startup clock")
    visits = []
    for index, position in enumerate(commands, 1):
        window = rows[position:position+12]
        arrivals = [frame for frame in window if "GC" in [x.strip() for x in frame["EVENTS"].split(";")]]
        if len(window) != 12 or len(arrivals) != 1:
            raise ValueError(f"Missing travel frame at visit {index}")
        visits.append(dict(Visit=index, FrameMs=max(f["FrameTime"] for f in window),
                           ArrivalMs=arrivals[0]["FrameTime"], ArrivalCpuMs=arrivals[0]["CPU"],
                           ArrivalGpuMs=arrivals[0]["GPUTime"]))
    sample = dict(StartupSeconds=round(startup, 3), FirstWeightMs=visits[0]["FrameMs"])
    for label, key in (("Frame","FrameTime"), ("CPU","CPU"), ("GPU","GPUTime")):
        sample.update(describe([r[key] for r in rows], label))
        sample.update(describe([r[key] for r in rows[:120]], "First120" + label))
    return meta, started, sample, visits


def write(path, rows):
    with path.open("w", newline="", encoding="utf8") as stream:
        writer = csv.DictWriter(stream, rows[0], lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, help="C.path, W1.path, W2.path, W3.path, PreviousBootUtc.txt")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    previous = datetime.fromisoformat((args.captures/"PreviousBootUtc.txt").read_text().strip().replace("Z","+00:00"))
    samples, visits = [], []
    boot = digest = last = None
    for tag in ("C","W1","W2","W3"):
        folder = Path((args.captures/(tag+".path")).read_text(encoding="utf-8-sig").strip())
        meta, start, sample, entries = summarize(folder)
        current = datetime.fromisoformat(meta["BootUtc"].replace("Z","+00:00"))
        if len(entries) != 16 or not previous < current <= start or (last and start <= last):
            raise ValueError("Wrong boot, process order, or visit count")
        if boot is not None and (current != boot or meta["ExecutableSha256"] != digest):
            raise ValueError("Different boot or executable")
        boot, digest, last = current, meta["ExecutableSha256"], start
        samples.append(dict(Run=tag, BootUtc=meta["BootUtc"], ProcessStartUtc=meta["ProcessStartUtc"], **sample))
        visits.extend(dict(Run=tag, **r) for r in entries)
    warm = [r["FrameMs"] for r in visits if r["Visit"] > 1]
    timing = describe(warm, "WarmTransition")
    write(args.output/"A07-FinalStockProcesses.csv",samples)
    write(args.output/"A07-FinalStockVisits.csv",visits)
    passed = sum(ms<=16.67 for ms in warm)
    fmt = lambda v: f"{v:.3f}".replace(".", ",")
    lines = [
        "# A‑07. Итог приёмки штатного профиля",
        "",
        f"Первый запуск после перезагрузки Windows от создания процесса до конца FEngineLoop::Init занял **{fmt(samples[0]['StartupSeconds'])} с**. Последующие запуски — " + ", ".join(fmt(r['StartupSeconds']) for r in samples[1:])+" с.",
        "",
        "| Показатель, мс | Первый процесс после загрузки Windows | Тёплые процессы W1 / W2 / W3 |",
        "| --- | ---: | ---: |",
    ]
    for label,key in (("Первый вход в Weight","FirstWeightMs"),("Весь маршрут, кадр p95","FrameP95Ms"),
                      ("Весь маршрут, кадр p99","FrameP99Ms"),("CPU p95","CPUP95Ms"),("CPU p99","CPUP99Ms"),
                      ("GPU p95","GPUP95Ms"),("GPU p99","GPUP99Ms"),
                      ("Первые 120 кадров CSV, p95","First120FrameP95Ms"),("Первые 120 кадров CSV, p99","First120FrameP99Ms")):
        lines.append(f"| {label} | {fmt(samples[0][key])} | " + " / ".join(fmt(r[key]) for r in samples[1:]) + " |")
    lines += ["",f"За 60 повторных входов Stairs → Weight p95 кадра перехода **{fmt(timing['WarmTransitionP95Ms'])} мс**, p99 **{fmt(timing['WarmTransitionP99Ms'])} мс**. В бюджете 16,67 мс — **{passed}/60**.",
              "", "Кадр перехода — максимум 12 кадров от команды. Для 60 повторов ближайший верхний ранг p99 равен максимуму. Полное время включает штатное ожидание VSync и ограничителя. CPU — максимум счётчиков игрового, рендерного и RHI-потоков в каждом кадре. Первый процесс — одно холодное наблюдение. Первые 120 кадров CSV идут после инициализации, начало работы движка учтено отдельным таймером.",
              "",f"Одна загрузка ОС и один EXE, SHA-256 `{digest.lower()}`. [Измерения процессов](Measurements/A07-FinalStockProcesses.csv) · [Все посещения](Measurements/A07-FinalStockVisits.csv) · [Методика и проверка отмены](A07FinalAcceptance.md)"]
    (args.output.parent/"A07FinalStockResults.md").write_text("\n".join(lines)+"\n",encoding="utf8")
    print(json.dumps(dict(**timing, InBudget=passed, Count=len(warm)),ensure_ascii=True))


if __name__ == "__main__":
    main()
