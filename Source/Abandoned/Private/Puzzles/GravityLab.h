#pragma once

#include "CoreMinimal.h"
#include "Domain/Puzzles/GravityField.h"
#include "Domain/Puzzles/PowerSignals.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Puzzles/PuzzleDeviceCommands.h"
#include "Player/PuzzleCharacter.h"
#include "GravityLab.generated.h"

class AGravityLabCharacter;
class UGravityLabLayout;

UCLASS()
class AGravityLabGameMode final : public AGameModeBase, public IPuzzleDeviceCommands
{
    GENERATED_BODY()

public:
    AGravityLabGameMode();
    bool UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target) override;
    void ResetPuzzle(APuzzleCharacter& Player) override;
    void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* Controller) override;
    void Tick(float DeltaSeconds) override;
    const TCHAR* GetDirectionLabel() const;
    bool IsPowered() const { return Power.IsPowered(); }
    bool HasReachedGoal() const { return bReachedGoal; }

private:
    friend class AGravityLabCharacter;
    using Direction = Abandoned::Domain::Puzzles::GravityDirection;

    void ResetLab(AGravityLabCharacter& Player);
    void ApplyField(APuzzleCube& Cube, const Abandoned::Domain::Puzzles::GravityField* Field);
    void UpdateFieldMarkers();
#if WITH_DEV_AUTOMATION_TESTS
    friend class UGravityLabVerification;
#endif

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY()
    TObjectPtr<const UGravityLabLayout> Layout;

    UPROPERTY()
    TObjectPtr<UBoxComponent> PowerSwitch;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Terminal;

    UPROPERTY()
    TObjectPtr<APuzzleCube> Small;

    UPROPERTY()
    TObjectPtr<APuzzleCube> Large;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Inactive;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Violet;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> FieldMarkers;

    Abandoned::Domain::Puzzles::SignalOutput Power{Abandoned::Domain::Puzzles::SignalMode::Toggle};
    Direction Selected = Direction::Down;
    bool bReachedGoal = false;
};

UCLASS()
class AGravityLabCharacter final : public APuzzleCharacter
{
    GENERATED_BODY()

public:
    AGravityLabCharacter();
    FRotator GetViewRotation() const override;
    void AddControllerYawInput(float Value) override;
    void AddControllerPitchInput(float Value) override;
    bool HasSmoothHorizon() const { return bSmoothHorizon; }

protected:
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Move(FVector LocalDirection) override;
    void Tick(float DeltaSeconds) override;

private:
    friend class AGravityLabGameMode;
#if WITH_DEV_AUTOMATION_TESTS
    friend class UGravityLabVerification;
#endif
    using Direction = Abandoned::Domain::Puzzles::GravityDirection;

    void ChangeGravity(Direction Down, float Scale);
    void PlaceAtLab(FVector Start, FVector LookAt);
    void UpdateView(float Blend);
    void SelectDirection(Direction Value);
    void ToggleHorizon() { bSmoothHorizon = !bSmoothHorizon; }

    Direction Current = Direction::Down;
    FQuat FieldFrame = FQuat::Identity;
    FQuat ViewFrame = FQuat::Identity;
    double Pitch = 0.0;
    double Yaw = 0.0;
    bool bSmoothHorizon = true;
};

UCLASS()
class AGravityLabHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
