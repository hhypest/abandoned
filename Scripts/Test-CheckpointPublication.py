"""Check ordered checkpoint publication and graceful exit in a packaged Win64 game on NTFS."""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import json
import os
import subprocess
import time
import uuid


class Overlapped(ctypes.Structure):
    _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD), ("Event", wintypes.HANDLE)]


class OplockRequest(ctypes.Structure):
    _fields_ = [("Version", wintypes.WORD), ("Length", wintypes.WORD),
                ("Level", wintypes.DWORD), ("Flags", wintypes.DWORD)]


class OplockResponse(ctypes.Structure):
    _fields_ = [("Version", wintypes.WORD), ("Length", wintypes.WORD),
                ("OriginalLevel", wintypes.DWORD), ("NewLevel", wintypes.DWORD),
                ("Flags", wintypes.DWORD), ("Access", wintypes.DWORD), ("Share", wintypes.WORD)]


def native(dll, name, result, *args):
    function = getattr(dll, name)
    function.restype = result
    function.argtypes = list(args)
    return function


class PublicationGate:
    """A read/handle oplock allows validation reads and pauses replacement of the existing slot."""

    def __init__(self, slot):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        handle, dword, pointer = wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID
        open_file = native(kernel, "CreateFileW", handle, wintypes.LPCWSTR, dword, dword,
                           pointer, dword, dword, handle)
        create_event = native(kernel, "CreateEventW", handle, pointer, wintypes.BOOL,
                              wintypes.BOOL, wintypes.LPCWSTR)
        device_control = native(kernel, "DeviceIoControl", wintypes.BOOL, handle, dword,
                                pointer, dword, pointer, dword, ctypes.POINTER(dword), ctypes.POINTER(Overlapped))
        self.close = native(kernel, "CloseHandle", wintypes.BOOL, handle)
        self.wait = native(kernel, "WaitForSingleObject", dword, handle, dword)
        # GENERIC_READ, share read/write/delete, OPEN_EXISTING, FILE_FLAG_OVERLAPPED.
        self.file = open_file(str(slot), 0x80000000, 7, None, 3, 0x40000000, None)
        if self.file == handle(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        self.event = create_event(None, True, False, None)
        if not self.event:
            error = ctypes.get_last_error()
            self.close(self.file)
            raise ctypes.WinError(error)
        self.overlapped = Overlapped(Event=self.event)
        self.request = OplockRequest(1, ctypes.sizeof(OplockRequest), 3, 1)
        self.response = OplockResponse()
        # FSCTL_REQUEST_OPLOCK, CACHE_READ | CACHE_HANDLE, REQUEST.
        result = device_control(self.file, 0x90240, ctypes.byref(self.request), ctypes.sizeof(self.request),
                                ctypes.byref(self.response), ctypes.sizeof(self.response), None,
                                ctypes.byref(self.overlapped))
        error = ctypes.get_last_error()
        if result or error != 997:  # An acquired oplock completes asynchronously with ERROR_IO_PENDING.
            self.close(self.file)
            self.close(self.event)
            raise RuntimeError(f"Cannot acquire an NTFS oplock (Win32 {error})")

    def await_replacement(self):
        if self.wait(self.event, 60_000) != 0:
            raise RuntimeError("Timed out waiting for the game's replacement request")

    def release(self):
        if self.file is not None:
            # Closing the oplock owner acknowledges the break and releases a waiting rename.
            self.close(self.file)
            self.wait(self.event, 5000)
            self.close(self.event)
            self.file = None


def read_log(log):
    return log.read_text(encoding="utf-8-sig", errors="replace") if log.is_file() else ""


def run_case(package, profile, commands, marker, final_chamber):
    slot = profile / "Saved/SaveGames/AbandonedCampaign.v2.json"
    slot.parent.mkdir(parents=True)
    previous = {"version": 2, "checkpoint": "Stairs", "archive": ["oxygen-restored"]}
    slot.write_text(json.dumps(previous), encoding="utf-8")
    log = profile / "Game.log"
    exe = package / "Abandoned/Binaries/Win64/Abandoned.exe"
    args = [str(exe), "/Engine/Maps/Entry?Chamber=Weight", "-vulkan", "-unattended",
            "-windowed", "-ResX=1280", "-ResY=720", "-forcelogflush", f"-UserDir={profile}",
            f"-abslog={log}", "-trace=cpu,bookmark,frame", f"-tracefile={profile / 'Travel.utrace'}",
            "-ExecCmds=CsvProfile FRAMES=120", "-csvExecCmds=" + commands]
    gate = PublicationGate(slot)
    game = None
    try:
        game = subprocess.Popen(args, cwd=package, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        gate.await_replacement()
        start = time.monotonic()
        while marker not in read_log(log):
            if game.poll() is not None or time.monotonic() - start > 20:
                raise RuntimeError(f"Missing {marker!r} with the replacement pending in {log}")
            time.sleep(0.02)
        time.sleep(1.5)
        text = read_log(log)
        if game.poll() is not None or "Saved campaign checkpoint v2: Weight" in text:
            raise RuntimeError(f"The process exited or reported publication while blocked: {log}")
        gate.release()
        held = time.monotonic() - start
        if game.wait(timeout=30) != 0:
            raise RuntimeError(f"Game exit {game.returncode}: {log}")
        text = read_log(log)
        required = ["Vulkan RHI ShaderPlatform for SM6: VULKAN_SM6", "Training chamber ready. Weight",
                    "Saved campaign checkpoint v2: Weight (1 archive records).",
                    f"Saved campaign checkpoint v2: {final_chamber} (1 archive records).", "LogExit: Exiting."]
        if any(item not in text for item in required):
            raise RuntimeError(f"Missing completion marker in {log}")
        expected = dict(previous, checkpoint=final_chamber)
        if json.loads(slot.read_text(encoding="utf-8-sig")) != expected or list(slot.parent.glob("*.tmp")):
            raise RuntimeError(f"Incorrect final slot or leftover temporary file in {slot.parent}")
        print(f"{profile.name}: held {held:.3f} s, exit 0, final checkpoint {final_chamber}")
    finally:
        gate.release()
        if game and game.poll() is None:
            try:
                game.wait(timeout=5)
            except subprocess.TimeoutExpired:
                game.kill()
                game.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="Packaged Windows directory")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This check requires Windows and local NTFS")
    root = Path(__file__).resolve().parents[1] / "Saved/CheckpointPublication" / uuid.uuid4().hex
    package = args.package.resolve()
    print(f"Captures: {root}", flush=True)
    run_case(package, root / "gate-exit", "2:quit", "Engine exit requested", "Weight")
    run_case(package, root / "gate-chain", "2:open /Engine/Maps/Entry?Chamber=Button,5:quit",
             "LogLoad: LoadMap: /Engine/Maps/Entry?Chamber=Button", "Button")


if __name__ == "__main__":
    main()
