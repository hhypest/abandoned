#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Player/AbandonedCharacter.h"
#include "MovementLab.generated.h"

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
class AMovementLabCharacter final : public AAbandonedCharacter
{
    GENERATED_BODY()

public:
    int32 GetTrialIndex() const { return TrialIndex; }
    const FString& GetResult() const { return Result; }

protected:
    void BeginPlay() override;
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Tick(float DeltaSeconds) override;
    void OnJumped_Implementation() override;
    void Landed(const FHitResult& Hit) override;

private:
    void ChangeTrial(int32 Offset);
    void ResetTrial();
    void ReportJump(const TCHAR* Outcome);
    void MeasureTrial(float DeltaSeconds);
    void AdvanceMeasurement();

    int32 TrialIndex = 0;
    bool bJumpInProgress = false;
    bool bReachedTarget = false;
    FVector LaunchPosition = FVector::ZeroVector;
    double LaunchSpeed = 0.0;
    double PeakZ = 0.0;
    FString Result;

    bool bMeasuring = false;
    bool bRequestedJump = false;
    bool bMeasuredTakeoff = false;
    bool bCrouchedInAir = false;
    int32 Variant = 0;
    float Elapsed = 0.0f;
};

UCLASS()
class AMovementLabHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
