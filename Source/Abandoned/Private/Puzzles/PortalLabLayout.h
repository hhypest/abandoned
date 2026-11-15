#pragma once

#include "CoreMinimal.h"
#include "World/SceneLayout.h"
#include "PortalLabLayout.generated.h"

USTRUCT()
struct FPortalMouthLayout
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Center = FVector::ZeroVector;

    UPROPERTY()
    double Yaw = 0.0;

    UPROPERTY()
    FVector Console = FVector::ZeroVector;
};

UCLASS(Config=Game)
class UPortalLabLayout final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    FVector Spawn = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector SmallCube = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector LargeCube = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector Dev20Station = FVector::ZeroVector;

    UPROPERTY(Config)
    FSceneBlock Dev20Exit;

    UPROPERTY(Config)
    FPortalMouthLayout Arrival;

    UPROPERTY(Config)
    FPortalMouthLayout Departure;

    UPROPERTY(Config)
    TArray<FSceneBlock> Room;

    UPROPERTY(Config)
    TArray<FSceneFinish> Finishes;

    UPROPERTY(Config)
    TArray<FSceneLight> Lights;
};
