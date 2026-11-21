"""Partition the Meshy DEV-02 GLB at the dark groove. Keep its authored exterior and embedded maps."""
from collections import defaultdict
from copy import deepcopy
from pathlib import Path
import json
import math
import struct


def split_plate(source, destination):
    data = source.read_bytes()
    size = struct.unpack_from("<I", data, 12)[0]
    model = json.loads(data[20:20 + size])
    binary = bytearray(data[28 + size:])
    original = model["meshes"][0]["primitives"][0]

    def read(index, fmt):
        acc = model["accessors"][index]
        view = model["bufferViews"][acc["bufferView"]]
        start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = view.get("byteStride", struct.calcsize(fmt))
        return [struct.unpack_from(fmt, binary, start + i * stride) for i in range(acc["count"])]

    def append(values, fmt, shape, component, bounds=False):
        binary.extend(bytes(-len(binary) % 4))
        start = len(binary)
        for value in values:
            binary.extend(struct.pack(fmt, *value))
        views = model["bufferViews"]
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(binary) - start})
        acc = {"bufferView": len(views) - 1, "componentType": component, "count": len(values), "type": shape}
        if bounds:
            acc.update(min=list(map(min, zip(*values))), max=list(map(max, zip(*values))))
        model["accessors"].append(acc)
        return len(model["accessors"]) - 1

    pos = read(original["attributes"]["POSITION"], "<fff")
    normals = read(original["attributes"]["NORMAL"], "<fff")
    uv = read(original["attributes"]["TEXCOORD_0"], "<ff")
    indices = original["indices"]
    fmt = {5123: "<H", 5125: "<I"}[model["accessors"][indices]["componentType"]]
    idx = [v[0] for v in read(indices, fmt)]
    if len(idx) != 20770 * 3:
        raise ValueError("Recheck the groove on this revision of DEV-02")
    parts = [[], []]
    edges = defaultdict(list)
    for n in range(0, len(idx), 3):
        tri = idx[n:n + 3]
        x, y, z = [sum(pos[k][d] for k in tri) / 3 for d in range(3)]
        # GLB units, Y up. Rounded rectangle through the middle of the recessed black border.
        dx = max(abs(x) - 0.64, 0)
        dz = max(abs(z + 0.03) - 0.66, 0)
        top = y > 0.025 and dx * dx + dz * dz < 0.061 ** 2
        parts[int(top)].extend(tri)
        for a, b in zip(tri, tri[1:] + tri[:1]):
            key = tuple(sorted((tuple(round(c, 5) for c in pos[a]), tuple(round(c, 5) for c in pos[b]))))
            edges[key].append((top, a, b))

    # Stationary inward-facing lip under the cut. Its UVs sample the original groove.
    for uses in edges.values():
        if {top for top, _, _ in uses} != {True, False}:
            continue
        _, a, b = next(entry for entry in uses if not entry[0])
        ax, ay, az = pos[a]
        bx, by, bz = pos[b]
        length = math.hypot(bx - ax, bz - az)
        if length < 1e-7:
            continue
        first = len(pos)
        pos.extend([(bx, by, bz), (ax, ay, az), (ax, ay - 0.15, az), (bx, by - 0.15, bz)])
        normals.extend([((az - bz) / length, 0, (bx - ax) / length)] * 4)
        uv.extend([uv[b], uv[a], uv[a], uv[b]])
        parts[0].extend(first + i for i in (0, 1, 2, 0, 2, 3))

    model["nodes"] = []
    model["meshes"] = []
    for name, part in zip(("PlateBase", "PlateTop"), parts):
        used = list(dict.fromkeys(part))
        local = {v: i for i, v in enumerate(used)}
        prim = deepcopy(original)
        prim["attributes"] = {key: append([values[k] for k in used], f, shape, 5126, key == "POSITION")
                              for key, values, f, shape in (("POSITION", pos, "<fff", "VEC3"),
                                                            ("NORMAL", normals, "<fff", "VEC3"),
                                                            ("TEXCOORD_0", uv, "<ff", "VEC2"))}
        prim["indices"] = append([(local[k],) for k in part], "<I", "SCALAR", 5125)
        model["nodes"].append({"mesh": len(model["meshes"]), "name": name})
        model["meshes"].append({"name": name, "primitives": [prim]})
    model["scenes"][model.get("scene", 0)]["nodes"] = [0, 1]
    model["buffers"][0]["byteLength"] = len(binary)
    desc = json.dumps(model, separators=(",", ":")).encode()
    desc += b" " * (-len(desc) % 4)
    binary.extend(bytes(-len(binary) % 4))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(struct.pack("<4sII", b"glTF", 2, 28 + len(desc) + len(binary))
                            + struct.pack("<I4s", len(desc), b"JSON") + desc
                            + struct.pack("<I4s", len(binary), b"BIN\0") + binary)
