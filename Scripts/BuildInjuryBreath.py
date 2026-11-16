"""Synthesize and import the project's short helmet breath into Unreal."""

from array import array
from math import exp, pi, sin
from pathlib import Path
from random import Random
import wave

import unreal


SAMPLE_RATE = 44_100
NAME = "S_SuitInjuryBreath"
DESTINATION = "/Game/Abandoned/Audio"


def breath_envelope(time, start, duration, attack, gain):
    progress = (time - start) / duration
    if not 0.0 < progress < 1.0:
        return 0.0
    rise = min(1.0, progress / attack)
    fade = (1.0 - progress) ** 1.35
    return gain * sin(rise * pi / 2.0) ** 2 * fade


def render_sound(destination):
    generator = Random(3217)
    samples = []
    lowpass = 0.0
    collar = 0.0
    rumble = 0.0
    phase = 0.0
    for index in range(int(1.26 * SAMPLE_RATE)):
        time = index / SAMPLE_RATE
        noise = generator.uniform(-1.0, 1.0)
        lowpass += (1.0 - exp(-2.0 * pi * 1950.0 / SAMPLE_RATE)) * (noise - lowpass)
        collar += (1.0 - exp(-2.0 * pi * 420.0 / SAMPLE_RATE)) * (lowpass - collar)
        rumble += (1.0 - exp(-2.0 * pi * 110.0 / SAMPLE_RATE)) * (collar - rumble)
        inhale = breath_envelope(time, 0.0, 0.36, 0.24, 1.0)
        exhale = breath_envelope(time, 0.43, 0.82, 0.22, 0.52)
        phase += 2.0 * pi * (116.0 + 38.0 * exp(-7.0 * time)) / SAMPLE_RATE
        throat = sin(phase) + 0.25 * sin(2.0 * phase)
        samples.append(inhale * (lowpass - rumble + 0.015 * throat) +
                       exhale * (0.75 * lowpass + collar - 1.75 * rumble))

    level = (0.28 * 32767.0) / max(abs(sample) for sample in samples)
    pcm = array("h", (round(sample * level) for sample in samples))
    with wave.open(str(destination), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm.tobytes())


def import_breath():
    source = Path(unreal.Paths.project_saved_dir()).resolve() / "GeneratedAudio" / f"{NAME}.wav"
    source.parent.mkdir(parents=True, exist_ok=True)
    render_sound(source)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", DESTINATION)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    sound = unreal.EditorAssetLibrary.load_asset(f"{DESTINATION}/{NAME}")
    if not isinstance(sound, unreal.SoundWave):
        raise RuntimeError("Could not import the synthesized helmet breath as a SoundWave.")
    unreal.log(f"INJURY_BREATH_READY {sound.get_path_name()} {sound.get_editor_property('duration'):.2f}s")


if __name__ == "__main__":
    import_breath()
