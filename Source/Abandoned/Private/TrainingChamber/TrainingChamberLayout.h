#pragma once

#include "CoreMinimal.h"
#include "Puzzles/PuzzleCube.h"
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

    UPROPERTY()
    FName Finish = NAME_None;

    bool IsValid() const
    {
        return !Center.ContainsNaN() && !Size.ContainsNaN() && Size.GetMin() > 0.0;
    }
};

USTRUCT()
struct FTrainingFinish
{
    GENERATED_BODY()

    UPROPERTY()
    FName Name = NAME_None;

    UPROPERTY()
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY()
    float Roughness = 0.5f;

    UPROPERTY()
    float Metallic = 0.0f;

    UPROPERTY()
    float Glow = 0.0f;

    bool IsValid() const
    {
        return !Name.IsNone() && FMath::IsFinite(Roughness) && FMath::IsWithinInclusive(Roughness, 0.0f, 1.0f) &&
            FMath::IsFinite(Metallic) && FMath::IsWithinInclusive(Metallic, 0.0f, 1.0f) &&
            FMath::IsFinite(Glow) && Glow >= 0.0f && FMath::IsFinite(Tint.R) && FMath::IsFinite(Tint.G) &&
            FMath::IsFinite(Tint.B) && FMath::IsFinite(Tint.A);
    }
};

USTRUCT()
struct FTrainingCubeSpawn
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Center = FVector::ZeroVector;

    UPROPERTY()
    EPuzzleCubeType Type = EPuzzleCubeType::Small;

    UPROPERTY()
    FVector Outlet = FVector::ZeroVector;
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
class UTrainingChamberLayout : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    TArray<FTrainingBlock> Room;

    UPROPERTY(Config)
    TArray<FTrainingLight> Lights;

    UPROPERTY(Config)
    TArray<FTrainingFinish> Finishes;

    UPROPERTY(Config)
    FString Title = TEXT("TRAINING CHAMBER");

    UPROPERTY(Config)
    FString Objective = TEXT("Weigh down the pressure plate");

    UPROPERTY(Config)
    bool bRequiresStack = false;

    UPROPERTY(Config)
    bool bRequiresButton = false;

    UPROPERTY(Config)
    FTrainingBlock Button;

    UPROPERTY(Config)
    FTrainingBlock StackZone;

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
    TArray<FTrainingCubeSpawn> CubeSpawns;

    UPROPERTY(Config)
    float PlateActivationMassKg = 0.0f;

    UPROPERTY(Config)
    float PlateMaximumMassKg = 1000.0f;

    UPROPERTY(Config)
    float PlayerMassKg = 85.0f;

    UPROPERTY(Config)
    float ThrowImpulse = 4200.0f;

    UPROPERTY(Config)
    FLinearColor PlacementGhostTint = FLinearColor(0.10, 0.82, 1.0, 0.42);

    UPROPERTY(Config)
    FLinearColor BlockedGhostTint = FLinearColor(0.90, 0.10, 0.14, 0.48);

    UPROPERTY(Config)
    FTrainingBlock ExitZone;

    UPROPERTY(Config)
    FTrainingBlock CubeBounds;

    UPROPERTY(Config)
    bool bDispenseCubes = false;

    double GetPlateTop() const { return Plate.Center.Z + Plate.Size.Z * 0.5; }
    FBox GetExitBounds() const { return MakeBounds(ExitZone); }
    FBox GetCubeBounds() const { return MakeBounds(CubeBounds); }
    FBox GetStackBounds() const { return MakeBounds(StackZone); }

    bool IsValid() const
    {
        return !Room.IsEmpty() && !Room.ContainsByPredicate(
            [](const FTrainingBlock& Block) { return !Block.IsValid(); }) && !Lights.ContainsByPredicate(
            [](const FTrainingLight& Light) { return !Light.IsValid(); }) && PlateLight.IsValid() &&
            !Finishes.ContainsByPredicate([](const FTrainingFinish& Finish) { return !Finish.IsValid(); }) &&
            FMath::IsFinite(PressedLightIntensity) && PressedLightIntensity >= 0.0f &&
            Door.IsValid() && Plate.IsValid() && !Spawn.ContainsNaN() && ExitZone.IsValid() && CubeBounds.IsValid() &&
            FMath::IsFinite(PlateActivationMassKg) && PlateActivationMassKg > 0.0f &&
            FMath::IsFinite(PlateMaximumMassKg) && PlateMaximumMassKg >= PlateActivationMassKg &&
            (!bRequiresButton || Button.IsValid()) && (!bRequiresStack || StackZone.IsValid()) &&
            FMath::IsFinite(PlayerMassKg) && PlayerMassKg > 0.0f &&
            FMath::IsFinite(ThrowImpulse) && ThrowImpulse > 0.0f && !CubeSpawns.IsEmpty() &&
            !CubeSpawns.ContainsByPredicate([this](const FTrainingCubeSpawn& Cube)
            {
                return Cube.Center.ContainsNaN() || !GetCubeBounds().IsInside(Cube.Center) ||
                    (bDispenseCubes && (Cube.Outlet.ContainsNaN() || !GetCubeBounds().IsInside(Cube.Outlet))) ||
                    !APuzzleCube::IsValidType(Cube.Type);
            });
    }

private:
    static FBox MakeBounds(const FTrainingBlock& Block)
    {
        return FBox(Block.Center - Block.Size * 0.5, Block.Center + Block.Size * 0.5);
    }
};

UCLASS(Config=Game, DefaultConfig)
class UEntryStairsLayout final : public UTrainingChamberLayout
{
    GENERATED_BODY()
};

UCLASS(Config=Game, DefaultConfig)
class UEntryButtonLayout final : public UTrainingChamberLayout
{
    GENERATED_BODY()
};

UCLASS(Config=Game, DefaultConfig)
class UEntryWeightLayout final : public UTrainingChamberLayout
{
    GENERATED_BODY()
};
