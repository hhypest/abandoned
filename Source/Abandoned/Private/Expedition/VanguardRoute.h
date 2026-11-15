#pragma once

#include <optional>

#include "CoreMinimal.h"
#include "Domain/Expedition/EntryRoute.h"
#include "Domain/Expedition/FallImpact.h"
#include "Domain/Expedition/SuitSupply.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Player/AbandonedCharacter.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VanguardRoute.generated.h"

class AVanguardCharacter;
class UBoxComponent;
class UMaterialInterface;
class USpotLightComponent;
class UVanguardRouteLayout;

UCLASS()
class UVanguardSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    Abandoned::Domain::Expedition::SuitSupply Suit;
    bool bFlashlightOn = false;
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
    float GetOxygenPercent() const;
    bool HasMinorInjuryNotice() const { return InjuryNoticeSeconds > 0.0f; }
    UBoxComponent* GetSupplyCabinet() const { return SupplyCabinet; }
    void Resupply();

private:
    friend class AVanguardCharacter;
    UVanguardSession* GetSession() const;
    void RecordLanding(double SurfaceHeight);
    void TickPassage(AVanguardCharacter& Player, float DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
    void Verify(AVanguardCharacter& Player, float DeltaSeconds);
    void VerifyFalls(AVanguardCharacter& Player, float DeltaSeconds);
    bool bVerifyRoute = false;
    bool bVerifyFalls = false;
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
#endif

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY(Transient)
    TObjectPtr<const UVanguardRouteLayout> Layout;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Door;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> SupplyCabinet;

    Abandoned::Domain::Expedition::Segment Current = Abandoned::Domain::Expedition::Segment::Passage;
    bool bTravelling = false;
    bool bResupplied = false;
    std::optional<double> LastGroundHeight;
    Abandoned::Domain::Expedition::FallImpact LandingImpact = Abandoned::Domain::Expedition::FallImpact::None;
    float InjuryNoticeSeconds = 0.0f;
};

UCLASS()
class AVanguardCharacter final : public AAbandonedCharacter
{
    GENERATED_BODY()

public:
    AVanguardCharacter();
    bool CanReachSupply() const;
    bool IsFlashlightOn() const;
    void Interact();

private:
    friend class AVanguardRouteGameMode;
    void ToggleFlashlight();

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<USpotLightComponent> ShoulderLight;

protected:
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Landed(const FHitResult& Hit) override;
};

UCLASS()
class AVanguardHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
