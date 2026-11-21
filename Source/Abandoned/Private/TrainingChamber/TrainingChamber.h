#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Application/Expedition/EntryCourse.h"
#include "Application/Puzzles/ChamberHintReader.h"
#include "Domain/Expedition/Dev20.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Puzzles/TrainingDoorDriveComponent.h"
#include "Infrastructure/Puzzles/TrainingPlateCircuitComponent.h"
#include "Infrastructure/Puzzles/PuzzleDeviceCommands.h"
#include "Player/PuzzleCharacter.h"
#include "TrainingChamber.generated.h"

class ATrainingChamberCharacter;
class ADev20Actor;
class APuzzleCube;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTrainingChamberLayout;

UCLASS()
class ATrainingChamberGameMode final : public AGameModeBase, public IPuzzleDeviceCommands
{
    GENERATED_BODY()

public:
    ATrainingChamberGameMode();
    void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    void Tick(float DeltaSeconds) override;

    UBoxComponent* GetPlate(int32 Index = 0) const { return PlateCircuit->GetPlate(Index); }
    bool UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target) override;
    void ResetPuzzle(APuzzleCharacter& Player) override;
    const TCHAR* DevicePrompt(const UPrimitiveComponent& Target) const override;
    UBoxComponent* GetButton() const { return Button; }
    UBoxComponent* GetDoor() const { return DoorDrive->GetDoor(); }
    UBoxComponent* GetHintTerminal() const { return HintTerminal; }
    const Abandoned::Application::Puzzles::ChamberHintReader& GetHints() const { return HintReader; }
    void ReadNextHint() { HintReader.Activate(); }
    const UTrainingChamberLayout* GetLayout() const { return Layout; }
    bool IsButtonLatched() const { return PlateCircuit->GetState().ButtonLatched; }
    bool IsParallelRelayPowered() const { return PlateCircuit->GetState().ParallelOutput; }
    float GetMeasuredMass(int32 Index = 0) const { return PlateCircuit->GetMeasuredMass(Index); }
    const TArray<float>& GetPlateLoads() const { return PlateCircuit->GetLoads(); }
    bool IsExitEnabled() const { return PlateCircuit->IsExitEnabled(); }
    Abandoned::Application::Expedition::CheckpointIssue GetCheckpointIssue() const { return Course.Issue; }
    bool HasCheckpointWriteFailed() const { return ArrivalWrite.IsReady() && !ArrivalWrite.Get(); }
    void PressButton();
    bool IsPlatePressed(int32 Index = 0) const { return PlateCircuit->IsPressed(Index); }
    bool IsDoorOpen() const { return DoorDrive->IsOpen(); }
    bool HasClampedObject() const { return DoorDrive->HasClampedObject(); }
    bool HasReachedExit() const { return bReachedExit; }
    bool IsDev20Run() const { return bDev20Run; }
    const TCHAR* GetDev20Status(const ATrainingChamberCharacter& Player) const;
    void ResetChamber(ATrainingChamberCharacter& Player);

private:
    void ReturnCube(ATrainingChamberCharacter& Player, int32 Index, bool bUseDispenser = false);
    void ObserveHints(const ATrainingChamberCharacter& Player, float DeltaSeconds);
    bool SpawnRelayTraces(UStaticMesh& Mesh, UMaterialInterface* Idle, UMaterialInterface* Powered);
    void UpdateRelayTraces();
    void SelectLayout(const Abandoned::Application::Expedition::WorldDestination& Destination);
    void SaveEntryCheckpoint();
    TSharedFuture<bool> ArrivalWrite;
    void AdvanceCourse(const ATrainingChamberCharacter& Player);

    UPROPERTY(Transient)
    TArray<TObjectPtr<APuzzleCube>> Cubes;

    UPROPERTY(Transient)
    TObjectPtr<ADev20Actor> Dev20;

    UPROPERTY(Transient)
    TObjectPtr<UTrainingPlateCircuitComponent> PlateCircuit;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> RelayTraces;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> RelayIdleMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> RelayPoweredMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Button;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> HintTerminal;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> ButtonVisual;

    UPROPERTY(Transient)
    TObjectPtr<const UTrainingChamberLayout> Layout;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UTrainingDoorDriveComponent> DoorDrive;

    Abandoned::Application::Expedition::CourseStart Course;
    Abandoned::Application::Puzzles::ChamberHintReader HintReader;
    bool bReachedExit = false;
    bool bDev20Run = false;
    bool bDev20Recovering = false;
    std::optional<Abandoned::Domain::Expedition::Dev20Stage> Dev20Stage;
};

UCLASS()
class ATrainingChamberCharacter final : public APuzzleCharacter
{
    GENERATED_BODY()

public:
    bool CanReadHintTerminal() const;

private:
    friend class ATrainingChamberGameMode;
};

UCLASS()
class ATrainingChamberHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;

private:
    void DrawHintPage(const UTrainingChamberLayout& Layout, int Page, int AvailablePages);
};
