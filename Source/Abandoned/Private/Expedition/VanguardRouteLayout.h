#pragma once

#include "CoreMinimal.h"
#include "Domain/Expedition/FallImpact.h"
#include "World/SceneLayout.h"
#include "VanguardRouteLayout.generated.h"

class UStaticMesh;
class USoundBase;
class UMaterialInterface;

UCLASS(Config=Game, DefaultConfig)
class UVanguardMantleVisuals final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> LeftGlove;

    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> RightGlove;

    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> Sleeve;

    UPROPERTY(Config)
    TSoftObjectPtr<UMaterialInterface> Finish;
};

UCLASS(Config=Game, DefaultConfig)
class UVanguardInjurySettings final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    double RecoverySeconds = 0.0;

    UPROPERTY(Config)
    float InitialGroundSpeedScale = 1.0f;

    UPROPERTY(Config)
    TSoftObjectPtr<USoundBase> BreathSound;

    UPROPERTY(Config)
    float BreathVolume = 0.7f;

    Abandoned::Domain::Expedition::InjuryRecoveryProfile Recovery() const
    {
        return {RecoverySeconds, InitialGroundSpeedScale};
    }
};

UCLASS(Config=Game, DefaultConfig)
class UVanguardFlashlightSettings final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    FVector ShoulderOffset = FVector::ZeroVector;

    UPROPERTY(Config)
    float IntensityLumens = 0.0f;

    UPROPERTY(Config)
    float RangeCm = 0.0f;

    UPROPERTY(Config)
    float InnerConeDegrees = 0.0f;

    UPROPERTY(Config)
    float OuterConeDegrees = 0.0f;

    bool IsValid() const
    {
        return !ShoulderOffset.ContainsNaN() &&
            FMath::IsFinite(IntensityLumens) && IntensityLumens > 0.0f &&
            FMath::IsFinite(RangeCm) && RangeCm > 0.0f &&
            InnerConeDegrees >= 0.0f && OuterConeDegrees > InnerConeDegrees && OuterConeDegrees <= 89.0f;
    }
};

UCLASS(Config=Game, DefaultConfig)
class UVanguardRouteLayout : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    TArray<FSceneBlock> Room;

    UPROPERTY(Config)
    TArray<FSceneFinish> Finishes;

    UPROPERTY(Config)
    TArray<FSceneLight> Lights;

    UPROPERTY(Config)
    FVector Spawn = FVector::ZeroVector;
};

UCLASS(Config=Game, DefaultConfig)
class UVanguardPassageLayout : public UVanguardRouteLayout
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    bool bEnableMantle = false;

    UPROPERTY(Config)
    TArray<FSceneBlock> MantleLedges;

    UPROPERTY(Config)
    FSceneBlock Door;

    UPROPERTY(Config)
    FSceneBlock ExitZone;

    UPROPERTY(Config)
    FSceneBlock FrameFit;

    UPROPERTY(Config)
    FRotator FrameRotation = FRotator::ZeroRotator;

    UPROPERTY(Config)
    FRotator LeafRotation = FRotator::ZeroRotator;

    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> FrameMesh;

    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> LeafMesh;
};

UCLASS(Config=Game, DefaultConfig)
class UVanguardStationLayout final : public UVanguardRouteLayout
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    FSceneBlock SupplyCabinet;
};

UCLASS(Config=Game, DefaultConfig)
class UVanguardRadiationLayout final : public UVanguardPassageLayout
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    FSceneBlock ExposureBounds;

    UPROPERTY(Config)
    TArray<FSceneBlock> ShelterBounds;

    UPROPERTY(Config)
    double QuietSeconds = 0.0;

    UPROPERTY(Config)
    double FlareSeconds = 0.0;

    UPROPERTY(Config)
    double QuietDosePerSecond = 0.0;

    UPROPERTY(Config)
    double FlareDosePerSecond = 0.0;

    UPROPERTY(Config)
    double RecoveryDosePerSecond = 0.0;
};
