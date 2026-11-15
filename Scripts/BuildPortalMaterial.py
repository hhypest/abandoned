"""Build the project-owned portal viewport material in the Unreal editor."""

import unreal


ROOT = "/Game/Abandoned/Materials"
NAME = "M_PortalView"
MATERIALS = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

material = ASSETS.load_asset(f"{ROOT}/{NAME}") if ASSETS.does_asset_exist(f"{ROOT}/{NAME}") else None
if material:
    MATERIALS.delete_all_material_expressions(material)
else:
    material = TOOLS.create_asset(NAME, ROOT, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)
screen = MATERIALS.create_material_expression(material, unreal.MaterialExpressionScreenPosition, -620, 0)
sample = MATERIALS.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -330, 0)
default = ASSETS.load_asset("/Engine/EngineResources/DefaultTexture")
sample.set_editor_property("texture", default)
sample.set_editor_property("parameter_name", "PortalView")
sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if default.srgb
                           else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
if not MATERIALS.connect_material_expressions(screen, "ViewportUV", sample, "UVs"):
    raise RuntimeError("Could not connect viewport coordinates")
if not MATERIALS.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError("Could not connect the captured view")
MATERIALS.recompile_material(material)
if not ASSETS.save_loaded_asset(material):
    raise RuntimeError("Could not save portal material")
unreal.log("PORTAL_MATERIAL_READY")
