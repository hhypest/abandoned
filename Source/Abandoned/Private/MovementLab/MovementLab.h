#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "MovementLab.generated.h"

class UCameraComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS()
class AMovementLabGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMovementLabGameMode();
    void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};

UCLASS()
class AMovementLabCharacter final : public ACharacter
{
    GENERATED_BODY()

public:
    AMovementLabCharacter();

    int32 GetTrialIndex() const { return TrialIndex; }
    const FString& GetResult() const { return Result; }

protected:
    void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    void UnPossessed() override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void Tick(float DeltaSeconds) override;
    void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    void OnJumped_Implementation() override;
    void Landed(const FHitResult& Hit) override;

private:
    void Move(FVector LocalDirection);
    void Look(const FInputActionValue& Value);
    void SetSprint(bool bHeld);
    void SetSlow(bool bHeld);
    void UpdateSpeed();
    void StartCrouch();
    void StopCrouch();
    void ChangeTrial(int32 Offset);
    void ResetTrial();
    void RemoveInputMapping();
    void ReportJump(const TCHAR* Outcome);

    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> InputMapping;

    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;

    int32 TrialIndex = 0;
    bool bSprintHeld = false;
    bool bSlowHeld = false;
    bool bJumpInProgress = false;
    bool bReachedTarget = false;
    FVector LaunchPosition = FVector::ZeroVector;
    double LaunchSpeed = 0.0;
    double PeakZ = 0.0;
    FString Result;
};

UCLASS()
class AMovementLabHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
