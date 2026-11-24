"""Build the Stairs hint-terminal material from the owner's installed SERLO pack."""
import unreal

source = "/Game/HologramShader/Materials/HologramShader/MI/MI_Hologram_1"
folder = "/Game/AbandonedLocal/HintTerminal"
name = "MI_HintProjection"
library = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary

parent = unreal.load_asset(source)
if not isinstance(parent, unreal.MaterialInstanceConstant):
    raise RuntimeError(f"Install Hologram Shader from Fab first: {source}")
path = f"{folder}/{name}"
material = (unreal.load_asset(path) if library.does_asset_exist(path)
            else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()))
edit.set_material_instance_parent(material, parent)
# Set a stationary, neutral field. The physical sign and the interaction prompt carry the message.
vectors = {
    "Hologram Color": (.65, .80, .84, 1),
    "Shell Color 1": (.12, .16, .17, 1),
    "Shell Color 2": (.20, .25, .27, 1),
    "Lines Time": (0, 0, 0, 0),
    "Hologram Control": (4, 4, 0, 0),
    "Cover UV Control": (1, 1, 0, 0),
    "Shell Texture Control": (1, .01, 0, 1),
    "Opacity Texture Control": (1, 1, 0, 0),
    "Distortion Cover Control": (0, 0, 0, 0),
    "Distortion Lines Control": (0, 0, 0, 0),
}
available = {str(key) for key in edit.get_vector_parameter_names(parent)}
for parameter, values in vectors.items():
    if parameter not in available:
        raise RuntimeError(f"Missing material parameter {parameter}")
    edit.set_material_instance_vector_parameter_value(material, parameter, unreal.LinearColor(*values))
    applied = edit.get_material_instance_vector_parameter_value(material, parameter)
    if any(abs(a - b) > 1e-5 for a, b in zip((applied.r, applied.g, applied.b, applied.a), values)):
        raise RuntimeError(f"Could not apply {parameter}")
edit.update_material_instance(material)
if not library.save_loaded_asset(material, False):
    raise RuntimeError(f"Could not save {path}")
unreal.log(f"HINT_PROJECTION_READY {path} parent={source}")
