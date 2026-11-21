#pragma once

#include "CoreMinimal.h"
#include "Infrastructure/Puzzles/PortalTransitComponent.h"
#include "Domain/Puzzles/PowerSignals.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Puzzles/PuzzleDeviceCommands.h"
#include "Player/PuzzleCharacter.h"
#include "PortalLab.generated.h"

class APortalLabCharacter;
class UPortalRoomViewComponent;
class UStaticMesh;
class UPortalLabLayout;
enum class EPuzzleCubeType : uint8;

#if WITH_DEV_AUTOMATION_TESTS
struct FPortalTransitObservation
{
    bool bPowered;
    bool bApproachingArrival;
    bool bInterrupted;
    bool bDev20Crossed;
    int32 Transfers;
    int32 BlockedTransfers;
    FVector Before;
    FVector After;
};
#endif

USTRUCT()
struct FPortalMouth
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UBoxComponent> Shutter;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Console;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> PowerBar;

    FTransform Pose;
};

UCLASS()
class APortalLabGameMode final : public AGameModeBase, public IPuzzleDeviceCommands
{
    GENERATED_BODY()

public:
    APortalLabGameMode();
    bool UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target) override;
    void ResetPuzzle(APuzzleCharacter& Player) override;
    void InitGame(const FString& Map, const FString& Options, FString& Error) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* Controller) override;
    void Tick(float DeltaSeconds) override;
    const TCHAR* GetMessage() const;
#if WITH_DEV_AUTOMATION_TESTS
    FPortalTransitObservation GetTransitObservation() const;
#endif

private:
    friend class APortalLabCharacter;

    EPuzzleCubeType CubeType(int32 Index) const;
    APuzzleCube* SpawnCube(int32 Index);
    void ResetLab(APortalLabCharacter& Player, bool bInterrupted = false);
    void ToggleSupply();
    void RecoverDev20();
    bool UseConsole(const UPrimitiveComponent& Target);
    void UpdateMouths(const APlayerController* Viewer);
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FPortalLabFixture;
#endif

    UPROPERTY()
    TObjectPtr<const UPortalLabLayout> Layout;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Lit;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Unlit;

    UPROPERTY()
    TArray<FPortalMouth> Mouths;

    UPROPERTY()
    TArray<TObjectPtr<APuzzleCube>> Cubes;

    UPROPERTY()
    TObjectPtr<ADev20Actor> Dev20;

    UPROPERTY()
    TObjectPtr<UPortalRoomViewComponent> RoomView;

    UPROPERTY()
    TObjectPtr<UPortalTransitComponent> Transit;

    FPortalTravelerState PlayerState;
    FPortalTravelerState CubeStates[2];
    FPortalTravelerState Dev20State;
    Abandoned::Domain::Puzzles::SignalOutput Supply{Abandoned::Domain::Puzzles::SignalMode::Toggle};
    bool bCrossedWithItem = false;
    bool bInterrupted = false;
    bool bDev20Run = false;
    bool bDev20Crossed = false;
    bool bDev20Departed = false;
    bool bDev20Recovered = false;
};

UCLASS()
class APortalLabCharacter final : public APuzzleCharacter
{
    GENERATED_BODY()

private:
    friend class APortalLabGameMode;
};

UCLASS()
class APortalLabHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
