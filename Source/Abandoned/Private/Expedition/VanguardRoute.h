#pragma once

#include <optional>

#include "CoreMinimal.h"
#include "Application/Expedition/MantleRequest.h"
#include "Domain/Expedition/EntryRoute.h"
#include "Domain/Expedition/FallImpact.h"
#include "Domain/Expedition/RadiationExposure.h"
#include "Domain/Expedition/SuitSupply.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Expedition/UnrealMantle.h"
#include "Player/AbandonedCharacter.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VanguardRoute.generated.h"

class AVanguardCharacter;
class UBoxComponent;
class UMaterialInterface;
class USpotLightComponent;
class UVanguardRadiationLayout;
class UVanguardRouteLayout;

UCLASS()
class UVanguardSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    Abandoned::Domain::Expedition::SuitSupply Suit;
    Abandoned::Domain::Expedition::RadiationDose Dose;
    bool bFlashlightOn = false;
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
    float GetOxygenPercent() const;
    std::optional<Abandoned::Domain::Expedition::DosimeterReading> GetDosimeter() const;
    bool HasMinorInjuryNotice() const { return InjuryNoticeSeconds > 0.0f; }
    UBoxComponent* GetSupplyCabinet() const { return SupplyCabinet; }
    void Resupply();

private:
    friend class AVanguardCharacter;
    UVanguardSession* GetSession() const;
    void RecordLanding(double SurfaceHeight);
    bool TickRadiation(const FVector& Position, float DeltaSeconds, Abandoned::Domain::Expedition::RadiationDose& Dose);
    void RestartRadiationPassage();
    void TickPassage(AVanguardCharacter& Player, float DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
    void Verify(AVanguardCharacter& Player, float DeltaSeconds);
    void VerifyFalls(AVanguardCharacter& Player, float DeltaSeconds);
    void VerifyRadiation(AVanguardCharacter& Player, float DeltaSeconds);
    void VerifyMantle(AVanguardCharacter& Player, float DeltaSeconds);
    enum class ERadiationVerification
    {
        Gate, WaitForFlare, LeaveShelter, ReturnToShelter, HoldInShelter, RecoverOutside, LethalExposure, RunToStation
    };
    bool bVerifyRoute = false;
    bool bVerifyFalls = false;
    bool bVerifyRadiation = false;
    bool bCaptureRadiation = false;
    bool bCaptureRoute = false;
    float VerificationSeconds = 0.0f;
    bool bCaptured = false;
    bool bCapturedOpenDoor = false;
    bool bDoorPassed = false;
    bool bMovedToSupply = false;
    int FlashlightInputStep = 0;
    int FallTestStep = 0;
    bool bPlacedAtTestPlatform = false;
    bool bWalkedOffTestPlatform = false;
    ERadiationVerification RadiationVerification = ERadiationVerification::Gate;
    double ProtectedDose = 0.0;
    float ShelterSeconds = 0.0f;
    enum class EMantleVerification
    {
        Approach, ReleaseTap, WaitForLanding, DuckRunUp, DuckJump, DuckLanding, PlaceBlocker, BlockedHold, Hold, PullUp, WaitForTop, Done
    };
    EMantleVerification MantleVerification = EMantleVerification::Approach;
    float MantleVerificationSeconds = 0.0f;
    bool bTapLeftGround = false;
    bool bDuckedInAir = false;
    bool bCapturedMantleApproach = false;
    TWeakObjectPtr<AActor> MantleTestBlocker;
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

    Abandoned::Domain::Expedition::Segment Current = Abandoned::Domain::Expedition::Segment::Passage;
    bool bTravelling = false;
    bool bResupplied = false;
    bool bClearedRadiation = false;
    std::optional<double> LastGroundHeight;
    Abandoned::Domain::Expedition::FallImpact LandingImpact = Abandoned::Domain::Expedition::FallImpact::None;
    float InjuryNoticeSeconds = 0.0f;
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
    void Interact();
    void Jump() override;
    void StopJumping() override;

private:
    friend class AVanguardRouteGameMode;
    void ToggleFlashlight();
    void ResetRadiation();
    void UpdateMantle(float DeltaSeconds, std::optional<double> SupportHeight);
    void FinishMantle();
    Abandoned::Application::Expedition::MantleRequest MantleRequest;
    std::optional<Abandoned::Infrastructure::Expedition::MantlePath> ActiveMantle;
    double MantleSeconds = 0.0;
    bool bMantleAvailable = false;

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<USpotLightComponent> ShoulderLight;

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
};
