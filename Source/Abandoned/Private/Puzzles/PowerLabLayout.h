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
    FVector PulseButton = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector PulseReturn = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector PulseRetry = FVector::ZeroVector;

    UPROPERTY(Config)
    FSceneBlock PulseDoor;

    UPROPERTY(Config)
    FSceneBlock PulseExit;

    UPROPERTY(Config)
    float PulseSeconds = 0.0f;

    bool HasValidPulse() const
    {
        const FBox DoorBounds = FBox(PulseDoor.Center - PulseDoor.Size * 0.5, PulseDoor.Center + PulseDoor.Size * 0.5);
        const double ExitStart = PulseExit.Center.X - PulseExit.Size.X * 0.5;
        return PulseDoor.IsValid() && PulseDoor.bCollision && PulseExit.IsValid() && !PulseExit.bCollision &&
            FMath::IsFinite(PulseSeconds) && PulseSeconds > 0.0f &&
            !PulseButton.ContainsNaN() && !PulseReturn.ContainsNaN() && !PulseRetry.ContainsNaN() &&
            PulseButton.X > Reflection.Door.X && PulseButton.X < DoorBounds.Min.X &&
            PulseRetry.X > Reflection.Door.X && PulseRetry.X + 30.0 < DoorBounds.Min.X &&
            PulseReturn.X > DoorBounds.Max.X && ExitStart > DoorBounds.Max.X;
    }

    UPROPERTY(Config)
    TArray<FSceneBlock> Room;

    UPROPERTY(Config)
    TArray<FSceneFinish> Finishes;

    UPROPERTY(Config)
    TArray<FSceneLight> Lights;
};
