#include "Player/FosTechBody.h"

#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Player/CubeGripPose.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Player/MovementProfile.h"
#include "TwoBoneIK.h"
#include "Misc/PackageName.h"

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

bool UFosTechBody::HasLedgeFitAssets() const
{
    const auto Available = [](const FSoftObjectPath& Path)
    {
        return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName());
    };
    return Available(CustomSuitAsset.ToSoftObjectPath()) && Available(CustomGripAsset.ToSoftObjectPath()) &&
        Available(LedgeGripAsset.ToSoftObjectPath());
}

void UFosTechBody::FitToWearer(const ACharacter& Wearer, bool bAllowCustomSuit)
{
    // The EQP-00 grip trial uses its own reference skeleton.
    const bool bGripFit = bAllowCustomSuit && FParse::Param(FCommandLine::Get(), TEXT("FabCubeGrip"));
    const bool bCustomFit = bAllowCustomSuit && !bGripFit && FParse::Param(FCommandLine::Get(), TEXT("CustomMannyFit"));
    USkeletalMesh* Custom = bCustomFit ? CustomSuitAsset.LoadSynchronous() : nullptr;
    USkeletalMesh* Asset = Custom ? Custom : BodyAsset.LoadSynchronous();
    if (!ensureMsgf(Asset, TEXT("Missing EQP-00 skeletal mesh"))) { return; }
    SetSkinnedAssetAndUpdate(Asset);
    const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
    const auto Named = [&Skeleton](const TCHAR* Name) { return Skeleton.FindBoneIndex(FName(Name)); };
    const auto Rig = [&Named, Custom](const TCHAR* Eqp, const TCHAR* Manny) { return Named(Custom ? Manny : Eqp); };
    Pelvis = Rig(TEXT("pelvis"), TEXT("Hips"));
    Spine = Rig(TEXT("spine"), TEXT("Spine"));
    Terminal = Named(TEXT("terminal_l"));
    Neck = Custom ? Named(TEXT("Neck")) : INDEX_NONE;
    LeftClavicle = Rig(TEXT("clavicle_l"), TEXT("Left-shoulder"));
    RightClavicle = Custom ? Named(TEXT("Right-shoulder")) : INDEX_NONE;
    Arms[0] = {Rig(TEXT("upperarm_l"), TEXT("Left-arm")), Rig(TEXT("lowerarm_l"), TEXT("Left-elbow")), Rig(TEXT("hand_l"), TEXT("Left-wrist"))};
    Arms[1] = {Rig(TEXT("upperarm_r"), TEXT("Right-arm")), Rig(TEXT("lowerarm_r"), TEXT("Right-elbow")), Rig(TEXT("hand_r"), TEXT("Right-wrist"))};
    Legs[0] = {Rig(TEXT("thigh_l"), TEXT("Left-leg")), Rig(TEXT("calf_l"), TEXT("Left-knee")), Rig(TEXT("foot_l"), TEXT("Left-ankle"))};
    Legs[1] = {Rig(TEXT("thigh_r"), TEXT("Right-leg")), Rig(TEXT("calf_r"), TEXT("Right-knee")), Rig(TEXT("foot_r"), TEXT("Right-ankle"))};
    static const TCHAR* FingerNames[] = {TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
    static const TCHAR* MannyFingers[4][3] = {
        {TEXT("Index_01"), TEXT("IndexFinger2"), TEXT("IndexFinger3")},
        {TEXT("MiddleFinger1"), TEXT("MiddleFinger2"), TEXT("MiddleFinger3")},
        {TEXT("RingFinger1"), TEXT("RingFinger2"), TEXT("RingFinger3")},
        {TEXT("LittleFinger1"), TEXT("LittleFinger2"), TEXT("LittleFinger3")}
    };
    bool bFingersReady = true;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        for (int32 Digit = 0; Digit < 4; ++Digit)
        {
            for (int32 Joint = 0; Joint < 3; ++Joint)
            {
                const FString Name = Custom
                    ? FString::Printf(TEXT("%s_%c"), MannyFingers[Digit][Joint], Side == 0 ? 'L' : 'R')
                    : FString::Printf(TEXT("%s_0%d_%c"), FingerNames[Digit], Joint + 1, Side == 0 ? 'l' : 'r');
                Fingers[Side][Digit][Joint] = Named(*Name);
                bFingersReady &= Fingers[Side][Digit][Joint] != INDEX_NONE;
            }
        }
    }
    const auto Valid = [](const FLimb& Limb) {
        return Limb.Upper != INDEX_NONE && Limb.Lower != INDEX_NONE && Limb.End != INDEX_NONE;
    };
    if (!ensureMsgf(Pelvis != INDEX_NONE && Spine != INDEX_NONE && LeftClavicle != INDEX_NONE && (Custom ? Neck : Terminal) != INDEX_NONE && Valid(Arms[0]) && Valid(Arms[1]) &&
        Valid(Legs[0]) && Valid(Legs[1]) && bFingersReady && (!Custom || RightClavicle != INDEX_NONE),
        TEXT("Incomplete first-person skeleton for %s"), *Asset->GetPathName()))
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
    const double Height = Custom ? Reference[Neck].GetLocation().Z - (MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z) : 2.0 * MeshBounds.BoxExtent.Z;
    SetRelativeScale3D(FVector(CollarHeight / FMath::Max(1.0, Height)));
    if (Custom) { UE_LOG(LogTemp, Display, TEXT("Custom Manny fitting: %s"), *Asset->GetPathName()); }
    SetStance(Wearer);
    const auto LoadFingers = [&Skeleton](const TSoftObjectPtr<UCubeGripPose>& Asset, TMap<int32, FQuat>& FingersOut)
    {
        if (const UCubeGripPose* Pose = Asset.LoadSynchronous())
        {
            for (const auto& Pair : Pose->FingerRotations)
            {
                const int32 Index = Skeleton.FindBoneIndex(Pair.Key);
                if (Index != INDEX_NONE && Pair.Value.IsNormalized()) { FingersOut.Add(Index, Pair.Value); }
            }
        }
        if (FingersOut.Num() != 30)
        {
            FingersOut.Empty();
            UE_LOG(LogTemp, Warning, TEXT("Grip pose %s requires 30 matching finger rotations"), *Asset.ToString());
        }
    };
    if (bGripFit || Custom) { LoadFingers(Custom ? CustomGripAsset : CubeGripAsset, CubeFingers); }
    if (Custom && !LedgeGripAsset.IsNull() && FPackageName::DoesPackageExist(LedgeGripAsset.ToSoftObjectPath().GetLongPackageName()))
    {
        LoadFingers(LedgeGripAsset, LedgeFingers);
        UE_LOG(LogTemp, Display, TEXT("Custom Manny ledge pose: %d joints"), LedgeFingers.Num());
    }
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
    BoneSpaceTransforms[Index] = Parent == INDEX_NONE ? Transform : Transform.GetRelativeTransform(Current(Parent));
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
    if (Neck != INDEX_NONE) { BoneSpaceTransforms[Neck].SetScale3D(FVector::ZeroVector); }
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

void UFosTechBody::PlacePalm(int32 Side, const FVector& KnuckleWorld, const FQuat& Orientation, float Weight)
{
    const FLimb& Arm = Arms[Side];
    const FTransform& Mesh = GetComponentTransform();
    FTransform Wrist = Current(Arm.End);
    const FVector AlongHand = Current(Fingers[Side][1][0]).GetLocation() - Wrist.GetLocation();
    const FVector Outward = GetRelativeRotation().UnrotateVector(Side == 0 ? -FVector::RightVector : FVector::RightVector);
    const FVector AcrossKnuckles = Current(Fingers[Side][3][0]).GetLocation() - Current(Fingers[Side][0][0]).GetLocation();
    FVector Dorsal = FVector::CrossProduct(AlongHand, AcrossKnuckles).GetSafeNormal();
    // Use handedness to orient the dorsal normal on Custom Manny's angled rest arms.
    const bool bInvert = Neck != INDEX_NONE ? Side == 0 : FVector::DotProduct(Dorsal, Outward) < 0.0;
    if (bInvert) { Dorsal = -Dorsal; }
    const FQuat HandBasis = FRotationMatrix::MakeFromXZ(AlongHand, Dorsal).ToQuat();
    const FQuat GripTurn = Mesh.InverseTransformRotation(Orientation) * HandBasis.Inverse();
    const FVector Knuckle = Mesh.InverseTransformPosition(KnuckleWorld);
    const FVector WristGoal = Knuckle - GripTurn.RotateVector(AlongHand);
    const FVector Pole = Current(Arm.Upper).GetLocation() + Outward * 40.0 - FVector::UpVector * 38.0;
    SolveLimb(Arm, FMath::Lerp(Wrist.GetLocation(), WristGoal, Weight), Pole,
        FQuat::Slerp(Wrist.GetRotation(), GripTurn * Wrist.GetRotation(), Weight));
}

void UFosTechBody::PoseCube(const UBoxComponent* Box)
{
    if (!HasCubeGripPose() || (!Box && !bHoldingPose)) { return; }
    Stance();
    if (Box)
    {
        // Lean from the waist so the shoulders follow the load with the feet planted.
        FTransform Torso = Current(Spine);
        const FVector Right = GetRelativeRotation().UnrotateVector(FVector::RightVector);
        const double LeanDegrees = IsCustomManny() ? 15.0 : 20.0;
        Torso.SetRotation(FQuat(Right, FMath::DegreesToRadians(LeanDegrees)) * Torso.GetRotation());
        SetComponentPose(Spine, Torso);
        const FTransform Frame = Box->GetComponentTransform();
        const FVector Extent = Box->GetUnscaledBoxExtent();
        // Manny's LOD 0 palm pad is 3.865 cm from its wrist-to-knuckle midline in mesh space.
        const double PadDepth = IsCustomManny() ? 3.86545 * GetComponentScale().Z : 2.0;
        const double Near = IsCustomManny() ? 0.85 : 0.79886;
        const double GripHeight = IsCustomManny() ? 0.39 : 0.25;
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const double Sign = Side == 0 ? -1.0 : 1.0;
            if (IsCustomManny())
            {
                // Bring the shoulders around the load without tipping the collar into the camera.
                const int32 Clavicle = Side == 0 ? LeftClavicle : RightClavicle;
                FTransform Shoulder = Current(Clavicle);
                Shoulder.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(-Sign * 55.0)) * Shoulder.GetRotation());
                SetComponentPose(Clavicle, Shoulder);
            }
            // The visible front bevels lie at -x +/- y = 1.59772 in half-size units.
            // Place the palm on their metal strips inside the box collision corners.
            const FVector Normal = Frame.TransformVectorNoScale(FVector(-1.0, Sign, 0.0).GetSafeNormal());
            const FVector Point = Frame.TransformPosition(FVector(-Near * Extent.X, (1.59772 - Near) * Extent.Y * Sign, GripHeight * Extent.Z));
            const FQuat Orientation = FRotationMatrix::MakeFromXZ(Frame.GetUnitAxis(EAxis::Z), Normal).ToQuat();
            PlacePalm(Side, Point + Normal * PadDepth, Orientation, 1.f);
        }
        for (const auto& Pair : CubeFingers) { BoneSpaceTransforms[Pair.Key].SetRotation(Pair.Value); }
    }
    RefreshBoneTransforms();
    bHoldingPose = Box != nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
FVector UFosTechBody::MannyPalmContact(int32 Hand) const
{
    // Skin points on LOD 0 triangles 36654 and 38831, in wrist bone space.
    return GetSocketTransform(Hand == 0 ? TEXT("Left-wrist") : TEXT("Right-wrist")).TransformPosition(
        FVector(Hand == 0 ? -0.030309 : 0.030309, -0.05555, -0.032653));
}
#endif

void UFosTechBody::PoseTablet(const FTransform& Frame, double HalfWidth, float Weight)
{
    if (!IsCustomManny()) { return; }
    Stance(Weight);
    for (int32 Side = 0; Side < 2 && Weight > 0.f; ++Side)
    {
        const double Sign = Side == 0 ? 1.0 : -1.0;
        const FVector Normal = Frame.GetUnitAxis(EAxis::Y) * Sign;
        const FVector Edge = Frame.TransformPosition(FVector(0, Sign * HalfWidth, 0));
        const FVector Along = (Frame.GetUnitAxis(EAxis::Z) * .75 - Frame.GetUnitAxis(EAxis::X) * .66).GetSafeNormal();
        const FQuat Rotation = FRotationMatrix::MakeFromXZ(Along, Normal).ToQuat();
        const int32 Clavicle = Side == 0 ? LeftClavicle : RightClavicle;
        FTransform Shoulder = Current(Clavicle);
        Shoulder.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(Sign * 35.0 * Weight)) * Shoulder.GetRotation());
        SetComponentPose(Clavicle, Shoulder);
        const double HalfHand = .5 * FVector::Distance(Current(Fingers[Side][1][0]).GetLocation(), Current(Arms[Side].End).GetLocation());
        PlacePalm(Side, Edge + (Normal * 3.86545 + Along * HalfHand) * GetComponentScale().Z, Rotation, Weight);
    }
    for (const auto& Pair : CubeFingers)
    {
        auto& Bone = BoneSpaceTransforms[Pair.Key];
        Bone.SetRotation(FQuat::Slerp(Bone.GetRotation(), Pair.Value, Weight));
    }
    RefreshBoneTransforms();
}

void UFosTechBody::Grip(int32 Side, const FVector& Palm, const FQuat& Orientation, float Weight)
{
    FVector Knuckle = Palm + FVector::UpVector * 2.0;
    if (IsCustomManny())
    {
        const double HalfHand = .5 * FVector::Distance(Current(Fingers[Side][1][0]).GetLocation(), Current(Arms[Side].End).GetLocation());
        Knuckle = Palm + (Orientation.GetForwardVector() * HalfHand + Orientation.GetUpVector() * 3.86545) * GetComponentScale().Z;
    }
    PlacePalm(Side, Knuckle, Orientation, Weight);
    const FTransform& Mesh = GetComponentTransform();

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
        if (IsCustomManny())
        {
            // Let the hips hang below the camera while the shoulders reach over the rim.
            Hip.AddToTranslation(FVector::DownVector * (14.0 * GripWeight / GetComponentScale().Z));
            for (const auto& Pair : LedgeFingers)
            {
                auto& Bone = BoneSpaceTransforms[Pair.Key];
                Bone.SetRotation(FQuat::Slerp(Bone.GetRotation(), Pair.Value, GripWeight));
            }
        }
        SetComponentPose(Pelvis, Hip);
        FTransform Torso = Current(Spine);
        const FVector Right = GetRelativeRotation().UnrotateVector(FVector::RightVector);
        Torso.SetRotation(FQuat(Right, FMath::DegreesToRadians(10.0) * GripWeight) * Torso.GetRotation());
        SetComponentPose(Spine, Torso);
        if (IsCustomManny())
        {
            for (int Side = 0; Side < 2; ++Side)
            {
                const int32 Clavicle = Side == 0 ? LeftClavicle : RightClavicle;
                FTransform Shoulder = Current(Clavicle);
                const double Yaw = (Side == 0 ? 40.0 : -40.0) * GripWeight;
                Shoulder.SetRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw)) * Shoulder.GetRotation());
                SetComponentPose(Clavicle, Shoulder);
            }
        }
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
