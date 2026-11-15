#pragma once

#include "CoreMinimal.h"
#include "Application/Expedition/EntryCourse.h"
#include "Domain/Puzzles/ChamberCircuit.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Player/AbandonedCharacter.h"
#include "TrainingChamber.generated.h"

class ATrainingChamberCharacter;
class APuzzleCube;
class UBoxComponent;
class UMaterialInterface;
class UPhysicsHandleComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UTrainingChamberLayout;

UCLASS()
class ATrainingChamberGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    ATrainingChamberGameMode();
    void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    void Tick(float DeltaSeconds) override;

    UBoxComponent* GetPlate() const { return Plate; }
    UBoxComponent* GetButton() const { return Button; }
    const UTrainingChamberLayout* GetLayout() const { return Layout; }
    bool IsButtonLatched() const { return Circuit.GetState().ButtonLatched; }
    float GetMeasuredMass() const { return MeasuredMassKg; }
    bool IsExitEnabled() const { return Circuit.GetState().ExitEnabled; }
    bool DidCheckpointFallback() const { return Course.CheckpointFallback; }
    void PressButton();
    bool IsPlatePressed() const { return Circuit.GetState().PlatePressed; }
    bool IsDoorOpen() const;
    bool HasReachedExit() const { return bReachedExit; }
    void ResetChamber(ATrainingChamberCharacter& Player);

private:
    void ReturnCube(ATrainingChamberCharacter& Player, int32 Index, bool bUseDispenser = false);
    float GetPlateMass(const ATrainingChamberCharacter& Player) const;
    void SetPlatePressed(bool bPressed);
    bool HasStackAtExit() const;
    void SelectLayout(const FString& Options);
    void SaveEntryCheckpoint();
    void AdvanceCourse();

#if WITH_DEV_AUTOMATION_TESTS
    enum class EVerificationPhase
    {
        Initial, StandOnPlate, JumpOffPlate, LookUp, LookDown, Carry, Open, Cross, Close, Throw,
        Return, CarryToStack, StartAscent, JumpToStack, JumpToLedge, RemoveSupport
    };
    void VerifyChamber(ATrainingChamberCharacter& Player, float DeltaSeconds);
    void VerifyEntryChamber(ATrainingChamberCharacter& Player, float DeltaSeconds);
    EVerificationPhase VerificationPhase = EVerificationPhase::Initial;
    float VerificationElapsed = 0.0f;
    bool bVerifyChamber = false;
    bool bVerificationScreenshotRequested = false;
    bool bVerifyEntryCourse = false;
    int32 CourseTestStep = 0;
    float CourseTestTime = 0.0f;
#endif

    UPROPERTY(Transient)
    TArray<TObjectPtr<APuzzleCube>> Cubes;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Plate;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Button;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> ButtonVisual;

    UPROPERTY(Transient)
    TObjectPtr<const UTrainingChamberLayout> Layout;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Door;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> PlateVisual;

    UPROPERTY(Transient)
    TObjectPtr<UPointLightComponent> PlateIndicator;

    float MeasuredMassKg = 0.0f;
    Abandoned::Application::Expedition::CourseStart Course;
    Abandoned::Domain::Puzzles::ChamberCircuit Circuit;
    bool bReachedExit = false;
};

UCLASS()
class ATrainingChamberCharacter final : public AAbandonedCharacter
{
    GENERATED_BODY()

public:
    ATrainingChamberCharacter();
    APuzzleCube* GetHeldCube() const;
    APuzzleCube* FindReachableCube() const;
    const TCHAR* GetPlacementPrompt() const;
    void ReleaseCube();

protected:
    void BeginPlay() override;
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Tick(float DeltaSeconds) override;
    void UnPossessed() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    friend class ATrainingChamberGameMode;
    struct FPlacement
    {
        UBoxComponent* Support = nullptr;
        FVector Center;
        FRotator Rotation;
        const TCHAR* BlockReason = nullptr;
    };
    bool FindPlacement(const APuzzleCube& Cube, FPlacement& Placement) const;
    UPrimitiveComponent* TraceInteractable() const;
    void UpdatePlacementGhost(const APuzzleCube* Held);
    void Interact();
    void ThrowHeldObject();
    void ResetChamber();
    ATrainingChamberGameMode* GetChamber() const;

    UPROPERTY()
    TObjectPtr<UPhysicsHandleComponent> Grip;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> PlacementGhost;

    TWeakObjectPtr<UBoxComponent> PreviewSupport;
    bool bPreviewBlocked = false;
    const TCHAR* PreviewBlockReason = nullptr;
};

UCLASS()
class ATrainingChamberHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
