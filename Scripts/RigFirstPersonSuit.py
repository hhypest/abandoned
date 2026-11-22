"""Add the approved EQP-00 skin to an exact copy of the Meshy GLB (Python 3 + NumPy)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
from tempfile import NamedTemporaryFile

import numpy as np


SOURCE_SHA = "25ff43995cb6d6d76f57faf48700b432685ef9f1c58e8f7e0f98dac39a1465a2"

# Joint centres in the original glTF metres, X to the wearer's left and Y up.
TORSO = [
    ("root", None, (0, 0, 0)), ("pelvis", "root", (0, .105, -.025)),
    ("spine", "pelvis", (0, .310, -.033)), ("chest", "spine", (0, .535, -.043)),
]
LIMBS = [
    ("clavicle", "chest", (.062, .755, -.060)),
    ("upperarm", "clavicle", (.245, .764, -.059)),
    ("lowerarm", "upperarm", (.397, .458, -.063)),
    ("hand", "lowerarm", (.505, .202, .036)),
    ("thigh", "pelvis", (.130, .099, -.023)),
    ("calf", "thigh", (.218, -.317, .006)),
    ("foot", "calf", (.284, -.748, -.060)),
]
# From knuckle to fingertip, including a fourth landmark for skin assignment.
DIGITS = {
    "thumb":  [(.497, .151, .104), (.476, .106, .119), (.461, .071, .138), (.448, .035, .152)],
    "index":  [(.530, .100, .131), (.539, .055, .162), (.524, .012, .169), (.511, -.043, .166)],
    "middle": [(.553, .098, .104), (.556, .043, .132), (.561, -.010, .127), (.545, -.052, .119)],
    "ring":   [(.564, .100, .078), (.580, .051, .104), (.577, .008, .101), (.570, -.043, .098)],
    "pinky":  [(.567, .100, .048), (.595, .055, .072), (.591, .012, .076), (.575, -.045, .068)],
}
GLASS = np.array((.486, .336, .0705))


def smooth(x, lo, hi):
    t = np.clip((x - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def rig(vertices):
    names, parents, points = [], [], []

    def joint(name, parent, point):
        names.append(name)
        parents.append(names.index(parent) if parent else -1)
        points.append(np.array(point, dtype=float))

    for name, parent, point in TORSO:
        joint(name, parent, point)
    for side, sign in (("l", 1), ("r", -1)):
        mirror = np.array([sign, 1, 1])
        for name, parent, point in LIMBS:
            joint(f"{name}_{side}", parent if parent in ("chest", "pelvis")
                  else f"{parent}_{side}", mirror * point)
        for digit, path in DIGITS.items():
            parent = f"hand_{side}"
            for i, point in enumerate(path[:3], 1):
                name = f"{digit}_{i:02}_{side}"
                joint(name, parent, mirror * point)
                parent = name
    joint("terminal_l", "lowerarm_l", GLASS)

    x, y, z = vertices.T
    lateral = abs(x)
    # The arm/body boundaries follow the gaps below the axilla and blend into the collar.
    inner = np.interp(y, [.0, .24, .43, .58, .70, .79, .89], [.36, .325, .278, .213, .172, .164, .155])
    shoulder = smooth(lateral - inner, -.018, .023)
    shoulder *= (y > -.07)
    leg = (1 - shoulder) * (1 - smooth(y, .025, .183))
    arm = shoulder
    torso = 1 - arm - leg
    weights = np.zeros((len(vertices), len(names)), dtype=np.float32)

    def put(name, value):
        weights[:, names.index(name)] += value

    spine = smooth(y, .24, .385)
    chest = smooth(y, .47, .61)
    put("pelvis", torso * (1-spine))
    put("spine", torso * (spine-chest))
    put("chest", torso * chest)

    # Project the original screen fit into UE's axes (X,Z,Y). Joint +X is the
    # surface normal. The display's horizontal -Y follows the forearm towards the hand.
    ue_x = np.array([.2158, .9280, .3038])
    ue_x /= np.linalg.norm(ue_x)
    ue_y = np.array([-.2553, -.2467, .9349])
    ue_y -= ue_x * (ue_x @ ue_y)
    ue_y /= np.linalg.norm(ue_y)
    ue_frame = np.column_stack([ue_x, ue_y, np.cross(ue_x, ue_y)])
    permutation = [0, 2, 1]
    glass_frame = ue_frame[permutation][:, permutation]
    # This oriented volume includes the entire rigid plate, key bank and bezel,
    # and ends before either flexible cuff. A small overlap into the rigid sleeve
    # is intentional (the plate and sleeve share a single surface).
    normal, long_axis, short_axis = ((vertices-GLASS)[:, permutation] @ ue_frame).T
    plate = ((abs(long_axis) < .110) & (short_axis > -.062) & (short_axis < .082)
             & (normal > -.045) & (normal < .025) & (x > 0))

    # Tag each fingertip with its nearest authored digit centreline. Use positional
    # fields so every UV seam duplicate receives the same weights.
    mirrored = np.column_stack((lateral, y, z))
    distances = []
    for path in DIGITS.values():
        path = np.array(path)
        distance = np.full(len(y), np.inf)
        for a, b in zip(path, path[1:]):
            delta = b-a
            t = np.clip(((mirrored-a) @ delta)/(delta@delta), 0, 1)
            distance = np.minimum(distance, ((mirrored-a-t[:, None]*delta)**2).sum(axis=1))
        distances.append(distance)
    closest_digit = np.argmin(distances, axis=0)

    for side, sign in (("l", 1), ("r", -1)):
        sided = x*sign >= 0 if sign == 1 else x*sign > 0
        # Feet and legs extend farther laterally than the waist; below the fingers
        # the full silhouette belongs to the legs.
        w = leg * sided
        knee = 1-smooth(y, -.376, -.256)
        ankle = 1-smooth(y, -.783, -.716)
        put(f"thigh_{side}", w*(1-knee))
        put(f"calf_{side}", w*(knee-ankle))
        put(f"foot_{side}", w*ankle)
        w = arm * sided
        elbow = 1-smooth(y, .421, .496)
        wrist = 1-smooth(y, .178, .223)
        upper = smooth(lateral-.19 + (.755-y)*.5, -.018, .067)
        put(f"clavicle_{side}", w*(1-upper))
        put(f"upperarm_{side}", w*upper*(1-elbow))
        put(f"lowerarm_{side}", w*upper*(elbow-wrist))
        hand = w*upper*wrist
        palm = hand.copy()
        for k, (digit, path) in enumerate(DIGITS.items()):
            finger = hand * (closest_digit == k)
            joint1, joint2, joint3 = [1-smooth(y, p[1]-.011, p[1]+.011) for p in path[:3]]
            palm -= finger*joint1
            put(f"{digit}_01_{side}", finger*(joint1-joint2))
            put(f"{digit}_02_{side}", finger*(joint2-joint3))
            put(f"{digit}_03_{side}", finger*joint3)
        put(f"hand_{side}", palm)

    weights[plate] = 0
    weights[plate, names.index("terminal_l")] = 1
    if not np.allclose(weights.sum(axis=1), 1, atol=1e-5) or (weights < -1e-5).any():
        raise ValueError("Invalid skin partition")
    joints = np.argsort(-weights, axis=1, kind="stable")[:, :4]
    w = np.take_along_axis(weights, joints, axis=1)
    w /= w.sum(axis=1, keepdims=True)
    if not (np.count_nonzero(w, axis=1) <= 4).all() or plate.sum() < 500:
        raise ValueError("Invalid skin influences or rigid casing")
    return names, parents, points, glass_frame, joints.astype("u1"), w.astype("<f4"), int(plate.sum())


def write_skin(source, destination):
    if source.resolve() == destination.resolve() or (destination.exists() and source.samefile(destination)):
        raise ValueError("Write the rigged copy to a different file from its source")
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA:
        raise ValueError("Unexpected EQP-00 source revision")
    length = struct.unpack_from("<I", raw, 12)[0]
    model = json.loads(raw[20:20+length])
    payload = bytearray(raw[28+length:28+length+model["buffers"][0]["byteLength"]])
    mesh = model["meshes"][0]
    primitive = mesh["primitives"][0]
    pos = model["accessors"][primitive["attributes"]["POSITION"]]
    view = model["bufferViews"][pos["bufferView"]]
    positions = np.frombuffer(
        payload, dtype="<f4", count=pos["count"]*3, offset=view.get("byteOffset", 0)).reshape(-1, 3).copy()
    names, parents, points, glass_frame, joints, w, plate_count = rig(positions)

    def accessor(array, component_type, shape):
        while len(payload) % 4:
            payload.append(0)
        data = array.tobytes()
        ref = len(model["bufferViews"])
        model["bufferViews"].append(dict(buffer=0, byteOffset=len(payload), byteLength=len(data)))
        payload.extend(data)
        model["accessors"].append(
            dict(bufferView=ref, componentType=component_type, count=len(array), type=shape))
        return len(model["accessors"])-1

    nodes = model["nodes"]
    first = len(nodes)
    inverse = []
    for i, name in enumerate(names):
        world = np.eye(4)
        world[:3, 3] = points[i]
        if name == "terminal_l":
            world[:3, :3] = glass_frame
        inverse.append(np.linalg.inv(world).T)
        local = world.copy()
        if parents[i] >= 0:
            local[:3, 3] -= points[parents[i]]
        node = dict(name=name, matrix=local.T.flatten().tolist())
        children = [first+k for k, j in enumerate(parents) if j == i]
        if children:
            node["children"] = children
        nodes.append(node)
    primitive["attributes"]["JOINTS_0"] = accessor(joints, 5121, "VEC4")
    primitive["attributes"]["WEIGHTS_0"] = accessor(w, 5126, "VEC4")
    model["skins"] = [dict(name="EQP00_Skeleton", skeleton=first, joints=list(range(first, len(nodes))),
                           inverseBindMatrices=accessor(np.asarray(inverse, dtype="<f4"), 5126, "MAT4"))]
    nodes[0].update(name="SK_EQP00", skin=0)
    mesh["name"] = "SK_EQP00"
    model["scenes"][0]["nodes"].append(first)
    model["buffers"][0]["byteLength"] = len(payload)
    description = json.dumps(model, separators=(",", ":")).encode()
    description += b" "*(-len(description) % 4)
    payload += b"\0"*(-len(payload) % 4)
    destination.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack("<4s4I", b"glTF", 2, 28 + len(description) +
                         len(payload), len(description), 0x4e4f534a)
    skinned = header + description + struct.pack("<II", len(payload), 0x004e4942) + payload
    digest = hashlib.sha256(skinned).hexdigest()
    # Publish only complete files. A retry can reuse an identical output but never
    # replaces an existing source, working copy or a concurrent process's export.
    with NamedTemporaryFile(dir=destination.parent, delete=False) as temporary:
        pending = Path(temporary.name)
    try:
        pending.write_bytes(skinned)
        try:
            os.link(pending, destination)
        except FileExistsError:
            if hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
                raise ValueError(f"Destination contains a different asset: {destination}") from None
    finally:
        pending.unlink(missing_ok=True)
    print(f"EQP00_SKIN vertices={len(positions)} joints={len(nodes)-first} rigid_plate={plate_count} "
          f"max_weights={np.count_nonzero(w, axis=1).max()} sha256={digest}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    write_skin(args.source, args.destination)
