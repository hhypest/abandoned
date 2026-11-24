"""Build the local DEV-20 soundtrack in the UE 5.7 editor (embedded Python 3.11)."""
import audioop
import tempfile
import wave
from pathlib import Path

import unreal

RATE = 48000
DEST = "/Game/AbandonedLocal/Dev20Ending"
FIRE = "/Game/MW_Pistol_SK/WAV/Fire/SW_01_Pistol_Type1_Fire"
PISTOL = "/Game/M1911/Meshes/NonRigged_M1911"
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
VOICE = ROOT / "AssetSource/Dev20Ending/Line.wav"


def pcm(path):
    with wave.open(str(path), "rb") as reader:
        width, channels, rate = reader.getsampwidth(), reader.getnchannels(), reader.getframerate()
        if width not in (2, 3) or channels not in (1, 2):
            raise ValueError(f"Expected mono or stereo PCM16/24 in {path.name}")
        data = reader.readframes(reader.getnframes())
    if channels == 2:
        data = audioop.tomono(data, width, 0.5, 0.5)
    data = audioop.lin2lin(data, width, 2)
    return audioop.ratecv(data, 2, 1, rate, RATE, None)[0]


def put(track, clip, start, peak):
    offset = round(start * RATE) * 2
    end = offset + len(clip)
    amplitude = audioop.max(clip, 2)
    if not amplitude or end > len(track):
        raise ValueError("Silent or overlong ending clip")
    track[offset:end] = audioop.mul(clip, 2, peak * 32767 / amplitude)


def prepare():
    voice = pcm(VOICE)
    if len(voice) > round(1.65 * RATE) * 2:
        raise ValueError("Voice must fit the 1.65-second subtitle")
    shot = unreal.load_asset(FIRE)
    if not isinstance(shot, unreal.SoundWave):
        raise RuntimeError(f"Install Modern Pistols SFX in the project: {FIRE}")
    with tempfile.TemporaryDirectory(prefix="abandoned-dev20-") as scratch:
        clip = Path(scratch) / "shot.wav"
        export = unreal.AssetExportTask()
        export.object = shot
        export.filename = str(clip)
        export.automated = True
        if not unreal.Exporter.run_asset_export_task(export) or not clip.is_file():
            raise RuntimeError("Could not export the selected SoundWave")
        sound = pcm(clip)
        # Leave room for the title after the decay of the offscreen sound.
        sound = bytearray(sound[:2 * RATE * 2])
        fade = min(len(sound) // 2, RATE // 40)
        for frame in range(fade):
            offset = len(sound) - 2 * (fade - frame)
            sample = audioop.mul(sound[offset:offset + 2], 2, (fade - frame - 1) / fade)
            sound[offset:offset + 2] = sample
        track = bytearray(round(4.2 * RATE) * 2)
        put(track, voice, .55, .58)
        put(track, sound, 2.2, .54)
        # Keep the reimport source at a persistent private project path.
        mixed = VOICE.with_name("S_Dev20Ending.wav")
        with wave.open(str(mixed), "wb") as out:
            out.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
            out.writeframes(track)
    task = unreal.AssetImportTask()
    task.filename = str(mixed)
    task.destination_path = DEST
    task.automated = task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.load_asset(DEST + "/S_Dev20Ending")
    if not task.imported_object_paths or not asset or not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError("Could not save the ending audio")
    model = DEST + "/SM_ExpeditionSidearm"
    if not unreal.EditorAssetLibrary.does_asset_exist(model):
        copy = unreal.EditorAssetLibrary.duplicate_asset(PISTOL, model)
        if not copy or not unreal.EditorAssetLibrary.save_loaded_asset(copy, False):
            raise RuntimeError("Install the M1911 model or restore the existing local belt mesh")
    unreal.log(f"DEV20_AUDIO_READY source={FIRE} duration={asset.duration:.2f}s")


prepare()
