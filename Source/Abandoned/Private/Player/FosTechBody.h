#pragma once

#include "Components/PoseableMeshComponent.h"
#include "FosTechBody.generated.h"

class ACharacter;
class USkeletalMesh;
class UCubeGripPose;
class UBoxComponent;

// Applies the wearer's pose to the visual mesh; the capsule owns all gameplay collision.
UCLASS(Config=Game, DefaultConfig)
class UFosTechBody final : public UPoseableMeshComponent
{
    GENERATED_BODY()

public:
    UFosTechBody();
    FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    void FitToWearer(const ACharacter& Wearer, bool bAllowCustomSuit);
    void SetStance(const ACharacter& Wearer);
    bool HasCubeGripPose() const { return !CubeFingers.IsEmpty(); }
    bool IsCustomManny() const { return Neck != INDEX_NONE; }
    void PoseCube(const UBoxComponent* Box);
    void PoseTablet(const FTransform& Frame, double HalfWidth, float Weight);
    void PoseArms(const FTransform& Eye, float RaiseWeight, const FVector* Handholds,
        const FQuat& GripOrientation, float GripWeight);

private:
    struct FLimb
    {
        int32 Upper;
        int32 Lower;
        int32 End;
    };
    FTransform Current(int32 Index) const;
    void SetComponentPose(int32 Index, const FTransform& Transform);
    void SolveLimb(const FLimb& Limb, const FVector& Target, const FVector& Pole, const FQuat& EndRotation);
    void Stance(float ReadingWeight = 0.f);
    void RaiseWrist(const FTransform& Eye, float Weight);
    void Grip(int32 Side, const FVector& Palm, const FQuat& Orientation, float Weight);
    void PlacePalm(int32 Side, const FVector& KnuckleWorld, const FQuat& Orientation, float Weight);

    UPROPERTY(Config)
    TSoftObjectPtr<USkeletalMesh> BodyAsset;
    UPROPERTY(Config)
    TSoftObjectPtr<UCubeGripPose> CubeGripAsset;
    UPROPERTY(Config)
    TSoftObjectPtr<USkeletalMesh> CustomSuitAsset;
    UPROPERTY(Config)
    TSoftObjectPtr<UCubeGripPose> CustomGripAsset;
    TMap<int32, FQuat> CubeFingers;
    bool bHoldingPose = false;
    TArray<FTransform> Reference;
    FLimb Arms[2];
    FLimb Legs[2];
    // Four load-bearing fingers per hand, in index-to-little and proximal-to-distal order.
    int32 Fingers[2][4][3];
    int32 Pelvis = INDEX_NONE;
    int32 Spine = INDEX_NONE;
    int32 Neck = INDEX_NONE;
    int32 LeftClavicle = INDEX_NONE;
    int32 RightClavicle = INDEX_NONE;
    int32 Terminal = INDEX_NONE;
    double PelvisDrop = 0.0;
    bool bArmPoseActive = false;
};
