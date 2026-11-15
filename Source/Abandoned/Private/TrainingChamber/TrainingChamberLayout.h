#pragma once

#include "CoreMinimal.h"
#include "Puzzles/PuzzleCube.h"
#include "World/SceneLayout.h"
#include "TrainingChamberLayout.generated.h"

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

UCLASS(Config=Game, DefaultConfig)
class UTrainingChamberLayout : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    TArray<FSceneBlock> Room;

    UPROPERTY(Config)
    TArray<FSceneLight> Lights;

    UPROPERTY(Config)
    TArray<FSceneFinish> Finishes;

    UPROPERTY(Config)
    FString Title = TEXT("TRAINING CHAMBER");

    UPROPERTY(Config)
    FString Objective = TEXT("Weigh down the pressure plate");

    UPROPERTY(Config)
    bool bRequiresStack = false;

    UPROPERTY(Config)
    bool bRequiresButton = false;

    UPROPERTY(Config)
    FSceneBlock Button;

    UPROPERTY(Config)
    FSceneBlock StackZone;

    UPROPERTY(Config)
    FSceneLight PlateLight;

    UPROPERTY(Config)
    float PressedLightIntensity = 0.0f;

    UPROPERTY(Config)
    FSceneBlock Door;

    UPROPERTY(Config)
    FSceneBlock Plate;

    UPROPERTY(Config)
    TArray<FSceneBlock> AdditionalPlates;

    UPROPERTY(Config)
    float BalanceTotalKg = 0.0f;

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
    FSceneBlock ExitZone;

    UPROPERTY(Config)
    FSceneBlock CubeBounds;

    UPROPERTY(Config)
    bool bDispenseCubes = false;

    double GetPlateTop() const { return Plate.Center.Z + Plate.Size.Z * 0.5; }
    bool UsesPlateBalance() const { return !AdditionalPlates.IsEmpty(); }
    int32 GetPlateCount() const { return 1 + AdditionalPlates.Num(); }
    const FSceneBlock& GetPlateAt(int32 Index) const { return Index == 0 ? Plate : AdditionalPlates[Index - 1]; }
    FBox GetExitBounds() const { return MakeBounds(ExitZone); }
    FBox GetCubeBounds() const { return MakeBounds(CubeBounds); }
    FBox GetStackBounds() const { return MakeBounds(StackZone); }

    bool IsValid() const
    {
        return !Room.IsEmpty() && !Room.ContainsByPredicate(
            [](const FSceneBlock& Block) { return !Block.IsValid(); }) && !Lights.ContainsByPredicate(
            [](const FSceneLight& Light) { return !Light.IsValid(); }) && PlateLight.IsValid() &&
            !Finishes.ContainsByPredicate([](const FSceneFinish& Finish) { return !Finish.IsValid(); }) &&
            FMath::IsFinite(PressedLightIntensity) && PressedLightIntensity >= 0.0f &&
            Door.IsValid() && Plate.IsValid() && !Spawn.ContainsNaN() && ExitZone.IsValid() &&
            CubeBounds.IsValid() && AreBalancePlatesValid() &&
            (UsesPlateBalance() || (FMath::IsFinite(PlateActivationMassKg) && PlateActivationMassKg > 0.0f &&
            FMath::IsFinite(PlateMaximumMassKg) && PlateMaximumMassKg >= PlateActivationMassKg)) &&
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
    bool AreBalancePlatesValid() const
    {
        if (!UsesPlateBalance())
        {
            return BalanceTotalKg == 0.0f;
        }
        if (bRequiresButton || bRequiresStack || !FMath::IsFinite(BalanceTotalKg) || BalanceTotalKg <= 0.0f)
        {
            return false;
        }
        for (int32 Index = 0; Index < GetPlateCount(); ++Index)
        {
            const FSceneBlock& Block = GetPlateAt(Index);
            if (!Block.IsValid() || !Block.bCollision || !GetCubeBounds().IsInside(Block.Center))
            {
                return false;
            }
            const FBox Bounds = MakeBounds(Block);
            for (int32 Previous = 0; Previous < Index; ++Previous)
            {
                if (Bounds.IntersectXY(MakeBounds(GetPlateAt(Previous))))
                {
                    return false;
                }
            }
        }
        return true;
    }

    static FBox MakeBounds(const FSceneBlock& Block)
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

UCLASS(Config=Game, DefaultConfig)
class UWeightBalanceLayout final : public UTrainingChamberLayout
{
    GENERATED_BODY()
};
