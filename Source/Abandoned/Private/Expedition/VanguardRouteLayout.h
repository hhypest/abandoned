#pragma once

#include "CoreMinimal.h"
#include "World/SceneLayout.h"
#include "VanguardRouteLayout.generated.h"

class UStaticMesh;

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
class UVanguardPassageLayout final : public UVanguardRouteLayout
{
    GENERATED_BODY()

public:
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
