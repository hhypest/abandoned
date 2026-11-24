"""Prepare the two optional passage finishes from the installed Concrete 07 Fab pack."""
import unreal

base = "/Game/SM_Concrete_07/Materials/Concrete_07"
destination = "/Game/AbandonedLocal/Concrete07"
for name, source, tint, relief in (
    ("MI_PassageFloor", "MT00082-Concrete_Clean/MI_MT00082_Concrete_Clean", (.58, .65, .73), 1.0),
    ("MI_PassageWall", "MT00183-Rough_Concrete/MI_MT00183_Rough_Concrete", (.38, .46, .56), .64),
):
    parent = unreal.load_asset(f"{base}/{source}")
    if not isinstance(parent, unreal.MaterialInstanceConstant):
        raise RuntimeError(f"Install Concrete 07 in the project: {source}")
    path = f"{destination}/{name}"
    material = (unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path)
                else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                    name, destination, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()))
    unreal.MaterialEditingLibrary.set_material_instance_parent(material, parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        material, "Base Color Multiplier", unreal.LinearColor(*tint, 1.0))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material, "Normal Strength", relief)
    unreal.MaterialEditingLibrary.update_material_instance(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, False):
        raise RuntimeError(f"Could not save {name}")
    unreal.log(f"CONCRETE07_READY {name} parent={parent.get_name()}")
