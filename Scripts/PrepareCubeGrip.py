"""Transfer Fab finger pose 45 into the local EQP-00 reference frames."""
import unreal

assets = unreal.EditorAssetLibrary
clip = assets.load_asset("/Game/Attaku/FingerPoses/Animations/60_Pose_animations/UE5_Finger_pose_45")
mesh = assets.load_asset("/Game/Abandoned/Meshy/EQP_00/SK_EQP00/SkeletalMeshes/SK_EQP00")
if not isinstance(clip, unreal.AnimSequence) or not isinstance(mesh, unreal.SkeletalMesh):
    raise RuntimeError("Install the 5.7 release of 60 Finger Poses and pull the EQP-00 assets.")
source = unreal.AnimPoseExtensions.get_anim_pose_at_frame(clip, 0, unreal.AnimPoseEvaluationOptions())
target = mesh.skeleton.get_reference_pose()
world = unreal.AnimPoseSpaces.WORLD
local = unreal.AnimPoseSpaces.LOCAL


def palm_basis(pose, side):
    def point(name):
        return pose.get_ref_bone_pose(f"{name}_{side}", world).translation
    along = point("middle_01") - point("hand")
    across = point("pinky_01") - point("index_01")
    return unreal.MathLibrary.make_rot_from_xy(along, across).quaternion()


rotations = {}
for side in ("l", "r"):
    mapping = palm_basis(target, side) * palm_basis(source, side).inversed()
    for digit in ("thumb", "index", "middle", "ring", "pinky"):
        for joint in (1, 2, 3):
            bone = f"{digit}_0{joint}_{side}"
            source_rest = source.get_ref_bone_pose(bone, local).rotation
            delta = source_rest.inversed() * source.get_bone_pose(bone, local).rotation
            basis = (target.get_ref_bone_pose(bone, world).rotation.inversed() * mapping
                     * source.get_ref_bone_pose(bone, world).rotation)
            rotations[bone] = (target.get_ref_bone_pose(bone, local).rotation * basis * delta * basis.inversed()).normalized()

folder = "/Game/AbandonedLocal/CubeGrip"
name = "DA_EQP00_Pose45"
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
