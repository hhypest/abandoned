"""Splice the suit's own right forearm into the left sleeve between the two textile collars."""
from collections import defaultdict
import numpy as np


def replace_terminal_sleeve(positions, normals, uvs, faces):
    # Planes in source metres. The gap is the narrow stitched band between the
    # original collar and the reflected replacement. The right arm is untouched.
    wrist, elbow, stitch = 0.2123, 0.4731, 0.0015
    attributes = np.column_stack((positions, normals, uvs)).astype(np.float64)
    mirrored = attributes.copy()
    mirrored[:, (0, 3)] *= -1
    vertices = list(np.concatenate((attributes, mirrored)))
    retained = []
    crossings = {}
    contour = defaultdict(list)

    def clip(face, height, above, boundary):
        result = []
        intersections = []
        for a, b in zip(face, face[1:] + face[:1]):
            inside_a = (vertices[a][1] > height) == above
            inside_b = (vertices[b][1] > height) == above
            if inside_a:
                result.append(a)
            if inside_a != inside_b:
                i, j = sorted((a, b))
                key = (i, j, height)
                if key not in crossings:
                    p, q = vertices[i], vertices[j]
                    t = (height - p[1]) / (q[1] - p[1])
                    point = p + t * (q-p)
                    point[:3] = np.round(point[:3], 8)
                    point[3:6] /= np.linalg.norm(point[3:6])
                    crossings[key] = len(vertices)
                    vertices.append(point)
                v = crossings[key]
                intersections.append(v)
                result.append(v)
        if len(intersections) == 2:
            # Record seam in output triangle order so closure has matching winding.
            a, b = intersections
            n = len(result)
            if result[(result.index(a)+1) % n] != b:
                a, b = b, a
            contour[boundary].append((a, b))
        retained.extend((result[0], a, b) for a, b in zip(result[1:], result[2:]))

    # Source is pinned by its SHA. Left sleeve is clear of the torso between
    # both planes. Only faces of the left graft get cut; other source faces
    # keep their exact winding, vertex attributes, and material.
    corners = positions[faces]
    bottom, top = corners[:, :, 1].min(1), corners[:, :, 1].max(1)
    left = (corners[:, :, 0].mean(1) > .27) & (top > wrist) & (bottom < elbow)
    retained.extend(faces[~left].tolist())
    for face in faces[left & (bottom < wrist)].tolist():
        clip(face, wrist, False, "lower_host")
    for face in faces[left & (top > elbow)].tolist():
        clip(face, elbow, True, "upper_host")
    lo, hi = wrist + stitch, elbow - stitch
    right = (corners[:, :, 0].mean(1) < -.27) & (top > lo) & (bottom < hi)
    reversed_faces = faces[:, ::-1] + len(positions)
    retained.extend(reversed_faces[right & (bottom >= lo) & (top <= hi)].tolist())
    for face in reversed_faces[right & (bottom < lo)].tolist():
        clip(face, lo, True, "lower_graft")
    for face in reversed_faces[right & (top > hi)].tolist():
        clip(face, hi, False, "upper_graft")

    def loop(boundary):
        # UV islands have split indices. Join by cut position only for tracing
        # the band; all source UVs and hard normals remain split on the mesh.
        links = {}
        for a, b in contour[boundary]:
            start, end = (tuple(vertices[k][:3]) for k in (a, b))
            if start == end:
                continue
            if start in links:
                raise ValueError(f"Branch in {boundary} cut")
            links[start] = (end, a)
        start = max(links)  # outermost arm point
        pos = start
        ring = []
        while pos in links:
            pos, index = links.pop(pos)
            ring.append(index)
        if pos != start or links or len(ring) < 16:
            raise ValueError(f"Expected one closed {boundary} collar contour")
        return ring

    def perimeter(ring):
        points = np.array([vertices[i][:3] for i in ring + ring[:1]])
        t = np.r_[0., np.cumsum(np.linalg.norm(np.diff(points, axis=0), axis=1))]
        return t / t[-1]

    for band in ("lower", "upper"):
        host = loop(band+"_host")
        graft = loop(band+"_graft")[::-1]
        start = min(range(len(graft)), key=lambda k: np.linalg.norm(vertices[host[0]][:3]-vertices[graft[k]][:3]))
        graft = graft[start:] + graft[:start]
        lengths_h, lengths_g = perimeter(host), perimeter(graft)
        # Give the new stitch its own narrow island. Sample one dark textile
        # point on the host collar, avoiding interpolation across UV islands.
        patch_uv = vertices[host[0]][6:].copy()
        def seam_vertex(index, along, across):
            v = vertices[index].copy()
            v[6:] = patch_uv + np.array((along * .00075, across * .00050))
            vertices.append(v)
            return len(vertices)-1
        host = [seam_vertex(i, t, 0) for i, t in zip(host, lengths_h)]
        graft = [seam_vertex(i, t, 1) for i, t in zip(graft, lengths_g)]
        i = j = 0
        while i < len(host) or j < len(graft):
            a, b = host[i % len(host)], graft[j % len(graft)]
            if j == len(graft) or (i < len(host) and lengths_h[i+1] <= lengths_g[j+1]):
                i += 1
                retained.append((a, b, host[i % len(host)]))
            else:
                j += 1
                retained.append((a, b, graft[j % len(graft)]))
    used, indices = np.unique(np.asarray(retained, dtype="<u4"), return_inverse=True)
    vertex = np.asarray(vertices, dtype="<f4")[used]
    print(f"EQP00_GRAFT removed_left_faces={left.sum()} donor_source_faces={right.sum()} "
          f"triangles={len(retained)} vertices={len(vertex)}")
    return vertex[:, :3], vertex[:, 3:6], vertex[:, 6:], indices.reshape(-1, 3).astype("<u4")
