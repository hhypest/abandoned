#include "Player/FosTechBody.h"

#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Player/MovementProfile.h"
#include "TwoBoneIK.h"

UFosTechBody::UFosTechBody()
{
    VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCanEverAffectNavigation(false);
    SetGenerateOverlapEvents(false);
    SetOnlyOwnerSee(true);
    SetCastShadow(false);
    SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
}

FBoxSphereBounds UFosTechBody::CalcBounds(const FTransform& LocalToWorld) const
{
    FBoxSphereBounds BoundsInWorld = Super::CalcBounds(LocalToWorld);
    const TArray<FTransform>& Bones = GetComponentSpaceTransforms();
    if (!Reference.IsEmpty() && Bones.Num() == Reference.Num())
    {
        // This visual has no physics asset. Its raised hand can leave the imported rest bounds
        // entirely, including when the camera sees only that hand through the near plane.
        FBox PoseBox(ForceInit);
        for (const FTransform& Bone : Bones) { PoseBox += Bone.GetLocation(); }
        BoundsInWorld = BoundsInWorld + FBoxSphereBounds(PoseBox.ExpandBy(20.0)).TransformBy(LocalToWorld);
    }
    return BoundsInWorld;
}

void UFosTechBody::FitToWearer(const ACharacter& Wearer)
{
    USkeletalMesh* Asset = BodyAsset.LoadSynchronous();
    if (!ensureMsgf(Asset, TEXT("Missing EQP-00 skeletal mesh"))) { return; }
    SetSkinnedAssetAndUpdate(Asset);
    const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
    const auto Named = [&Skeleton](const TCHAR* Name) { return Skeleton.FindBoneIndex(FName(Name)); };
    Pelvis = Named(TEXT("pelvis"));
    Terminal = Named(TEXT("terminal_l"));
    Spine = Named(TEXT("spine"));
    LeftClavicle = Named(TEXT("clavicle_l"));
    Arms[0] = {Named(TEXT("upperarm_l")), Named(TEXT("lowerarm_l")), Named(TEXT("hand_l"))};
    Arms[1] = {Named(TEXT("upperarm_r")), Named(TEXT("lowerarm_r")), Named(TEXT("hand_r"))};
    Legs[0] = {Named(TEXT("thigh_l")), Named(TEXT("calf_l")), Named(TEXT("foot_l"))};
    Legs[1] = {Named(TEXT("thigh_r")), Named(TEXT("calf_r")), Named(TEXT("foot_r"))};
    static const TCHAR* FingerNames[] = {TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
    bool bFingersReady = true;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        for (int32 Digit = 0; Digit < 4; ++Digit)
        {
            for (int32 Joint = 0; Joint < 3; ++Joint)
            {
                const FString Name = FString::Printf(TEXT("%s_0%d_%c"), FingerNames[Digit], Joint + 1, Side == 0 ? 'l' : 'r');
                Fingers[Side][Digit][Joint] = Named(*Name);
                bFingersReady &= Fingers[Side][Digit][Joint] != INDEX_NONE;
            }
        }
    }
    const auto Valid = [](const FLimb& Limb) {
        return Limb.Upper != INDEX_NONE && Limb.Lower != INDEX_NONE && Limb.End != INDEX_NONE;
    };
    if (!ensureMsgf(Pelvis != INDEX_NONE && Spine != INDEX_NONE && LeftClavicle != INDEX_NONE && Terminal != INDEX_NONE && Valid(Arms[0]) && Valid(Arms[1]) &&
        Valid(Legs[0]) && Valid(Legs[1]) && bFingersReady,
        TEXT("Incomplete EQP-00 skeleton")))
    {
        SetVisibility(false);
        return;
    }
    Reference = Skeleton.GetRefBonePose();
    for (int32 Index = 1; Index < Reference.Num(); ++Index)
    {
        Reference[Index] *= Reference[Skeleton.GetParentIndex(Index)];
    }

    // Align anatomical forward and left to the pawn independently of the importer's horizontal axes.
    const FVector Left = Reference[Arms[0].Upper].GetLocation() - Reference[Arms[1].Upper].GetLocation();
    const FVector Forward = FVector::CrossProduct(FVector::UpVector, Left).GetSafeNormal2D();
    SetRelativeRotation(FRotator(0.0, -Forward.Rotation().Yaw, 0.0));
    const FBoxSphereBounds MeshBounds = Asset->GetImportedBounds();
    const double CollarHeight = Abandoned::MovementProfile::StandingHalfHeight + Abandoned::MovementProfile::CameraHeight - 6.0;
    SetRelativeScale3D(FVector(CollarHeight / (2.0 * MeshBounds.BoxExtent.Z)));
    SetStance(Wearer);
}

FTransform UFosTechBody::Current(int32 Index) const
{
    const FReferenceSkeleton& Skeleton = GetSkinnedAsset()->GetRefSkeleton();
    FTransform Result = BoneSpaceTransforms[Index];
    for (int32 Parent = Skeleton.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
    {
        Result *= BoneSpaceTransforms[Parent];
    }
    return Result;
}

void UFosTechBody::SetComponentPose(int32 Index, const FTransform& Transform)
{
    const int32 Parent = GetSkinnedAsset()->GetRefSkeleton().GetParentIndex(Index);
    BoneSpaceTransforms[Index] = Transform.GetRelativeTransform(Current(Parent));
}

void UFosTechBody::SolveLimb(const FLimb& Limb, const FVector& Target, const FVector& Pole, const FQuat& EndRotation)
{
    FTransform Upper = Current(Limb.Upper), Lower = Current(Limb.Lower), End = Current(Limb.End);
    AnimationCore::SolveTwoBoneIK(Upper, Lower, End, Pole, Target, false, 1.0, 1.0);
    End.SetRotation(EndRotation);
    SetComponentPose(Limb.Upper, Upper);
    SetComponentPose(Limb.Lower, Lower);
    SetComponentPose(Limb.End, End);
}

void UFosTechBody::SetStance(const ACharacter& Wearer)
{
    if (Reference.IsEmpty()) { return; }
    const FBoxSphereBounds MeshBounds = CastChecked<USkeletalMesh>(GetSkinnedAsset())->GetImportedBounds();
    const double HalfHeight = Wearer.GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
    const double Scale = GetRelativeScale3D().Z;
    // A squat shares the eye-height change between the hips and a forward bend at the waist.
    PelvisDrop = 1.48 * (Abandoned::MovementProfile::StandingHalfHeight - HalfHeight) / Scale;
    // Grounded crouch moves the capsule down by exactly the required upward offset.
    // A swept/world-delta setter can see zero movement during CharacterMovement's
    // deferred update and skip writing the new attachment offset. This mesh is cosmetic.
    SetRelativeLocation_Direct(FVector(-10.0, 0.0, -HalfHeight - Scale * (MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z)));
    UpdateComponentToWorld();
    Stance();
    RefreshBoneTransforms();
    bArmPoseActive = false;
}

void UFosTechBody::Stance(float ReadingWeight)
{
    BoneSpaceTransforms = GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
    if (FMath::IsNearlyZero(PelvisDrop)) { return; }
    FTransform Hip = Current(Pelvis);
    const FVector Forward = GetRelativeRotation().UnrotateVector(FVector::ForwardVector);
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    // When reading in a squat, shift the hips over the feet to support the raised arm.
    Hip.AddToTranslation((FVector::DownVector - Forward * FMath::Lerp(0.48, 0.24, ReadingWeight)) * PelvisDrop);
    // The eye is above the front of the suit. Keep the open neck behind it when
    // looking straight down while the pelvis moves back over the planted boots.
    Hip.SetRotation(FQuat(Right, FMath::DegreesToRadians(18.0)) * Hip.GetRotation());
    SetComponentPose(Pelvis, Hip);
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FLimb& Leg = Legs[Side];
        const FTransform& Boot = Reference[Leg.End];
        SolveLimb(Leg, Boot.GetLocation(), Reference[Leg.Lower].GetLocation() + Forward * 85.0, Boot.GetRotation());
    }
}

void UFosTechBody::RaiseWrist(const FTransform& Eye, float Weight)
{
    const FLimb& Arm = Arms[0];
    const FTransform& Mesh = GetComponentTransform();
    const FTransform Screen = Current(Terminal);
    const FTransform Elbow = Current(Arm.Lower);
    const FTransform Shoulder = Current(Arm.Upper);
    const FTransform Collar = Current(LeftClavicle);
    FTransform Torso = Current(Spine);

    // Keep the glass square to an ordinary gaze. Roll the cuff slightly toward the eye
    // when looking overhead, where the forearm approaches the limit of its reach.
    // Solve for the elbow: the screen is rigidly offset from it on the same forearm.
    const double Upward = FMath::Max(0.0, Eye.GetUnitAxis(EAxis::X).Z);
    const double SideAngle = 0.30 * Upward * Upward;
    const FVector SightInView = FVector(1.0, -0.16, -0.07).GetSafeNormal();
    const FQuat ScreenRotation = Mesh.InverseTransformRotation(Eye.GetRotation() *
        FRotationMatrix::MakeFromXZ(FVector(-1.0, SideAngle, 0.0), FVector::UpVector).ToQuat());
    const FQuat Turn = ScreenRotation * Screen.GetRotation().Inverse();
    const FVector Sight = Mesh.InverseTransformVectorNoScale(Eye.TransformVectorNoScale(SightInView));
    const FVector RayStart = Mesh.InverseTransformPosition(Eye.GetLocation()) +
        Turn.RotateVector(Elbow.GetLocation() - Screen.GetLocation());
    const double TorsoLength = FVector::Distance(Torso.GetLocation(), Collar.GetLocation());
    const double ArmReach = FVector::Distance(Collar.GetLocation(), Shoulder.GetLocation()) +
        FVector::Distance(Shoulder.GetLocation(), Elbow.GetLocation()) - 0.5;

    // Intersect the reading ray with the arm and torso's reach. At high pitch
    // the wearer can bring the screen closer while keeping its centre in view.
    const FVector ToSpine = Torso.GetLocation() - RayStart;
    const double Along = FVector::DotProduct(ToSpine, Sight);
    const double Reach = TorsoLength + ArmReach;
    const double HalfChord = FMath::Sqrt(FMath::Max(0.0, Reach * Reach - (ToSpine - Along * Sight).SizeSquared()));
    const double ReadingDistance = 22.0 / Mesh.GetScale3D().Z;
    const FVector ElbowGoal = RayStart + Sight * FMath::Clamp(Along + HalfChord, 0.0, ReadingDistance);

    // Retain the original trunk unless the clavicle and upper arm alone cannot
    // reach the elbow. Swing the spine just enough; the pelvis and feet stay planted.
    const FQuat RestSpine = BoneSpaceTransforms[Spine].GetRotation();
    const FQuat RestCollar = BoneSpaceTransforms[LeftClavicle].GetRotation();
    const FQuat RestUpper = BoneSpaceTransforms[Arm.Upper].GetRotation();
    const FQuat RestLower = BoneSpaceTransforms[Arm.Lower].GetRotation();
    if (FVector::DistSquared(Collar.GetLocation(), ElbowGoal) > ArmReach * ArmReach)
    {
        FVector NewCollar, ReachableElbow;
        AnimationCore::SolveTwoBoneIK(Torso.GetLocation(), Collar.GetLocation(), Elbow.GetLocation(),
            Collar.GetLocation(), ElbowGoal, NewCollar, ReachableElbow, TorsoLength, ArmReach, false, 1.0, 1.0);
        const FQuat Lean = FQuat::FindBetweenNormals((Collar.GetLocation() - Torso.GetLocation()).GetSafeNormal(),
            (NewCollar - Torso.GetLocation()).GetSafeNormal());
        Torso.SetRotation(Lean * Torso.GetRotation());
        SetComponentPose(Spine, Torso);
    }
    const FVector Outward = GetRelativeRotation().UnrotateVector(-FVector::RightVector);
    const FVector ShoulderPole = Current(LeftClavicle).GetLocation() + 30.0 * Outward + 6.0 * FVector::UpVector;
    SolveLimb({LeftClavicle, Arm.Upper, Arm.Lower}, ElbowGoal, ShoulderPole, Turn * Elbow.GetRotation());

    // Blend rotations in the hierarchy so each segment keeps its length on the way up.
    const auto Blend = [this, Weight](int32 Index, const FQuat& Rest) {
        FTransform& Bone = BoneSpaceTransforms[Index];
        Bone.SetRotation(FQuat::Slerp(Rest, Bone.GetRotation(), Weight));
    };
    Blend(Spine, RestSpine);
    Blend(LeftClavicle, RestCollar);
    Blend(Arm.Upper, RestUpper);
    Blend(Arm.Lower, RestLower);
}

void UFosTechBody::Grip(int32 Side, const FVector& Palm, const FQuat& Orientation, float Weight)
{
    const FLimb& Arm = Arms[Side];
    const FTransform& Mesh = GetComponentTransform();
    FTransform Wrist = Current(Arm.End);
    const FVector AlongHand = Current(Fingers[Side][1][0]).GetLocation() - Wrist.GetLocation();
    const FVector Outward = GetRelativeRotation().UnrotateVector(Side == 0 ? -FVector::RightVector : FVector::RightVector);
    const FVector AcrossKnuckles = Current(Fingers[Side][3][0]).GetLocation() - Current(Fingers[Side][0][0]).GetLocation();
    FVector Dorsal = FVector::CrossProduct(AlongHand, AcrossKnuckles).GetSafeNormal();
    if (FVector::DotProduct(Dorsal, Outward) < 0.0) { Dorsal = -Dorsal; }
    const FQuat HandBasis = FRotationMatrix::MakeFromXZ(AlongHand, Dorsal).ToQuat();
    const FQuat GripTurn = Mesh.InverseTransformRotation(Orientation) * HandBasis.Inverse();
    const FVector Knuckle = Mesh.InverseTransformPosition(Palm + FVector::UpVector * 2.0);
    const FVector WristGoal = Knuckle - GripTurn.RotateVector(AlongHand);
    const FVector Pole = Current(Arm.Upper).GetLocation() + Outward * 40.0 - FVector::UpVector * 38.0;
    SolveLimb(Arm, FMath::Lerp(Wrist.GetLocation(), WristGoal, Weight), Pole,
        FQuat::Slerp(Wrist.GetRotation(), GripTurn * Wrist.GetRotation(), Weight));

    // Lay each finger pad on the top surface. Preserve phalanx lengths and lateral spread.
    const FVector Surface = Mesh.InverseTransformPosition(Palm);
    const FVector Normal = Mesh.InverseTransformVectorNoScale(Orientation.GetUpVector());
    for (const auto& Digit : Fingers[Side])
    {
        for (int32 Joint = 0; Joint < 2; ++Joint)
        {
            FTransform Parent = Current(Digit[Joint]);
            const FVector Direction = Current(Digit[Joint + 1]).GetLocation() - Parent.GetLocation();
            const double Length = Direction.Size();
            if (Length < UE_KINDA_SMALL_NUMBER) { continue; }
            const double Height = FVector::DotProduct(Parent.GetLocation() - Surface, Normal);
            const double PadHeight = (Joint == 0 ? 1.6 : 1.3) / Mesh.GetScale3D().Z;
            const double Rise = FMath::Clamp(PadHeight - Height, -Length, Length);
            const FVector Along = FVector::VectorPlaneProject(Direction, Normal).GetSafeNormal();
            const FVector Planted = Along * FMath::Sqrt(FMath::Max(0.0, Length * Length - Rise * Rise)) + Normal * Rise;
            const FQuat Turn = FQuat::FindBetweenNormals(Direction / Length, Planted.GetSafeNormal());
            Parent.SetRotation(FQuat::Slerp(FQuat::Identity, Turn, Weight) * Parent.GetRotation());
            SetComponentPose(Digit[Joint], Parent);
        }
    }
}

void UFosTechBody::PoseArms(const FTransform& Eye, float RaiseWeight, const FVector* Handholds,
    const FQuat& GripOrientation, float GripWeight)
{
    if (Reference.IsEmpty()) { return; }
    const bool bActive = RaiseWeight > 0.f || (Handholds && GripWeight > 0.f);
    if (!bActive && !bArmPoseActive) { return; }
    Stance(RaiseWeight);
    if (Handholds && GripWeight > 0.f)
    {
        // The arms bear the weight before the trunk clears the sill. The collision
        // capsule can rise early, while the wearer still hangs from the fixed handholds.
        FTransform Hip = Current(Pelvis);
        Hip.AddToTranslation(FVector::DownVector * (28.0 * GripWeight / GetComponentScale().Z));
        SetComponentPose(Pelvis, Hip);
        FTransform Torso = Current(Spine);
        const FVector Right = GetRelativeRotation().UnrotateVector(FVector::RightVector);
        Torso.SetRotation(FQuat(Right, FMath::DegreesToRadians(10.0) * GripWeight) * Torso.GetRotation());
        SetComponentPose(Spine, Torso);
        Grip(0, Handholds[0], GripOrientation, GripWeight);
        Grip(1, Handholds[1], GripOrientation, GripWeight);
    }
    else if (RaiseWeight > 0.f)
    {
        RaiseWrist(Eye, RaiseWeight);
    }
    RefreshBoneTransforms();
    bArmPoseActive = bActive;
}
