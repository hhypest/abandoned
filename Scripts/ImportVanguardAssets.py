"""Run in the UE editor. Imports the owner's doors and any locally acquired Fab maps."""
import os
import re
import tempfile
from pathlib import Path
from zipfile import ZipFile

import unreal


ROOT = "/Game/AbandonedLocal/Vanguard"
ASSETS = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MATERIALS = unreal.MaterialEditingLibrary
ARCHIVES = {
    "Frame": "Sci_Fi_Industrial_Doo.zip",
    "Leaf": "Sci_Fi_Sliding_Door_L.zip",
}


def import_asset(file, name, options=None):
    path = f"{ROOT}/{name}"
    existing = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if existing:
        return existing
    task = unreal.AssetImportTask()
    task.filename = str(file)
    task.destination_path = ROOT
    task.destination_name = name
    task.automated = True
    task.save = True
    if options:
        task.options = options
    TOOLS.import_asset_tasks([task])
    asset = next(iter(task.get_objects()), None)
    if not asset:
        raise RuntimeError(f"Import failed for {file.name}")
    if asset.get_name() != name:
        if not ASSETS.rename_asset(asset.get_path_name(), path):
            raise RuntimeError(f"Could not assign the package name {path}")
        asset = ASSETS.load_asset(path)
    return asset


def material(name, files, tiles=1.0):
    path = f"{ROOT}/M_{name}"
    if ASSETS.does_asset_exist(path):
        return ASSETS.load_asset(path)
    result = TOOLS.create_asset(f"M_{name}", ROOT, unreal.Material, unreal.MaterialFactoryNew())
    uv = MATERIALS.create_material_expression(result, unreal.MaterialExpressionTextureCoordinate, -920, 0)
    uv.set_editor_property("u_tiling", tiles)
    uv.set_editor_property("v_tiling", tiles)
    properties = {
        "BaseColor": unreal.MaterialProperty.MP_BASE_COLOR,
        "Metallic": unreal.MaterialProperty.MP_METALLIC,
        "Roughness": unreal.MaterialProperty.MP_ROUGHNESS,
        "Normal": unreal.MaterialProperty.MP_NORMAL,
        "AO": unreal.MaterialProperty.MP_AMBIENT_OCCLUSION,
    }
    for i, (channel, source) in enumerate(files.items()):
        texture = import_asset(source, f"T_{name}_{channel}")
        if channel != "BaseColor":
            texture.set_editor_property("srgb", False)
        if channel == "Normal":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        ASSETS.save_loaded_asset(texture)
        sample = MATERIALS.create_material_expression(result, unreal.MaterialExpressionTextureSample, -550, 270 * i)
        sample.set_editor_property("texture", texture)
        sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if channel == "BaseColor" else (
            unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if channel == "Normal"
            else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
        )
        sample.set_editor_property("sampler_type", sampler)
        if not MATERIALS.connect_material_expressions(uv, "", sample, "UVs"):
            raise RuntimeError(f"Could not connect UVs for {channel}")
        if not MATERIALS.connect_material_property(sample, "", properties[channel]):
            raise RuntimeError(f"Could not connect {channel}")
    MATERIALS.recompile_material(result)
    ASSETS.save_loaded_asset(result)
    return result


def fit_frame_opening(result):
    """The source surround includes a centre panel. Cut its visible opening to the passage collar."""
    if any(str(name) == "DoorOpeningHalfWidth" for name in MATERIALS.get_scalar_parameter_names(result)):
        return
    result.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)

    def node(expression, x, y, **properties):
        value = MATERIALS.create_material_expression(result, expression, x, y)
        for name, prop in properties.items():
            value.set_editor_property(name, prop)
        return value

    def bind(source, dest, pin=""):
        if not MATERIALS.connect_material_expressions(source, "", dest, pin):
            raise RuntimeError(f"Could not connect the frame opening input {pin}")

    world = node(unreal.MaterialExpressionWorldPosition, -1420, 940)
    centre = node(unreal.MaterialExpressionObjectPositionWS, -1420, 1130)
    delta = node(unreal.MaterialExpressionSubtract, -1210, 990)
    bind(world, delta, "A")
    bind(centre, delta, "B")
    width_axis = node(unreal.MaterialExpressionComponentMask, -1050, 950, r=False, g=True, b=False, a=False)
    height_axis = node(unreal.MaterialExpressionComponentMask, -1050, 1210, r=False, g=False, b=True, a=False)
    bind(delta, width_axis)
    bind(delta, height_axis)
    absolute_width = node(unreal.MaterialExpressionAbs, -890, 950)
    bind(width_axis, absolute_width)
    width = node(unreal.MaterialExpressionScalarParameter, -890, 1070,
                 parameter_name="DoorOpeningHalfWidth", default_value=100.0)
    top = node(unreal.MaterialExpressionScalarParameter, -890, 1330,
               parameter_name="DoorOpeningTop", default_value=103.0)

    def outer_edge(position, limit, y):
        difference = node(unreal.MaterialExpressionSubtract, -680, y)
        bind(position, difference, "A")
        bind(limit, difference, "B")
        sharpen = node(unreal.MaterialExpressionMultiply, -520, y, const_b=4.0)
        bind(difference, sharpen, "A")
        cap = node(unreal.MaterialExpressionSaturate, -360, y)
        bind(sharpen, cap)
        return cap

    mask = node(unreal.MaterialExpressionMax, -180, 1080)
    bind(outer_edge(absolute_width, width, 950), mask, "A")
    bind(outer_edge(height_axis, top, 1210), mask, "B")
    if not MATERIALS.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError("Could not configure the frame opening opacity")
    MATERIALS.recompile_material(result)
    ASSETS.save_loaded_asset(result)


def import_door(role, archive):
    with tempfile.TemporaryDirectory(prefix="abandoned-door-") as directory, ZipFile(archive) as package:
        scratch = Path(directory)
        files = {}
        mesh = None
        for member in package.namelist():
            basename = Path(member).name
            ext = Path(basename).suffix.lower()
            if ext not in (".fbx", ".png"):
                continue
            local = scratch / basename
            local.write_bytes(package.read(member))
            if ext == ".fbx":
                if mesh:
                    raise RuntimeError(f"Multiple FBX files in {archive.name}")
                mesh = local
            else:
                channel = next(
                    (role for role in ("Normal", "Metallic", "Roughness") if basename.lower().endswith(f"_{role.lower()}.png")),
                    "BaseColor",
                )
                if channel in files:
                    raise RuntimeError(f"Duplicate {channel} map in {archive.name}")
                files[channel] = local
        if not mesh or "BaseColor" not in files:
            raise RuntimeError(f"Missing FBX or base color in {archive.name}")
        options = unreal.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
        options.import_mesh = True
        options.import_materials = False
        options.import_textures = False
        options.static_mesh_import_data.set_editor_property("combine_meshes", True)
        options.static_mesh_import_data.set_editor_property("auto_generate_collision", False)
        options.static_mesh_import_data.set_editor_property("build_nanite", False)
        result = import_asset(mesh, f"SM_Door{role}", options)
        finish = material(f"Door{role}", files)
        if role == "Frame":
            fit_frame_opening(finish)
        result.set_material(0, finish)
        ASSETS.save_loaded_asset(result)
        unreal.log(f"VANGUARD_DOOR_READY {role}: {result.get_path_name()}")


def import_fab(name, folder):
    channels = {
        "BaseColor": r"albedo|base.?color|diffuse",
        "Normal": r"normal",
        "Roughness": r"roughness|rough",
        "AO": r"ambient.?occlusion|(?:^|[_-])ao(?:[_-]|$)",
    }
    images = sorted(p for p in folder.rglob("*") if p.suffix.lower() in {".png", ".jpg", ".jpeg", ".tga", ".exr"})
    files = {}
    for channel, pattern in channels.items():
        matches = [p for p in images if re.search(pattern, p.stem.lower())]
        if len(matches) == 1:
            files[channel] = matches[0]
        elif len(matches) > 1:
            raise RuntimeError(f"Choose one {channel} map in {folder}")
    if not {"BaseColor", "Normal", "Roughness"} <= files.keys():
        raise RuntimeError(f"Missing base color, normal or roughness in {folder}")
    material(name, files)
    unreal.log(f"VANGUARD_FAB_READY {name}")


door_directory = Path(os.environ["ABANDONED_DOOR_SOURCES"])
for door_role, archive_name in ARCHIVES.items():
    import_door(door_role, door_directory / archive_name)
if fab := os.environ.get("ABANDONED_FAB_SOURCES"):
    fab_directory = Path(fab)
    for finish in ("IndustrialFloor", "CastConcrete"):
        import_fab(finish, fab_directory / finish)
