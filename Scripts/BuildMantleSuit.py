"""Build the original first-person mantle gloves and sleeve in centimeters."""

from math import cos, pi, sin
from pathlib import Path

import unreal


ROOT = "/Game/Abandoned/Suit"
ASSETS = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
FABRIC = (0.053, 0.077, 0.095, 0.84)
RUBBER = (0.024, 0.033, 0.042, 0.92)
PLATE = (0.25, 0.31, 0.34, 0.47)
SEAM = (0.095, 0.13, 0.15, 0.68)
STRIPE = (0.67, 0.23, 0.045, 0.58)


class Mesh:
    def __init__(self):
        self.vertices = []
        self.triangles = []
        self.face_uvs = []
        self.colors = []

    def point(self, xyz, color):
        index = len(self.vertices)
        self.vertices.append(xyz)
        self.colors.append(color)
        return index

    def face(self, indices, uv):
        self.triangles.append(indices)
        self.face_uvs.append(uv)

    def tube(self, rows, color, sides=16):
        # Each row specifies x, center y/z, and horizontal/vertical radii.
        first = len(self.vertices)
        for row, (x, y, z, half_width, half_height) in enumerate(rows):
            for side in range(sides):
                angle = side * 2.0 * pi / sides
                shade = 0.91 + 0.09 * sin(1.2 + side * 3.1 + row * 2.0)
                tint = (*[channel * shade for channel in color[:3]], color[3])
                self.point((x, y + half_width * cos(angle), z + half_height * sin(angle)), tint)
        for row in range(len(rows) - 1):
            u0 = row / (len(rows) - 1)
            u1 = (row + 1) / (len(rows) - 1)
            for side in range(sides):
                a = first + row * sides + side
                b = first + row * sides + (side + 1) % sides
                v0 = side / sides
                v1 = (side + 1) / sides
                self.face((a, b, a + sides), ((u0, v0), (u0, v1), (u1, v0)))
                self.face((b, b + sides, a + sides), ((u0, v1), (u1, v1), (u1, v0)))
        front = self.point(rows[0][:3], color)
        back = self.point(rows[-1][:3], color)
        last = first + (len(rows) - 1) * sides
        cap_uvs = [(0.5 + 0.5 * cos(side * 2.0 * pi / sides),
                    0.5 + 0.5 * sin(side * 2.0 * pi / sides)) for side in range(sides)]
        for side in range(sides):
            next_side = (side + 1) % sides
            self.face((front, first + next_side, first + side), ((0.5, 0.5), cap_uvs[next_side], cap_uvs[side]))
            self.face((back, last + side, last + next_side), ((0.5, 0.5), cap_uvs[side], cap_uvs[next_side]))

    def mirror(self):
        self.vertices = [(x, -y, z) for x, y, z in self.vertices]
        self.triangles = [(a, c, b) for a, b, c in self.triangles]
        self.face_uvs = [(a, c, b) for a, b, c in self.face_uvs]

    def export_fbx(self, path):
        # FBX uses right-handed coordinates and a negative last index per triangle.
        positions = ",".join(f"{value:.5f}" for x, y, z in self.vertices for value in (x, -y, z))
        indices = ",".join(str(value) for a, b, c in self.triangles for value in (a, c, -b - 1))
        colors = ",".join(f"{value:.5f}" for rgba in self.colors for value in rgba)
        uv = ",".join(f"{value:.5f}" for a, b, c in self.face_uvs for pair in (a, c, b) for value in pair)
        smooth = ",".join("1" for _ in self.triangles)
        path.write_text(f"""; FBX 7.4.0 project file
FBXHeaderExtension: {{
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}}
GlobalSettings: {{
    Version: 1000
    Properties70: {{
        P: "UpAxis", "int", "Integer", "",2
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",1
        P: "FrontAxisSign", "int", "Integer", "",-1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }}
}}
Documents: {{
    Count: 1
    Document: 3, "Scene", "Scene" {{
        RootNode: 0
    }}
}}
Definitions: {{
    Version: 100
    Count: 2
    ObjectType: "Geometry" {{
        Count: 1
    }}
    ObjectType: "Model" {{
        Count: 1
    }}
}}
Objects: {{
    Geometry: 1, "Geometry::{path.stem}", "Mesh" {{
        GeometryVersion: 124
        Vertices: *{len(self.vertices) * 3} {{
            a: {positions}
        }}
        PolygonVertexIndex: *{len(self.triangles) * 3} {{
            a: {indices}
        }}
        LayerElementColor: 0 {{
            Version: 101
            Name: "Suit finish"
            MappingInformationType: "ByVertice"
            ReferenceInformationType: "Direct"
            Colors: *{len(self.colors) * 4} {{
                a: {colors}
            }}
        }}
        LayerElementSmoothing: 0 {{
            Version: 102
            Name: ""
            MappingInformationType: "ByPolygon"
            ReferenceInformationType: "Direct"
            Smoothing: *{len(self.triangles)} {{
                a: {smooth}
            }}
        }}
        LayerElementUV: 0 {{
            Version: 101
            Name: "UVChannel_1"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            UV: *{len(self.face_uvs) * 6} {{
                a: {uv}
            }}
        }}
        Layer: 0 {{
            Version: 100
            LayerElement: {{
                Type: "LayerElementColor"
                TypedIndex: 0
            }}
            LayerElement: {{
                Type: "LayerElementSmoothing"
                TypedIndex: 0
            }}
            LayerElement: {{
                Type: "LayerElementUV"
                TypedIndex: 0
            }}
        }}
    }}
    Model: 2, "Model::{path.stem}", "Mesh" {{
        Version: 232
        Shading: T
        Culling: "CullingOff"
    }}
}}
Connections: {{
    C: "OO",1,2
    C: "OO",2,0
}}
""", encoding="ascii")


def glove():
    mesh = Mesh()
    mesh.tube([(-8, 0, 2.5, 2.95, 2.0), (-7, 0, 2.65, 3.15, 2.12),
               (-4, 0, 2.65, 3.35, 2.0), (-1, 0, 2.6, 4.08, 1.9),
               (3, 0.05, 2.6, 4.0, 1.63), (5.2, 0.25, 2.5, 3.69, 1.38)], FABRIC)
    mesh.tube([(-6.5, 0, 2.65, 3.3, 2.24), (-5.7, 0, 2.65, 3.31, 2.25),
               (-5.3, 0, 2.65, 3.18, 2.08)], STRIPE)
    mesh.tube([(-3.7, 0.4, 3.92, 1.85, 0.63), (-2.5, 0.3, 3.91, 2.82, 1.0),
               (1.4, 0.25, 3.96, 3.0, 1.0), (3.8, 0.35, 3.8, 2.7, 0.55)], PLATE)
    for y, length, width in [(-2.75, 10.9, 0.98), (-0.75, 11.7, 0.97),
                             (1.2, 11.0, 0.9), (2.95, 9.4, 0.79)]:
        mesh.tube([(3.2, y, 2.8, width, 1.24), (5.3, y, 2.77, width, 1.32),
                   (7.0, y, 2.65, width * 0.95, 1.13),
                   (length - 1.7, y, 1.9, width * 0.91, 1.03),
                   (length - 0.45, y, 1.47, width * 0.79, 0.97),
                   (length, y, 1.37, width * 0.42, 0.71)], RUBBER, 12)
        mesh.tube([(4.0, y, 3.7, width * 0.62, 0.51), (4.65, y, 3.77, width * 0.9, 0.64),
                   (5.85, y, 3.71, width * 0.83, 0.61), (6.3, y, 3.54, width * 0.63, 0.36)],
                  SEAM, 12)
    mesh.tube([(-2.8, -2.75, 2.46, 1.28, 1.4), (-1.2, -4.0, 2.42, 1.28, 1.39),
               (1.0, -5.05, 2.14, 1.13, 1.17), (3.55, -5.66, 1.78, 0.99, 1.06),
               (5.0, -5.53, 1.51, 0.55, 0.74)], RUBBER)
    return mesh


def sleeve():
    mesh = Mesh()
    mesh.tube([(0, 0, 0, 3.06, 2.25), (1.4, 0, 0, 3.32, 2.73), (5, 0, 0, 3.49, 2.7),
               (8, 0, 0.04, 3.65, 2.82), (13.5, 0, 0.06, 4.12, 3.25),
               (20, 0, 0, 4.38, 3.56), (26, 0, 0, 4.4, 3.65)], FABRIC, 24)
    mesh.tube([(1.4, 0, 0, 3.38, 2.8), (1.83, 0, 0, 3.52, 2.94),
               (3.35, 0, 0, 3.57, 2.96), (3.75, 0, 0, 3.47, 2.82)], RUBBER, 24)
    mesh.tube([(3.85, 0, 0, 3.5, 2.81), (4.2, 0, 0, 3.57, 2.91),
               (4.97, 0, 0, 3.59, 2.91), (5.4, 0, 0, 3.54, 2.76)], STRIPE, 24)
    mesh.tube([(7.7, 0, 2.23, 1.3, 0.68), (8.9, 0, 2.57, 2.15, 0.86),
               (13.8, 0, 2.95, 2.45, 0.91), (16.6, 0, 3.01, 1.85, 0.63)], SEAM)
    return mesh


def material():
    name = "M_MantleSuit"
    path = f"{ROOT}/{name}"
    asset = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if asset:
        EDIT.delete_all_material_expressions(asset)
    else:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    paint = EDIT.create_material_expression(asset, unreal.MaterialExpressionVertexColor, -320, 0)
    roughness = EDIT.create_material_expression(asset, unreal.MaterialExpressionConstant, -320, 140)
    roughness.set_editor_property("r", 0.73)
    if (not EDIT.connect_material_property(paint, "", unreal.MaterialProperty.MP_BASE_COLOR)
            or not EDIT.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)):
        raise RuntimeError("Could not connect suit colors and roughness.")
    EDIT.recompile_material(asset)
    if not ASSETS.save_loaded_asset(asset):
        raise RuntimeError("Could not save the mantle suit material.")
    return asset


def save_mesh(name, model, finish):
    path = f"{ROOT}/{name}"
    folder = Path(unreal.Paths.project_saved_dir()).resolve() / "GeneratedMantleSuit"
    folder.mkdir(parents=True, exist_ok=True)
    source = folder / f"{name}.fbx"
    model.export_fbx(source)
    options = unreal.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    mesh_options = options.get_editor_property("static_mesh_import_data")
    mesh_options.set_editor_property("auto_generate_collision", False)
    mesh_options.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    task = unreal.AssetImportTask()
    for prop, value in {"filename": str(source), "destination_path": ROOT, "automated": True,
                        "replace_existing": True, "replace_existing_settings": True,
                        "options": options, "factory": unreal.FbxFactory()}.items():
        task.set_editor_property(prop, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = ASSETS.load_asset(path)
    if not isinstance(asset, unreal.StaticMesh) or not task.get_objects():
        raise RuntimeError(f"Could not generate {name}.")
    asset.set_material(0, finish)
    if not ASSETS.save_loaded_asset(asset):
        raise RuntimeError(f"Could not save {name}.")
    unreal.log(f"MANTLE_SUIT_READY {name} {len(model.triangles)} triangles")


if __name__ == "__main__":
    finish = material()
    save_mesh("SM_MantleGlove_R", glove(), finish)
    left = glove()
    left.mirror()
    save_mesh("SM_MantleGlove_L", left, finish)
    save_mesh("SM_MantleSleeve", sleeve(), finish)
