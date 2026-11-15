#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Player/AbandonedCharacter.h"
#include "TrainingChamber.generated.h"

class ATrainingChamberCharacter;
class APuzzleCube;
class UBoxComponent;
class UPhysicsHandleComponent;
class UPointLightComponent;
class UStaticMeshComponent;

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
    bool IsPlatePressed() const { return bPlatePressed; }
    bool IsDoorOpen() const;
    bool HasReachedExit() const { return bReachedExit; }
    void ResetChamber(ATrainingChamberCharacter& Player);

private:
    void ReturnCube(ATrainingChamberCharacter& Player, int32 Index);
    float GetPlateMass(const ATrainingChamberCharacter& Player) const;
    void SetPlatePressed(bool bPressed);

#if WITH_DEV_AUTOMATION_TESTS
    enum class EVerificationPhase
    {
        Initial, StandOnPlate, JumpOffPlate, LookUp, LookDown, Carry, Open, Cross, Close, Throw,
        Return, CarryToStack, StartAscent, JumpToStack, JumpToLedge, RemoveSupport
    };
    void VerifyChamber(ATrainingChamberCharacter& Player, float DeltaSeconds);
    EVerificationPhase VerificationPhase = EVerificationPhase::Initial;
    float VerificationElapsed = 0.0f;
    bool bVerifyChamber = false;
    bool bGhostScreenshotRequested = false;
#endif

    UPROPERTY(Transient)
    TArray<TObjectPtr<APuzzleCube>> Cubes;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Plate;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Door;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> PlateVisual;

    UPROPERTY(Transient)
    TObjectPtr<UPointLightComponent> PlateIndicator;

    float PlateContactSeconds = 0.0f;
    bool bPlatePressed = false;
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
    };
    bool FindPlacement(const APuzzleCube& Cube, FPlacement& Placement) const;
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
};

UCLASS()
class ATrainingChamberHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
