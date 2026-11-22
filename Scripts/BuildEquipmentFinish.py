"""Build the first-person PBR finish for the suit and casing in UE 5.7."""
from pathlib import Path
import random
import struct
import unreal as ue


assets = ue.EditorAssetLibrary
edit = ue.MaterialEditingLibrary
tools = ue.AssetToolsHelpers.get_asset_tools()
folder = "/Game/Abandoned/Suit"
suit = "/Game/Abandoned/Meshy/EQP_00/SK_EQP00"
case = "/Game/Abandoned/Meshy/EQP_01/EQP-01"


def finish_tile():
    # Periodic height field. RG encode its two slopes, B encodes roughness variation.
    size = 128
    generator = random.Random(1101)
    height = [generator.uniform(-1, 1) for _ in range(size * size)]

    def sample(x, y):
        return height[(y % size) * size + x % size]

    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            dx = (sample(x + 1, y) - sample(x - 1, y)) / 2
            dy = (sample(x, y + 1) - sample(x, y - 1)) / 2
            pixels.extend(round(127.5 * (v + 1)) for v in (sample(x, y), dy, dx))
    temp = Path(ue.Paths.convert_relative_path_to_full(ue.Paths.project_saved_dir()), "T_FosTechGrain.tga")
    temp.write_bytes(struct.pack("<BBB5xHHHHBB", 0, 0, 2, 0, 0, size, size, 24, 0x20) + pixels)
    name = temp.stem
    path = f"{folder}/{name}"
    if not assets.does_asset_exist(path):
        ue.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
        task = ue.AssetImportTask()
        task.filename, task.destination_path, task.automated, task.save = str(temp), folder, True, False
        tools.import_asset_tasks([task])
    tile = assets.load_asset(path)
    tile.set_editor_property("srgb", False)
    tile.set_editor_property("compression_settings", ue.TextureCompressionSettings.TC_BC7)
    assets.save_loaded_asset(tile)
    return tile


def texture_setup(source):
    result = {}
    for name, group in (("normal", ue.TextureGroup.TEXTUREGROUP_CHARACTER_NORMAL_MAP),
                        ("texture_0", ue.TextureGroup.TEXTUREGROUP_CHARACTER),
                        ("texture_0_metallic_roughness", ue.TextureGroup.TEXTUREGROUP_CHARACTER_SPECULAR)):
        tex = assets.load_asset(f"{source}/Textures/{name}")
        settings = {"lod_group": group, "filter": ue.TextureFilter.TF_DEFAULT}
        if name == "texture_0":
            settings.update(compression_settings=ue.TextureCompressionSettings.TC_BC7,
                            mip_gen_settings=ue.TextureMipGenSettings.TMGS_SHARPEN1)
        for key, value in settings.items():
            if tex.get_editor_property(key) != value:
                tex.set_editor_property(key, value)
        assets.save_loaded_asset(tex)
        result[name] = tex
    return result


def wire(src, output, dst, pin):
    if not edit.connect_material_expressions(src, output, dst, pin):
        raise RuntimeError(f"Failed to link {src} to {dst}.{pin}")


def output(node, pin, prop):
    if not edit.connect_material_property(node, pin, prop):
        raise RuntimeError(f"Failed to link {node} to {prop}")


tile = finish_tile()
# Neutral engine defaults avoid pulling one item's full texture set into the other.
name = "M_FosTechEquipment"
path = f"{folder}/{name}"
if assets.does_asset_exist(path):
    mat = assets.load_asset(path)
    edit.delete_all_material_expressions(mat)
else:
    mat = tools.create_asset(name, folder, ue.Material, ue.MaterialFactoryNew())
mat.set_editor_property("two_sided", True)
edit.set_material_usage(mat, ue.MaterialUsage.MATUSAGE_SKELETAL_MESH)
edit.set_material_usage(mat, ue.MaterialUsage.MATUSAGE_NANITE)


def node(cls, x, y):
    return edit.create_material_expression(mat, cls, x, y)


def sample(name, default, kind, y):
    if not isinstance(default, ue.Texture2D):
        raise RuntimeError(f"Missing default for {name}")
    n = node(ue.MaterialExpressionTextureSampleParameter2D, -600, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("texture", default)
    n.set_editor_property("sampler_type", kind)
    return n


st = ue.MaterialSamplerType
props = ue.MaterialProperty
color = sample("BaseColorTexture", assets.load_asset("/Engine/EngineResources/DefaultTexture"), st.SAMPLERTYPE_COLOR, -480)
normal = sample("NormalTexture", ue.load_object(None, "/Engine/EngineMaterials/FlatNormal.FlatNormal"), st.SAMPLERTYPE_NORMAL, -200)
packed = sample("MetallicRoughnessTexture", tile, st.SAMPLERTYPE_LINEAR_COLOR, 70)
grain = node(ue.MaterialExpressionTextureSample, -600, 390)
grain.set_editor_property("texture", tile)
grain.set_editor_property("sampler_type", st.SAMPLERTYPE_LINEAR_COLOR)
uv = node(ue.MaterialExpressionTextureCoordinate, -1220, 450)
repeats = node(ue.MaterialExpressionScalarParameter, -1220, 590)
repeats.set_editor_property("parameter_name", "GrainTiling")
repeats.set_editor_property("default_value", 32.0)
mul = node(ue.MaterialExpressionMultiply, -930, 490)
wire(uv, "", mul, "A")
wire(repeats, "", mul, "B")
wire(mul, "", grain, "UVs")

# Change only reflected light. Mip filtering averages both slopes and roughness to zero at distance.
finish = node(ue.MaterialExpressionCustom, -230, 40)
finish.set_editor_property("description", "Fine finish on non-metallic surfaces")
finish.set_editor_property("output_type", ue.CustomMaterialOutputType.CMOT_FLOAT4)
pins = []
for name in ("BaseNormal", "MetalRough", "Grain"):
    pin = ue.CustomInput()
    pin.set_editor_property("input_name", name)
    pins.append(pin)
finish.set_editor_property("inputs", pins)
finish.set_editor_property("code", """
float coating = 1.0 - saturate(MetalRough.b);
float3 detail = Grain * 2.0 - 1.0;
float3 n = normalize(float3(BaseNormal.xy + detail.xy * (0.09 * coating), BaseNormal.z));
return float4(n, saturate(MetalRough.g + detail.b * (0.055 * coating)));
""")
wire(normal, "RGB", finish, "BaseNormal")
wire(packed, "RGB", finish, "MetalRough")
wire(grain, "RGB", finish, "Grain")
output(color, "RGB", props.MP_BASE_COLOR)
output(packed, "B", props.MP_METALLIC)
for channels, prop, y in ((("r","g","b"), props.MP_NORMAL, 10), (("a",), props.MP_ROUGHNESS, 190)):
    mask = node(ue.MaterialExpressionComponentMask, 10, y)
    for channel in ("r","g","b","a"):
        mask.set_editor_property(channel, channel in channels)
    wire(finish, "", mask, "")
    output(mask, "", prop)
edit.recompile_material(mat)
assets.save_loaded_asset(mat)
for source, repeats in ((suit, 128.0), (case, 28.0)):
    maps = texture_setup(source)
    instance = assets.load_asset(f"{source}/Materials/BakedMaterial")
    edit.set_material_instance_parent(instance, mat)
    for parameter, tex in (("NormalTexture", "normal"), ("BaseColorTexture", "texture_0"),
                           ("MetallicRoughnessTexture", "texture_0_metallic_roughness")):
        edit.set_material_instance_texture_parameter_value(instance, parameter, maps[tex])
    edit.set_material_instance_scalar_parameter_value(instance, "GrainTiling", repeats)
    edit.update_material_instance(instance)
    assets.save_loaded_asset(instance)
ue.log("FOSTECH_EQUIPMENT_SURFACES_READY")
