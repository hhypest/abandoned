"""Prepare a local soundtrack and non-interactive prop from the specified Fab downloads.
Requires Python 3.11+, numpy, and a mono PCM16 reading of the Russian line.
"""
from pathlib import Path
import hashlib
import io
import sys
import tarfile
import wave
import zipfile
import numpy as np

ROOT = Path(__file__).resolve().parents[1] / "AssetSource/Dev20Ending"
RATE = 48000


def check(path, expected):
    with path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != expected:
            raise ValueError(f"Unexpected archive revision: {path.name}")


def read_wave(stream):
    with wave.open(stream, "rb") as sound:
        channels, width, rate, count = sound.getparams()[:4]
        raw = sound.readframes(count)
    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(float) / 32768
    elif width == 3:
        data = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        values = data[:, 0] | data[:, 1] << 8 | data[:, 2] << 16
        data = ((values ^ 0x800000) - 0x800000) / 8388608
    else:
        raise ValueError(f"Unsupported PCM width {width}")
    mono = data.reshape(-1, channels).mean(axis=1)
    return np.interp(np.arange(round(len(mono) * RATE / rate)) * rate / RATE, np.arange(len(mono)), mono)


def main(mesh, library, voice):
    check(mesh, "37b5cd71bdae8708984351a091b7bec39327984f3bb28f0566198361ad947479")
    check(library, "46e838dbad94662b29c6c7a08a9b22d617556b3d3b5721cab1f04665e16f1111")
    ROOT.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(mesh) as archive:
        (ROOT / "SM_ExpeditionSidearm.fbx").write_bytes(archive.read("FBX/NonRigged/NonRigged_M1911.fbx"))
    draw_id = "0b98d8dcea31eed9ff38708b82166481"
    click_id = "f42c580463f1dc144aee780d5df4005b"
    with tarfile.open(library, "r:gz") as archive:
        def take(guid, suffix):
            if not archive.extractfile(guid + "/pathname").read().endswith(suffix):
                raise ValueError("Unexpected foley take")
            return read_wave(io.BytesIO(archive.extractfile(guid + "/asset").read()))
        draw = take(draw_id, b"Full_Size_9mm_Pistol_Holster_Draw_Take_1.wav")
        click = take(click_id, b"Full_Size_9mm_Pistol_Slide_Lock_Release_Take_1.wav")
    with voice.open("rb") as stream:
        line = read_wave(stream)
    if len(line) > round(1.65 * RATE):
        raise ValueError("Voice line exceeds its 1.65-second subtitle")
    mix = np.zeros(round(4.2 * RATE))
    def place(start, samples, volume):
        offset = round(start * RATE)
        if len(samples) > len(mix) - offset:
            raise ValueError("Audio exceeds scene duration")
        peak = max(abs(samples))
        if peak <= 0:
            raise ValueError("Silent source")
        mix[offset:offset + len(samples)] += volume * samples / peak
    place(0, draw[:int(.75 * RATE)], .20)
    place(.55, line, .58)
    # Pitch and layer the library transient to suggest the distant sound.
    peak = int(np.argmax(np.abs(click)))
    start = max(0, peak - int(.012 * RATE))
    transient = click[start:start + int(.18 * RATE)]
    slowed = np.interp(np.arange(0, len(transient), .43), np.arange(len(transient)), transient)
    place(2.2, transient, .40)
    place(2.2, slowed, .32)
    place(2.29, slowed, .12)
    with wave.open(str(ROOT / "S_Dev20Ending.wav"), "wb") as out:
        out.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        out.writeframes((np.clip(mix, -1, 1)*32767).astype("<i2").tobytes())


if __name__ == "__main__":
    main(*(Path(s) for s in sys.argv[1:]))
