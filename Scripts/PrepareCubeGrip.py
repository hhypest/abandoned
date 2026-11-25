"""Transfer Fab finger pose 45 to the selected local character's reference frames."""
import unreal

assets = unreal.EditorAssetLibrary
custom = unreal.SystemLibrary.parse_param(unreal.SystemLibrary.get_command_line(), "CustomMannyFit")
mesh_path = ("/Game/AbandonedLocal/CustomMannyV2/CustomPawn" if custom else
             "/Game/Abandoned/Meshy/EQP_00/SK_EQP00/SkeletalMeshes/SK_EQP00")
clip = assets.load_asset("/Game/Attaku/FingerPoses/Animations/60_Pose_animations/UE5_Finger_pose_45")
mesh = assets.load_asset(mesh_path)
if not isinstance(clip, unreal.AnimSequence) or not isinstance(mesh, unreal.SkeletalMesh):
    raise RuntimeError(f"Install 60 Finger Poses for UE 5.7 and prepare {mesh_path}.")
source = unreal.AnimPoseExtensions.get_anim_pose_at_frame(clip, 0, unreal.AnimPoseEvaluationOptions())
target = mesh.skeleton.get_reference_pose()
world = unreal.AnimPoseSpaces.WORLD
local = unreal.AnimPoseSpaces.LOCAL
manny_digits = {
    "index": ("Index_01", "IndexFinger2", "IndexFinger3"),
    "middle": ("MiddleFinger1", "MiddleFinger2", "MiddleFinger3"),
    "ring": ("RingFinger1", "RingFinger2", "RingFinger3"),
    "pinky": ("LittleFinger1", "LittleFinger2", "LittleFinger3"),
    "thumb": ("Thumb0", "Thumb1", "Thumb2"),
}


def palm_basis(pose, wrist, middle, index, pinky):
    def point(name):
        return pose.get_ref_bone_pose(name, world).translation
    return unreal.MathLibrary.make_rot_from_xy(point(middle) - point(wrist), point(pinky) - point(index)).quaternion()


target_names = {str(name).lower() for name in target.get_bone_names()}
rotations = {}
for side, wrist in (("l", "Left-wrist"), ("r", "Right-wrist")):
    source_basis = palm_basis(source, f"hand_{side}", f"middle_01_{side}", f"index_01_{side}", f"pinky_01_{side}")
    suffix = side.upper()
    target_basis = (palm_basis(target, wrist, f"MiddleFinger1_{suffix}", f"Index_01_{suffix}", f"LittleFinger1_{suffix}")
                    if custom else palm_basis(target, f"hand_{side}", f"middle_01_{side}", f"index_01_{side}", f"pinky_01_{side}"))
    mapping = target_basis * source_basis.inversed()
    for digit, names in manny_digits.items():
        for joint, name in enumerate(names, start=1):
            bone = f"{digit}_0{joint}_{side}"
            dest = f"{name}_{suffix}" if custom else bone
            if dest.lower() not in target_names:
                raise ValueError(f"Missing finger joint {dest} in {mesh_path}")
            delta = source.get_ref_bone_pose(bone, local).rotation.inversed() * source.get_bone_pose(bone, local).rotation
            basis = (target.get_ref_bone_pose(dest, world).rotation.inversed() * mapping
                     * source.get_ref_bone_pose(bone, world).rotation)
            rotations[dest] = (target.get_ref_bone_pose(dest, local).rotation * basis * delta * basis.inversed()).normalized()

folder = "/Game/AbandonedLocal/CubeGrip"
name = "DA_CustomManny_Pose45" if custom else "DA_EQP00_Pose45"
path = f"{folder}/{name}"
if assets.does_asset_exist(path):
    output = assets.load_asset(path)
else:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CubeGripPose)
    output = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.CubeGripPose, factory)
output.set_editor_property("finger_rotations", rotations)
if not assets.save_loaded_asset(output, False):
    raise RuntimeError("Could not save the local grip pose")
unreal.log(f"CUBE_GRIP_READY {path} joints={len(rotations)}")
