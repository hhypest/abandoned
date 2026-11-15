#pragma once

#include "CoreMinimal.h"
#include "TrainingChamberLayout.generated.h"

USTRUCT()
struct FTrainingBlock
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Center = FVector::ZeroVector;

    UPROPERTY()
    FVector Size = FVector::ZeroVector;

    UPROPERTY()
    bool bCollision = true;

    bool IsValid() const
    {
        return !Center.ContainsNaN() && !Size.ContainsNaN() && Size.GetMin() > 0.0;
    }
};

USTRUCT()
struct FTrainingLight
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Position = FVector::ZeroVector;

    UPROPERTY()
    float Intensity = 0.0f;

    UPROPERTY()
    float Radius = 0.0f;

    UPROPERTY()
    FLinearColor Color = FLinearColor::White;

    bool IsValid() const
    {
        return !Position.ContainsNaN() && FMath::IsFinite(Intensity) && Intensity >= 0.0f &&
            FMath::IsFinite(Radius) && Radius > 0.0f &&
            FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
    }
};

UCLASS(Config=Game, DefaultConfig)
class UTrainingChamberLayout final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    TArray<FTrainingBlock> Room;

    UPROPERTY(Config)
    TArray<FTrainingLight> Lights;

    UPROPERTY(Config)
    FTrainingLight PlateLight;

    UPROPERTY(Config)
    float PressedLightIntensity = 0.0f;

    UPROPERTY(Config)
    FTrainingBlock Door;

    UPROPERTY(Config)
    FTrainingBlock Plate;

    UPROPERTY(Config)
    FVector Spawn = FVector::ZeroVector;

    UPROPERTY(Config)
    FVector CubeSpawn = FVector::ZeroVector;

    UPROPERTY(Config)
    FTrainingBlock ExitZone;

    UPROPERTY(Config)
    FTrainingBlock CubeBounds;

    double GetPlateTop() const { return Plate.Center.Z + Plate.Size.Z * 0.5; }
    FBox GetExitBounds() const { return MakeBounds(ExitZone); }
    FBox GetCubeBounds() const { return MakeBounds(CubeBounds); }

    bool IsValid() const
    {
        return !Room.IsEmpty() && !Room.ContainsByPredicate(
            [](const FTrainingBlock& Block) { return !Block.IsValid(); }) && !Lights.ContainsByPredicate(
            [](const FTrainingLight& Light) { return !Light.IsValid(); }) && PlateLight.IsValid() &&
            FMath::IsFinite(PressedLightIntensity) && PressedLightIntensity >= 0.0f &&
            Door.IsValid() && Plate.IsValid() && !Spawn.ContainsNaN() && !CubeSpawn.ContainsNaN() &&
            ExitZone.IsValid() && CubeBounds.IsValid() && GetCubeBounds().IsInside(CubeSpawn);
    }

private:
    static FBox MakeBounds(const FTrainingBlock& Block)
    {
        return FBox(Block.Center - Block.Size * 0.5, Block.Center + Block.Size * 0.5);
    }
};
