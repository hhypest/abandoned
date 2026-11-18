#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Puzzles/PuzzleDeviceCommands.h"
#include "Player/PuzzleCharacter.h"
#include "PowerLab.generated.h"

class APowerLabCharacter;
class UOpticalCircuitComponent;
class UOpticalReflectorComponent;
class UPowerLabDoorsComponent;
class UPowerLabLayout;

enum class EOpticsAim : uint8 { None, Emitter, Mirror };

UCLASS()
class APowerLabGameMode final : public AGameModeBase, public IPuzzleDeviceCommands
{
    GENERATED_BODY()

public:
    APowerLabGameMode();
    bool UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target) override;
    void ResetPuzzle(APuzzleCharacter& Player) override;
    void PlaceDevice(APuzzleCharacter& Player, UBoxComponent& Body) override;
    void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
    void Tick(float DeltaSeconds) override;
    const TCHAR* GetObjective(const FVector& Player) const;
    bool IsInPulseBay(const FVector& Player) const;
    float GetPulseSeconds() const;
    float GetPulseFraction() const;

private:
    friend class APowerLabCharacter;

    bool Interact(APowerLabCharacter& Player, UPrimitiveComponent& Target);
    void ResetLab(APowerLabCharacter& Player);
#if WITH_DEV_AUTOMATION_TESTS
    friend class UOpticsVerification;
    friend class UPulseDoorVerification;
#endif

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY()
    TObjectPtr<const UPowerLabLayout> Layout;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Button;

    UPROPERTY()
    TObjectPtr<APuzzleCube> Cube;

    UPROPERTY()
    TObjectPtr<UOpticalCircuitComponent> Optics;

    UPROPERTY()
    TObjectPtr<UOpticalReflectorComponent> Reflector;

    UPROPERTY()
    TObjectPtr<UPowerLabDoorsComponent> Doors;
};

UCLASS()
class APowerLabCharacter final : public APuzzleCharacter
{
    GENERATED_BODY()

public:
    bool IsAiming() const { return Aim != EOpticsAim::None; }

protected:
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Tick(float DeltaSeconds) override;
    void Interact() override;

private:
    friend class APowerLabGameMode;
#if WITH_DEV_AUTOMATION_TESTS
    friend class UOpticsVerification;
    friend class UPulseDoorVerification;
#endif

    void AimMirror();
    EOpticsAim Aim = EOpticsAim::None;
};

UCLASS()
class APowerLabHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
