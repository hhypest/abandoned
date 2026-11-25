"""Inspect the owner's Fab finger poses and movement clips on their authored skeletons."""

import csv
import math
from pathlib import Path

import unreal


POSES = "/Game/Attaku/FingerPoses/Animations/60_Pose_animations"
DIGITS = ("index", "middle", "ring", "pinky")
MOVEMENTS = (
    "Crouch/anim_Crouch_Idle",
    "LedgeClimb/anim_LedgeClimb_Idle",
    "LedgeClimb/anim_LedgeClimb_ClimbUp",
    "Mantle/anim_Mantle_1M_R",
)


def curl(pose, digit, side):
    """Angle between the palm-to-knuckle direction and the middle phalanx."""
    names = [f"hand_{side}"] + [f"{digit}_0{i}_{side}" for i in (1, 2, 3)]
    wrist, base, middle, tip = [
        pose.get_bone_pose(name, unreal.AnimPoseSpaces.WORLD).translation for name in names
    ]
    palm = base - wrist
    phalanx = tip - middle
    denominator = palm.length() * phalanx.length()
    if denominator < 0.0001:
        raise ValueError(f"Degenerate finger chain {digit}_{side}")
    cosine = palm.dot(phalanx) / denominator
    return round(math.degrees(math.acos(max(-1.0, min(1.0, cosine)))), 1)


def inspect_fingers(output, options):
    columns = [(digit, side) for side in ("l", "r") for digit in DIGITS]
    rows = []
    for index in range(60):
        path = f"{POSES}/UE5_Finger_pose_{index}"
        clip = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(clip, unreal.AnimSequence):
            raise RuntimeError(f"Missing animation sequence {path}")
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_frame(clip, 0, options)
        names = set(map(str, pose.get_bone_names()))
        for digit, side in columns:
            required = {f"hand_{side}", *(f"{digit}_0{i}_{side}" for i in (1, 2, 3))}
            if not required.issubset(names):
                raise ValueError(f"{path} lacks {sorted(required - names)}")
        rows.append([index] + [curl(pose, digit, side) for digit, side in columns])

    with (output / "finger-poses.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["pose"] + [f"{digit}_{side}_deg" for digit, side in columns])
        writer.writerows(rows)
    unreal.log(f"FAB_FINGER_POSES: measured {len(rows)} two-hand poses")


def inspect_movements(output, options):
    rows = []
    bones = ("hand_l", "hand_r", "foot_l", "foot_r", "pelvis")
    for name in MOVEMENTS:
        clip = unreal.EditorAssetLibrary.load_asset(f"/Game/FreeAnimationLibrary/Animations/{name}")
        if not isinstance(clip, unreal.AnimSequence):
            raise RuntimeError(f"Missing movement clip {name}")
        keys = unreal.AnimationLibrary.get_num_keys(clip)
        if keys < 2:
            raise ValueError(f"Expected multiple pose samples in {name}")
        for frame in range(keys):
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_frame(clip, frame, options)
            if not set(bones).issubset(map(str, pose.get_bone_names())):
                raise ValueError(f"Missing hand, foot or pelvis in {name} at frame {frame}")
            left, right, foot_l, foot_r, pelvis = [
                pose.get_bone_pose(bone, unreal.AnimPoseSpaces.WORLD).translation for bone in bones
            ]
            rows.append((name, frame, round((right - left).length(), 2),
                         round(pelvis.z - (foot_l.z + foot_r.z) / 2, 2)))
    with (output / "movement.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("clip", "frame", "wrist_distance_cm", "pelvis_above_mean_ankle_cm"))
        writer.writerows(rows)
    unreal.log(f"FAB_MOVEMENTS: evaluated {len(rows)} samples across {len(MOVEMENTS)} clips")


output = Path(unreal.Paths.project_saved_dir()) / "SoldierFit"
output.mkdir(parents=True, exist_ok=True)
options = unreal.AnimPoseEvaluationOptions()
inspect_fingers(output, options)
inspect_movements(output, options)
