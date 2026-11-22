"""Build the EQP-01 display surface. Composite moving glyphs after temporal reconstruction."""
import unreal

assets = unreal.EditorAssetLibrary
materials = unreal.MaterialEditingLibrary
folder = "/Game/Abandoned/Suit"
name = "M_WristDisplay"
path = f"{folder}/{name}"
if assets.does_asset_exist(path):
    display = assets.load_asset(path)
    materials.delete_all_material_expressions(display)
else:
    display = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Material, unreal.MaterialFactoryNew())

display.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
display.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
display.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
sample = materials.create_material_expression(display, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
sample.set_editor_property("parameter_name", "SlateUI")
default = assets.load_asset("/Engine/EngineResources/DefaultTexture")
sample.set_editor_property("texture", default)
sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
exposure = materials.create_material_expression(display, unreal.MaterialExpressionEyeAdaptationInverse, -220, 0)
if not (materials.connect_material_expressions(sample, "RGB", exposure, "")
        and materials.connect_material_property(exposure, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)):
    raise RuntimeError("Could not connect the exposure-compensated display")
# A solid black backing fills the readout. Retain the widget plane's normal depth test.
solid = materials.create_material_expression(display, unreal.MaterialExpressionConstant, -220, 160)
solid.set_editor_property("r", 1.0)
if not materials.connect_material_property(solid, "", unreal.MaterialProperty.MP_OPACITY):
    raise RuntimeError("Could not connect the display opacity")
materials.recompile_material(display)
if not assets.save_loaded_asset(display):
    raise RuntimeError("Could not save the wrist display material")
unreal.log("WRIST_DISPLAY_READY")
