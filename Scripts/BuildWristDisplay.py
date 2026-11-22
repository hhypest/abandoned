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
uv = materials.create_material_expression(display, unreal.MaterialExpressionTextureCoordinate, -500, 150)
fresnel = materials.create_material_expression(display, unreal.MaterialExpressionFresnel, -500, 300)
coating = materials.create_material_expression(display, unreal.MaterialExpressionCustom, -250, 0)
coating.set_editor_property("description", "EQP-01 anti-reflective cover glass")
inputs = []
for label in ("Ink", "GlassUV", "Grazing"):
    item = unreal.CustomInput()
    item.set_editor_property("input_name", label)
    inputs.append(item)
coating.set_editor_property("inputs", inputs)
coating.set_editor_property("code", """
float2 edgeDistance = min(GlassUV, 1.0 - GlassUV);
float rim = 1.0 - smoothstep(0.0, 0.04, min(edgeDistance.x, edgeDistance.y));
float upperReflection = exp2(-5.0 * GlassUV.y);
float3 cover = float3(0.055, 0.095, 0.100) *
               (0.12 + 0.35 * upperReflection + 0.32 * rim + 0.7 * Grazing);
return Ink + cover;
""")
exposure = materials.create_material_expression(display, unreal.MaterialExpressionEyeAdaptationInverse, 0, 0)
if not (materials.connect_material_expressions(sample, "RGB", coating, "Ink")
        and materials.connect_material_expressions(uv, "", coating, "GlassUV")
        and materials.connect_material_expressions(fresnel, "", coating, "Grazing")
        and materials.connect_material_expressions(coating, "", exposure, "")
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
