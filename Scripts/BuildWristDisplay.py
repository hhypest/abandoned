"""Build the projected EQP-01 hologram. Composite moving glyphs after temporal reconstruction."""
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
uv = materials.create_material_expression(display, unreal.MaterialExpressionTextureCoordinate, -500, 150)
field = materials.create_material_expression(display, unreal.MaterialExpressionCustom, -250, 0)
field.set_editor_property("description", "EQP-01 projected light field")
inputs = []
for label in ("Ink", "PanelUV"):
    item = unreal.CustomInput()
    item.set_editor_property("input_name", label)
    inputs.append(item)
field.set_editor_property("inputs", inputs)
field.set_editor_property("code", """
float2 edge = min(PanelUV, 1.0 - PanelUV);
float border = 1.0 - smoothstep(0.003, 0.008, min(edge.x, edge.y));
float lines = 0.5 + 0.5 * cos(PanelUV.y * 48.0 * 6.2831853);
return Ink + float3(0.015, 0.19, 0.20) * (0.10 + 0.08 * lines + border);
""")
exposure = materials.create_material_expression(display, unreal.MaterialExpressionEyeAdaptationInverse, 0, 0)
if not (materials.connect_material_expressions(sample, "RGB", field, "Ink")
        and materials.connect_material_expressions(uv, "", field, "PanelUV")
        and materials.connect_material_expressions(field, "", exposure, "")
        and materials.connect_material_property(exposure, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)):
    raise RuntimeError("Could not connect the projected display")
# Keep the cuff visible through the empty field. Glyphs retain full contrast
# when the projected readout crosses the bright edge of the physical housing.
opacity = materials.create_material_expression(display, unreal.MaterialExpressionLinearInterpolate, -180, 250)
opacity.set_editor_property("const_a", 0.62)
opacity.set_editor_property("const_b", 1.0)
if not (materials.connect_material_expressions(sample, "A", opacity, "Alpha")
        and materials.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)):
    raise RuntimeError("Could not connect hologram coverage")
materials.recompile_material(display)
if not assets.save_loaded_asset(display):
    raise RuntimeError("Could not save the wrist display material")
unreal.log("WRIST_DISPLAY_READY")
