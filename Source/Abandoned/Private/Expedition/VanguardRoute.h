#pragma once

#include <optional>

#include "CoreMinimal.h"
#include "Application/Expedition/MantleRequest.h"
#include "Application/Expedition/SuitTerminal.h"
#include "Application/Expedition/WorldRoute.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Expedition/UnrealMantle.h"
#include "Player/AbandonedCharacter.h"
#include "VanguardRoute.generated.h"

class AVanguardCharacter;
class AVanguardSegment;
class UAudioComponent;
class UBoxComponent;
class UMaterialInterface;
class USpotLightComponent;
class UStaticMeshComponent;
class UVanguardRouteStateComponent;
class UVanguardRouteLayout;

UCLASS()
class AVanguardRouteGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    AVanguardRouteGameMode();
    void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* Player) override;
};

// The same route rules serve a dedicated Vanguard world and a streamed campaign segment.
UCLASS()
class AVanguardSegment final : public AActor
{
    GENERATED_BODY()

public:
    AVanguardSegment();
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable, Category="Passage")
    bool BakePassageRoom(bool bStation = false);
#endif
    bool PollPassageAssets();
    void Prepare(Abandoned::Application::Expedition::VanguardDestination Destination, FString& Error);
    bool HasPassageSupport() const;
    static AVanguardSegment* ActiveIn(UWorld& World);
    FVector PlayerStart() const;
    bool StationLoadFailed() const { return bStationFailed; }
#if WITH_DEV_AUTOMATION_TESTS
    bool RevealStationForVerification();
    const UBoxComponent* GetStationGate() const { return StationGate; }
#endif
    void Activate();
    void Equip(AVanguardCharacter& Player);
    void Tick(float DeltaSeconds) override;
    const UVanguardRouteStateComponent& GetRouteState() const { return *RouteState; }
    UBoxComponent* GetSupplyCabinet() const { return SupplyCabinet; }
    void Resupply();

private:
    friend class AVanguardCharacter;
    UPROPERTY()
    TArray<TObjectPtr<class AStaticMeshActor>> PlacedRoom;
    TSharedPtr<struct FStreamableHandle> AssetRequest;
    bool bRequestedAssets = false;
    bool bActive = false;
    bool bStationFailed = false;
    bool PollStation();
    bool StationReady() const;
    bool EnterStation(AVanguardCharacter& Player);
    UPROPERTY(Transient)
    TObjectPtr<class ULevelStreamingDynamic> StationInstance;
    UPROPERTY(Transient)
    TObjectPtr<AVanguardSegment> IncomingStation;
    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> StationGate;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UBoxComponent>> Ledges;
#if WITH_DEV_AUTOMATION_TESTS
    friend class UVanguardFallVerification;
    TWeakObjectPtr<class UVanguardFallVerification> FallCheck;
    TWeakObjectPtr<class UVanguardRadiationVerification> RadiationCheck;
    TWeakObjectPtr<class UVanguardRouteVerification> RouteCheck;
#endif

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY(Transient)
    TObjectPtr<const UVanguardRouteLayout> Layout;

    UPROPERTY(Transient)
    TObjectPtr<UVanguardRouteStateComponent> RouteState;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Door;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> SupplyCabinet;
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
    friend class AVanguardSegment;
    UPROPERTY(Transient)
    TObjectPtr<AVanguardSegment> Segment;
    friend class AVanguardHUD;
    void ToggleFlashlight();
    void PlayInjuryBreath();
#if WITH_DEV_AUTOMATION_TESTS
    friend class UVanguardRadiationVerification;
    friend class UVanguardRouteVerification;
    friend class UVanguardWristTerminalVerification;
    friend class UVanguardMantleVerification;
    friend class UVanguardFallVerification;
#endif
    void ResetRadiation();
    void CreateWristTerminal();
    void ConfigureWristTerminal();
    void BindWristTerminalInput(UEnhancedInputComponent& Input);
    void ToggleWristTerminal();
    void CloseWristTerminal();
    void AdvanceTerminalPage();
    void NextArchiveRecord();
    void PreviousArchiveRecord();
    void MoveArchive(bool Forward);
    void UpdateWristTerminal(const UVanguardRouteStateComponent& Route);
    void UpdatePassageMovement(UVanguardRouteStateComponent& Route, bool bEnableMantle, float DeltaSeconds);
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
