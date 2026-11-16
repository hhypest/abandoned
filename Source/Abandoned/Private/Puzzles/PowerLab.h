#pragma once

#include "Application/Puzzles/OpticsCourse.h"
#include "Application/Puzzles/TimedPassage.h"
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "TrainingChamber/TrainingChamber.h"
#include "PowerLab.generated.h"

class APowerLabCharacter;
class UMaterialInterface;
class UPowerLabLayout;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

enum class EOpticsAim : uint8 { None, Emitter, Mirror };

USTRUCT()
struct FOpticalLane
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UBoxComponent> Emitter;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Receiver;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Door;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> EmitterLamp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ReceiverLamp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> PowerLine;

    FVector Direction = FVector::RightVector;
    Abandoned::Infrastructure::Puzzles::SlidingDoorClamp Clamp;
};

UCLASS()
class APowerLabGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    APowerLabGameMode();
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
    void TryDockMirror();
    void RestoreMirror();
    void AimAt(EOpticsAim Target, FVector Point);
    const AActor* GetAimOwner(EOpticsAim Aim) const;
    bool TraceLane(FOpticalLane& Lane, bool bPowered, bool bAcceptReflection, int32& BeamIndex);
    void DrawBeam(FVector Start, FVector End, int32& BeamIndex);
    bool MoveDoor(UBoxComponent* Door, Abandoned::Infrastructure::Puzzles::SlidingDoorClamp& Clamp,
        const FVector& Closed, bool bOpen, float DeltaSeconds, APowerLabCharacter& Player,
        const FVector* LocalRetry = nullptr);
    bool SpawnPulseBay(UStaticMesh& Mesh, UMaterialInterface& Steel, UMaterialInterface& Panel, UMaterialInterface& Screen);
    void ObservePulse(float DeltaSeconds, const FVector& Location);
    void UpdatePulseDisplay();
    void ResetPulseBay();

#if WITH_DEV_AUTOMATION_TESTS
    void Verify(APowerLabCharacter& Player, float DeltaSeconds);
    void VerifyPulse(APowerLabCharacter& Player, float DeltaSeconds);
    bool bVerify = false;
    bool bVerifyPulse = false;
    bool bCapture = false;
    int32 VerificationStep = 0;
    float VerificationTime = 0.0f;
    int32 PulseTestStep = 0;
    float PulseTestTime = 0.0f;
#endif

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY()
    TObjectPtr<const UPowerLabLayout> Layout;

    UPROPERTY()
    FOpticalLane Interruption;

    UPROPERTY()
    FOpticalLane Reflection;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Button;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ButtonLamp;

    UPROPERTY()
    TObjectPtr<APuzzleCube> Cube;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Mirror;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> MirrorFace;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Cyan;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Amber;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Inactive;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Beams;

    UPROPERTY()
    TObjectPtr<UBoxComponent> PulseDoor;

    UPROPERTY()
    TObjectPtr<UBoxComponent> PulseButton;

    UPROPERTY()
    TObjectPtr<UBoxComponent> PulseReturn;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> PulseIndicators;

    UPROPERTY()
    TObjectPtr<UTextRenderComponent> PulseClock;

    Abandoned::Infrastructure::Puzzles::SlidingDoorClamp PulseClamp;
    Abandoned::Application::Puzzles::OpticsCourse Course;
    Abandoned::Application::Puzzles::TimedPassage Pulse{0.0f};
    int32 DisplayedPulseTenths = -1;
    bool bDirectSignal = false;
    bool bReflectedSignal = false;
    bool bMirrorDocked = false;
};

UCLASS()
class APowerLabCharacter final : public ATrainingChamberCharacter
{
    GENERATED_BODY()

public:
    bool IsAiming() const { return Aim != EOpticsAim::None; }

protected:
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Tick(float DeltaSeconds) override;
    void Interact() override;
    void ResetChamber() override;
    bool InteractWithDevice(UPrimitiveComponent& Target) override;
    void PlaceHeldDevice(UBoxComponent& Body) override;

private:
    friend class APowerLabGameMode;

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
