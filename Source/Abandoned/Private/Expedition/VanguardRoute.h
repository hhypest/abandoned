#pragma once

#include "CoreMinimal.h"
#include "Domain/Expedition/EntryRoute.h"
#include "Domain/Expedition/SuitSupply.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Player/AbandonedCharacter.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VanguardRoute.generated.h"

class AVanguardCharacter;
class UBoxComponent;
class UMaterialInterface;
class UVanguardRouteLayout;

UCLASS()
class UVanguardSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    Abandoned::Domain::Expedition::SuitSupply Suit;
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
    UBoxComponent* GetSupplyCabinet() const { return SupplyCabinet; }
    void Resupply();

private:
    UVanguardSession* GetSession() const;
    void TickPassage(AVanguardCharacter& Player, float DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
    void Verify(AVanguardCharacter& Player, float DeltaSeconds);
    bool bVerifyRoute = false;
    bool bCaptureRoute = false;
    float VerificationSeconds = 0.0f;
    bool bCaptured = false;
    bool bCapturedOpenDoor = false;
    bool bDoorPassed = false;
    bool bMovedToSupply = false;
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
};

UCLASS()
class AVanguardCharacter final : public AAbandonedCharacter
{
    GENERATED_BODY()

public:
    bool CanReachSupply() const;
    void Interact();

private:
    friend class AVanguardRouteGameMode;

protected:
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
};

UCLASS()
class AVanguardHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
