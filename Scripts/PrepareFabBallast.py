"""Prepare Mint Chips 3D's Fab FBX archive for ImportFabBallast.py (Python + Pillow)."""
import hashlib
import shutil
import sys
import zipfile
from pathlib import Path

from PIL import Image


def prepare(archive, output):
    with archive.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != "b1f9488b54ca44e3dbc7165f252ab93bb5874664298c57669fa8a3934a7c587f":
            raise ValueError("Unexpected scifi_cube.zip revision")
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as files:
        with files.open("Scifi_Cube/source/Scifi-Cube.fbx") as mesh, (output / "SM_Ballast.fbx").open("wb") as out:
            shutil.copyfileobj(mesh, out)
        channels = []
        for source in ("Diffuse", "Normal", "Roughness", "Metalness", "Specular"):
            with files.open(f"Scifi_Cube/textures/{source}.png") as stream, Image.open(stream) as image:
                image = image.resize((2048, 2048), Image.Resampling.BOX)
                if source in ("Diffuse", "Normal"):
                    image.save(output / f"T_{source}.png")
                else:
                    channels.append(image.getchannel("R"))
        Image.merge("RGB", channels).save(output / "T_RMS.png")


if __name__ == "__main__":
    prepare(Path(sys.argv[1]), Path(__file__).resolve().parents[1] / "AssetSource/Fab/Ballast/Prepared")
