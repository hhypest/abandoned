#pragma once

#include "CoreMinimal.h"
#include "World/SceneLayout.h"
#include "PowerLabLayout.generated.h"

USTRUCT()
struct FOpticalLaneLayout
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Emitter = FVector::ZeroVector;

    UPROPERTY()
    FVector Receiver = FVector::ZeroVector;

    UPROPERTY()
    FVector Door = FVector::ZeroVector;
};

UCLASS(Config=Game)
class UPowerLabLayout final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    FVector Spawn = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector Button = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector Cube = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector Mirror = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector MirrorDock = FVector::ZeroVector;

    UPROPERTY(Config)
    FOpticalLaneLayout Interruption;

    UPROPERTY(Config)
    FOpticalLaneLayout Reflection;

    UPROPERTY(Config)
    TArray<FSceneBlock> Room;

    UPROPERTY(Config)
    TArray<FSceneFinish> Finishes;

    UPROPERTY(Config)
    TArray<FSceneLight> Lights;
};
