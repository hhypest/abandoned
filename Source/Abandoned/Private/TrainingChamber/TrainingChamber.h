#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Application/Expedition/EntryCourse.h"
#include "Application/Puzzles/ChamberHintReader.h"
#include "Domain/Expedition/Dev20.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "TrainingChamber/ChamberAssembly.h"
#include "Infrastructure/Puzzles/TrainingPlateCircuitComponent.h"
#include "Infrastructure/Puzzles/PuzzleDeviceCommands.h"
#include "Player/PuzzleCharacter.h"
#include "TrainingChamber.generated.h"

class ATrainingChamberCharacter;
class ADev20Actor;
class APuzzleCube;
class UBoxComponent;
class UMaterialInterface;
class ULevelStreamingDynamic;
class AVanguardSegment;
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

    UBoxComponent* GetPlate(int32 Index = 0) const { return Devices ? Devices->PlateCircuit->GetPlate(Index) : nullptr; }
    bool UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target) override;
    void ResetPuzzle(APuzzleCharacter& Player) override;
    const TCHAR* DevicePrompt(const UPrimitiveComponent& Target) const override;
    UBoxComponent* GetButton() const { return Devices->Button; }
    UBoxComponent* GetDoor() const { return Devices ? Devices->DoorDrive->GetDoor() : nullptr; }
    UBoxComponent* GetHintTerminal() const { return Devices->HintTerminal; }
    const Abandoned::Application::Puzzles::ChamberHintReader& GetHints() const { return HintReader; }
    void ReadNextHint() { HintReader.Activate(); }
    const UTrainingChamberLayout* GetLayout() const { return Layout; }
    bool IsButtonLatched() const { return Devices->PlateCircuit->GetState().ButtonLatched; }
    bool IsParallelRelayPowered() const { return Devices->PlateCircuit->GetState().ParallelOutput; }
    float GetMeasuredMass(int32 Index = 0) const { return Devices->PlateCircuit->GetMeasuredMass(Index); }
    const TArray<float>& GetPlateLoads() const { return Devices->PlateCircuit->GetLoads(); }
    bool IsExitEnabled() const { return Devices->PlateCircuit->IsExitEnabled(); }
    Abandoned::Application::Expedition::CheckpointIssue GetCheckpointIssue() const { return Course.Issue; }
    bool HasCheckpointWriteFailed() const { return ArrivalWrite.IsReady() && !ArrivalWrite.Get(); }
    void PressButton();
#if WITH_DEV_AUTOMATION_TESTS
    bool RevealChamberForVerification();
#endif
    bool IsPlatePressed(int32 Index = 0) const { return Devices->PlateCircuit->IsPressed(Index); }
    bool IsWaitingForChamber() const { return NeedsAirlock() && !IncomingChamber && !Passage; }
    bool HasChamberLoadFailed() const { return bChamberLoadFailed; }
    bool IsDoorOpen() const { return Devices->DoorDrive->IsOpen(); }
    bool HasClampedObject() const { return Devices->DoorDrive->HasClampedObject(); }
    bool HasReachedExit() const { return bReachedExit; }
    bool IsDev20Run() const { return bDev20Run; }
    const TCHAR* GetDev20Status(const ATrainingChamberCharacter& Player) const;
    void ResetChamber(ATrainingChamberCharacter& Player);

private:
    void ReturnCube(ATrainingChamberCharacter& Player, int32 Index, bool bUseDispenser = false);
    void ObserveHints(const ATrainingChamberCharacter& Player, float DeltaSeconds);
    void ActivateChamber();
    void RequestNextChamber();
    void PrepareNextChamber();
    void RetireOutgoingChamber();
    bool EnterNextChamber(ATrainingChamberCharacter& Player);
    bool NeedsAirlock() const;
    bool bChamberLoadFailed = false;
    bool bChamberRetryRequested = false;
    UPROPERTY(Transient)
    TObjectPtr<ULevelStreamingDynamic> IncomingInstance;
    UPROPERTY(Transient)
    TObjectPtr<ULevelStreamingDynamic> ActiveInstance;
    UPROPERTY(Transient)
    TObjectPtr<AChamberAssembly> IncomingChamber;
    UPROPERTY(Transient)
    TObjectPtr<AVanguardSegment> Passage;
    void SelectLayout(const Abandoned::Application::Expedition::WorldDestination& Destination);
    void SaveEntryCheckpoint();
    TSharedFuture<bool> ArrivalWrite;
    void AdvanceCourse(const ATrainingChamberCharacter& Player);

    UPROPERTY(Transient)
    TObjectPtr<AChamberAssembly> Devices;

    UPROPERTY(Transient)
    TObjectPtr<const UTrainingChamberLayout> Layout;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    UPROPERTY(Transient)
    TObjectPtr<ADev20Actor> Dev20;

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
