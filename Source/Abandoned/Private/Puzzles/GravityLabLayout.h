#pragma once

#include "CoreMinimal.h"
#include "World/SceneLayout.h"
#include "GravityLabLayout.generated.h"

UCLASS(Config=Game)
class UGravityLabLayout final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    FVector Spawn = FVector::ZeroVector;

    UPROPERTY(Config)
    FSceneBlock Field;

    UPROPERTY(Config)
    FSceneBlock Goal;

    UPROPERTY(Config)
    FVector PowerSwitch = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector Terminal = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector SmallStart = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector LargeStart = FVector::ZeroVector;

    UPROPERTY(Config)
    TArray<FSceneBlock> Room;

    UPROPERTY(Config)
    TArray<FSceneFinish> Finishes;

    UPROPERTY(Config)
    TArray<FSceneLight> Lights;
};
