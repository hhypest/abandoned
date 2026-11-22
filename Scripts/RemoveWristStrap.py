"""Cut the EQP-01 strap behind the casing. The importer's inventory pins the source GLB."""
from collections import defaultdict
import json
import math
import struct


def _cap(loop, positions):
    """Triangulate one simple cut contour in its XY projection, facing the removed side."""
    def cross(a, b, c):
        x, y, _ = positions[a]
        u, v, _ = positions[b]
        s, t, _ = positions[c]
        return (u - x) * (t - y) - (v - y) * (s - x)

    area = sum(cross(loop[0], b, c) for b, c in zip(loop[1:], loop[2:]))
    if area < 0:
        loop.reverse()
    while len(loop) > 3:
        for i, b in enumerate(loop):
            a, c = loop[i - 1], loop[(i + 1) % len(loop)]
            if cross(a, b, c) <= 1e-14:
                continue
            if any(min(cross(a, b, p), cross(b, c, p), cross(c, a, p)) >= -1e-14
                   for p in loop if p not in (a, b, c)):
                continue
            yield a, c, b
            loop.pop(i)
            break
        else:
            raise ValueError("EQP-01 cut contour could not be triangulated")
    yield loop[0], loop[2], loop[1]


def remove_wrist_strap(source, destination):
    data = source.read_bytes()
    size = struct.unpack_from("<I", data, 12)[0]
    model = json.loads(data[20:20 + size])
    binary = bytearray(data[28 + size:])
    prim = model["meshes"][0]["primitives"][0]
    attrs = prim["attributes"]

    def read(index, fmt):
        acc = model["accessors"][index]
        view = model["bufferViews"][acc["bufferView"]]
        start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = view.get("byteStride", struct.calcsize(fmt))
        return [struct.unpack_from(fmt, binary, start + i * stride) for i in range(acc["count"])]

    pos, normal, uv = (read(attrs[k], fmt) for k, fmt in
                       (("POSITION", "<3f"), ("NORMAL", "<3f"), ("TEXCOORD_0", "<2f")))
    source_indices = read(prim["indices"], "<I")
    if len(source_indices) != 115588 * 3:
        raise ValueError("Unexpected EQP-01 source geometry")

    # The tilted back of the casing is at z + 0.226 y >= 0.668 (GLB units).
    # This parallel plane crosses only the strap attachment, including its side lug.
    distance = [z + 0.226 * y - 0.645 for x, y, z in pos]
    crossings = {}
    boundary = defaultdict(set)
    triangles = []

    def intersect(a, b):
        key = tuple(sorted((a, b)))
        if key not in crossings:
            i, j = key
            t = distance[i] / (distance[i] - distance[j])
            crossings[key] = len(pos)
            for values in (pos, normal, uv):
                point = tuple(u + (v - u) * t for u, v in zip(values[i], values[j]))
                if values is pos:
                    # Share the exact boundary coordinates across UV splits and the cap.
                    point = tuple(round(k, 8) for k in point)
                elif values is normal:
                    point = tuple(k / math.hypot(*point) for k in point)
                values.append(point)
        return crossings[key]

    for offset in range(0, len(source_indices), 3):
        face = [v[0] for v in source_indices[offset:offset + 3]]
        polygon, seam = [], []
        for a, b in zip(face, face[1:] + face[:1]):
            if distance[a] >= 0:
                polygon.append(a)
            if (distance[a] >= 0) != (distance[b] >= 0):
                k = intersect(a, b)
                polygon.append(k)
                seam.append(pos[k])
        if seam:
            a, b = seam
            boundary[a].add(b)
            boundary[b].add(a)
        triangles.extend((polygon[0], a, b) for a, b in zip(polygon[1:], polygon[2:]))

    # UV seams can duplicate vertices at the same position. Join only the cut contour,
    # keeping the original UV splits and authored normals on all exterior triangles.
    if any(len(neighbours) != 2 for neighbours in boundary.values()):
        raise ValueError("Expected closed contours at the EQP-01 strap attachment")
    remaining = set(boundary)
    cut_normal = tuple(k / math.hypot(1, 0.226) for k in (0, -0.226, -1))
    contours = 0
    while remaining:
        point = min(remaining)
        previous = None
        loop = []
        while point in remaining:
            remaining.remove(point)
            loop.append(len(pos))
            x, y, z = point
            pos.append(point)
            normal.append(cut_normal)
            # Small opaque steel patch on the original back plate's UV island.
            uv.append((0.268 + y * 0.004, 0.541 + x * 0.004))
            previous, point = point, min(n for n in boundary[point] if n != previous)
        triangles.extend(_cap(loop, pos))
        contours += 1
    if contours != 2:
        raise ValueError("Expected two separate attachment contours")

    used = list(dict.fromkeys(i for face in triangles for i in face))
    local = {original: i for i, original in enumerate(used)}
    # Repack the embedded maps byte-for-byte, omitting the superseded geometry buffers.
    original_binary = binary
    binary = bytearray()
    for image in model["images"]:
        view = model["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        binary.extend(bytes(-len(binary) % 4))
        view["byteOffset"] = len(binary)
        binary.extend(original_binary[start:start + view["byteLength"]])

    def append(index, values, fmt):
        acc = model["accessors"][index]
        binary.extend(bytes(-len(binary) % 4))
        start = len(binary)
        for value in values:
            binary.extend(struct.pack(fmt, *value))
        view = model["bufferViews"][acc["bufferView"]]
        view.update(byteOffset=start, byteLength=len(binary) - start)
        acc.update(count=len(values))
        if "min" in acc:
            acc.update(min=list(map(min, zip(*values))), max=list(map(max, zip(*values))))

    for key, values, fmt in (("POSITION", pos, "<3f"), ("NORMAL", normal, "<3f"), ("TEXCOORD_0", uv, "<2f")):
        append(attrs[key], [values[i] for i in used], fmt)
    append(prim["indices"], [(local[i],) for face in triangles for i in face], "<I")
    model["buffers"][0]["byteLength"] = len(binary)
    desc = json.dumps(model, separators=(",", ":")).encode()
    desc += b" " * (-len(desc) % 4)
    binary.extend(bytes(-len(binary) % 4))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(struct.pack("<4sII", b"glTF", 2, 28 + len(desc) + len(binary))
                           + struct.pack("<I4s", len(desc), b"JSON") + desc
                           + struct.pack("<I4s", len(binary), b"BIN\0") + binary)
