#pragma once

#include <optional>

#include "CoreMinimal.h"
#include "Application/Expedition/MantleRequest.h"
#include "Application/Expedition/SuitSession.h"
#include "Application/Expedition/SuitTerminal.h"
#include "Application/Expedition/WorldRoute.h"
#include "Domain/Expedition/EntryRoute.h"
#include "Domain/Expedition/FallImpact.h"
#include "Domain/Expedition/RadiationExposure.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Expedition/UnrealMantle.h"
#include "Player/AbandonedCharacter.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VanguardRoute.generated.h"

class AVanguardCharacter;
class UAudioComponent;
class UBoxComponent;
class UMaterialInterface;
class USpotLightComponent;
class UStaticMeshComponent;
class UVanguardRadiationLayout;
class UVanguardRouteLayout;

UCLASS()
class UVanguardSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    Abandoned::Application::Expedition::SuitSession Suit;
#if WITH_DEV_AUTOMATION_TESTS
    bool bRecoveredFromRadiation = false;
#endif
};

UCLASS()
class AVanguardRouteGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    AVanguardRouteGameMode();
    void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
    void Tick(float DeltaSeconds) override;
    bool AtStation() const;
    bool HasResupplied() const { return bResupplied; }
    bool HasClearedRadiation() const { return bClearedRadiation; }
    bool HasCheckpointWriteFailed() const { return bCheckpointWriteFailed; }
    float GetOxygenPercent() const;
    std::optional<Abandoned::Domain::Expedition::DosimeterReading> GetDosimeter() const;
    bool HasMinorInjuryNotice() const { return InjuryRecovery.IsActive(); }
    UBoxComponent* GetSupplyCabinet() const { return SupplyCabinet; }
    void Resupply();

private:
    friend class AVanguardCharacter;
    UVanguardSession* GetSession() const;
    Abandoned::Application::Expedition::SuitTelemetry GetSuitTelemetry() const;
    void RecordLanding(double SurfaceHeight);
    bool TickRadiation(const FVector& Position, float DeltaSeconds, Abandoned::Application::Expedition::SuitSession& Suit);
    void RestartRadiationPassage();
    void TickPassage(AVanguardCharacter& Player, float DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
    void Verify(AVanguardCharacter& Player, float DeltaSeconds);
    friend class UVanguardFallVerification;
    TWeakObjectPtr<class UVanguardFallVerification> FallCheck;
    void VerifyRadiation(AVanguardCharacter& Player, float DeltaSeconds);
    bool VerifyWristTerminal(AVanguardCharacter& Player, float DeltaSeconds);
    TWeakObjectPtr<class UVanguardMantleVerification> MantleCheck;
    enum class ERadiationVerification
    {
        Gate, WaitForFlare, LeaveShelter, ReturnToShelter, HoldInShelter, RecoverOutside, LethalExposure, RunToStation
    };
    bool bVerifyRoute = false;
    bool bVerifyRadiation = false;
    bool bCaptureRadiation = false;
    bool bVerifyWristTerminal = false;
    bool bCaptureWristTerminal = false;
    int TerminalInputStep = 0;
    float TerminalVerificationSeconds = 0.0f;
    bool bCaptureRoute = false;
    float VerificationSeconds = 0.0f;
    bool bCaptured = false;
    bool bCapturedOpenDoor = false;
    bool bDoorPassed = false;
    bool bMovedToSupply = false;
    int FlashlightInputStep = 0;
    ERadiationVerification RadiationVerification = ERadiationVerification::Gate;
    double ProtectedDose = 0.0;
    float ShelterSeconds = 0.0f;
#endif

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY(Transient)
    TObjectPtr<const UVanguardRouteLayout> Layout;

    UPROPERTY(Transient)
    TObjectPtr<const UVanguardRadiationLayout> RadiationLayout;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Door;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> SupplyCabinet;

    Abandoned::Application::Expedition::VanguardDestination Destination;
    std::optional<Abandoned::Application::Expedition::WorldDestination> RecoveryDestination;
    bool bTravelling = false;
    bool bResupplied = false;
    bool bClearedRadiation = false;
    bool bCheckpointWriteFailed = false;
    std::optional<double> LastGroundHeight;
    Abandoned::Domain::Expedition::FallImpact LandingImpact = Abandoned::Domain::Expedition::FallImpact::None;
    Abandoned::Domain::Expedition::MinorInjuryRecovery InjuryRecovery;
    std::optional<Abandoned::Domain::Expedition::RadiationExposure> Radiation;
    Abandoned::Domain::Expedition::RadiationPosition RadiationPosition = Abandoned::Domain::Expedition::RadiationPosition::Outside;
};

UCLASS()
class AVanguardCharacter final : public AAbandonedCharacter
{
    GENERATED_BODY()

public:
    AVanguardCharacter();
    bool CanReachSupply() const;
    bool IsFlashlightOn() const;
    bool CanMantle() const { return bMantleAvailable; }
    bool IsMantling() const { return ActiveMantle.has_value(); }
    bool IsWristTerminalOpen() const { return WristTerminal.Page().has_value(); }
    void Interact();
    void Jump() override;
    void StopJumping() override;

private:
    friend class AVanguardRouteGameMode;
    friend class AVanguardHUD;
    void ToggleFlashlight();
    void PlayInjuryBreath();
#if WITH_DEV_AUTOMATION_TESTS
    friend class UVanguardMantleVerification;
    friend class UVanguardFallVerification;
#endif
    void ResetRadiation();
    void CreateWristTerminal();
    void ConfigureWristTerminal(UMaterialInterface* Surface);
    void BindWristTerminalInput(UEnhancedInputComponent& Input);
    void ToggleWristTerminal();
    void CloseWristTerminal();
    void AdvanceTerminalPage();
    void NextArchiveRecord();
    void PreviousArchiveRecord();
    void MoveArchive(bool Forward);
    void UpdateWristTerminal(const AVanguardRouteGameMode& Mode);
    void UpdateMantle(float DeltaSeconds, std::optional<double> SupportHeight);
    void FinishMantle();
    void CreateMantleVisuals();
    void LoadMantleVisuals();
    void UpdateMantleHands();
    Abandoned::Application::Expedition::MantleRequest MantleRequest;
    Abandoned::Application::Expedition::SuitTerminal WristTerminal;
    FString WristHeading;
    FString WristReadout;
    std::optional<Abandoned::Infrastructure::Expedition::MantlePath> ActiveMantle;
    std::optional<Abandoned::Infrastructure::Expedition::MantleHandhold> ActiveMantleHandhold;
    double MantleSeconds = 0.0;
    bool bMantleAvailable = false;

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<USpotLightComponent> ShoulderLight;

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<UAudioComponent> InjuryBreath;

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<UStaticMeshComponent> MantleGloves[2];

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<UStaticMeshComponent> MantleSleeves[2];

    UPROPERTY()
    TObjectPtr<USceneComponent> WristRoot;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WristCasing;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WristDisplay;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WristSleeve;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WristGlove;

protected:
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void UnPossessed() override;
    void Landed(const FHitResult& Hit) override;
};

UCLASS()
class AVanguardHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;

private:
    void DrawWristReadout(const AVanguardCharacter& Player);
};
