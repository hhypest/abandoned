#pragma once

#include "CoreMinimal.h"
#include "Domain/Puzzles/ChamberHints.h"
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

UENUM()
enum class ETrainingRelaySignal : uint8
{
    PlateA,
    PlateB,
    Drive
};

USTRUCT()
struct FTrainingRelayTrace
{
    GENERATED_BODY()

    UPROPERTY()
    FSceneBlock Block;

    UPROPERTY()
    ETrainingRelaySignal Signal = ETrainingRelaySignal::PlateA;
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
    FSceneBlock HintTerminal;

    UPROPERTY(Config)
    TArray<FString> HintPages;

    UPROPERTY(Config)
    double HintDelaySeconds = 360.0;

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
    bool bUseParallelPlates = false;

    UPROPERTY(Config)
    TArray<FTrainingRelayTrace> RelayTraces;

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
    FSceneBlock ExitZone;

    UPROPERTY(Config)
    FSceneBlock CubeBounds;

    UPROPERTY(Config)
    bool bDispenseCubes = false;

    double GetPlateTop() const { return Plate.Center.Z + Plate.Size.Z * 0.5; }
    bool UsesParallelPlates() const { return bUseParallelPlates; }
    bool UsesPlateBalance() const { return !UsesParallelPlates() && !AdditionalPlates.IsEmpty(); }
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
            CubeBounds.IsValid() && AreMultiplePlatesValid() && AreHintsValid() &&
            (UsesPlateBalance() || (FMath::IsFinite(PlateActivationMassKg) && PlateActivationMassKg > 0.0f &&
            FMath::IsFinite(PlateMaximumMassKg) && PlateMaximumMassKg >= PlateActivationMassKg)) &&
            (!bRequiresButton || Button.IsValid()) && (!bRequiresStack || StackZone.IsValid()) &&
            FMath::IsFinite(PlayerMassKg) && PlayerMassKg > 0.0f &&
            !CubeSpawns.IsEmpty() &&
            !CubeSpawns.ContainsByPredicate([this](const FTrainingCubeSpawn& Cube)
            {
                return Cube.Center.ContainsNaN() || !GetCubeBounds().IsInside(Cube.Center) ||
                    (bDispenseCubes && (Cube.Outlet.ContainsNaN() || !GetCubeBounds().IsInside(Cube.Outlet))) ||
                    !APuzzleCube::IsValidType(Cube.Type);
            });
    }

private:
    bool AreHintsValid() const
    {
        using Abandoned::Domain::Puzzles::ChamberHints;
        return HintPages.IsEmpty() || (HintPages.Num() == ChamberHints::PageCount && HintTerminal.IsValid() &&
            HintTerminal.bCollision && FMath::IsFinite(HintDelaySeconds) && HintDelaySeconds > 0.0 &&
            GetPlateCount() <= Abandoned::Domain::Puzzles::MaximumHintPlates &&
            !HintPages.ContainsByPredicate([](const FString& Page) { return Page.TrimStartAndEnd().IsEmpty(); }));
    }

    bool AreMultiplePlatesValid() const
    {
        if (UsesParallelPlates())
        {
            const auto HasTrace = [this](ETrainingRelaySignal Signal)
            {
                return RelayTraces.ContainsByPredicate([Signal](const FTrainingRelayTrace& Trace)
                    { return Trace.Signal == Signal; });
            };
            if (AdditionalPlates.Num() != 1 || !bRequiresButton || bRequiresStack || BalanceTotalKg != 0.0f ||
                !HasTrace(ETrainingRelaySignal::PlateA) || !HasTrace(ETrainingRelaySignal::PlateB) ||
                !HasTrace(ETrainingRelaySignal::Drive) ||
                RelayTraces.ContainsByPredicate([](const FTrainingRelayTrace& Trace)
                {
                    return !Trace.Block.IsValid() || Trace.Block.bCollision || Trace.Signal > ETrainingRelaySignal::Drive;
                }))
            {
                return false;
            }
        }
        else if (AdditionalPlates.IsEmpty())
        {
            return BalanceTotalKg == 0.0f && RelayTraces.IsEmpty();
        }
        else if (bRequiresButton || bRequiresStack || !FMath::IsFinite(BalanceTotalKg) || BalanceTotalKg <= 0.0f ||
            !RelayTraces.IsEmpty())
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
