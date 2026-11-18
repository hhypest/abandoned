#pragma once

#include "CoreMinimal.h"
#include "Domain/Puzzles/PortalTransit.h"
#include "Domain/Puzzles/PowerSignals.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Puzzles/PuzzleDeviceCommands.h"
#include "Player/PuzzleCharacter.h"
#include "PortalLab.generated.h"

class APortalLabCharacter;
class USceneCaptureComponent2D;
class UStaticMesh;
class UTextureRenderTarget2D;
class UPortalLabLayout;
enum class EPuzzleCubeType : uint8;

#if WITH_DEV_AUTOMATION_TESTS
struct FPortalTransitObservation
{
    bool bPowered;
    bool bApproachingArrival;
    bool bInterrupted;
    bool bDev20Crossed;
    int32 Transfers;
    FVector Before;
    FVector After;
};
#endif

USTRUCT()
struct FPortalMouth
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UBoxComponent> Shutter;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Console;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Window;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> PowerBar;

    FTransform Pose;
};

UCLASS()
class APortalLabGameMode final : public AGameModeBase, public IPuzzleDeviceCommands
{
    GENERATED_BODY()

public:
    APortalLabGameMode();
    bool UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target) override;
    void ResetPuzzle(APuzzleCharacter& Player) override;
    void InitGame(const FString& Map, const FString& Options, FString& Error) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* Controller) override;
    void Tick(float DeltaSeconds) override;
    const TCHAR* GetMessage() const;
#if WITH_DEV_AUTOMATION_TESTS
    FPortalTransitObservation GetTransitObservation() const;
#endif

private:
    friend class APortalLabCharacter;

    struct FTraveler
    {
        FVector Previous = FVector::ZeroVector;
        int32 Approach = INDEX_NONE;
        bool bTracked = false;
        bool bUnsupportedShape = false;
    };

    EPuzzleCubeType CubeType(int32 Index) const;
    APuzzleCube* SpawnCube(int32 Index);
    void ResetLab(APortalLabCharacter& Player, bool bInterrupted = false);
    void ToggleSupply();
    void RecoverDev20();
    bool UseConsole(const UPrimitiveComponent& Target);
    void UpdateMouths();
    void CaptureView(const APortalLabCharacter& Player);
    void ObserveTraveler(AActor& Actor, UPrimitiveComponent& Body, FTraveler& State,
        APortalLabCharacter* Player, int32 CubeIndex = INDEX_NONE);
    bool ExitClear(const UPrimitiveComponent& Body, const FTransform& Destination,
        const UPrimitiveComponent* Other) const;

#if WITH_DEV_AUTOMATION_TESTS
    void Verify(APortalLabCharacter& Player, float DeltaSeconds);
    void InspectTransfer(const FVector& Before, const FVector& After);
    bool bVerify = false;
    bool bCapture = false;
    bool bRequestedFirstPortalFrame = false;
    int32 VerificationStep = 0;
    float VerificationTime = 0.0f;
    int32 Transfers = 0;
    int32 BlockedTransfers = 0;
    FVector LastBefore = FVector::ZeroVector;
    FVector LastAfter = FVector::ZeroVector;
    TWeakObjectPtr<APuzzleCube> VerifyItem;
#endif

    UPROPERTY()
    TObjectPtr<const UPortalLabLayout> Layout;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> BaseMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> WindowMaterial;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Lit;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Unlit;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Ballast;

    UPROPERTY()
    TArray<FPortalMouth> Mouths;

    UPROPERTY()
    TArray<TObjectPtr<APuzzleCube>> Cubes;

    UPROPERTY()
    TObjectPtr<ADev20Actor> Dev20;

    UPROPERTY()
    TObjectPtr<USceneCaptureComponent2D> Capture;

    UPROPERTY()
    TObjectPtr<UTextureRenderTarget2D> ViewTexture;

    FTraveler PlayerState;
    FTraveler CubeStates[2];
    FTraveler Dev20State;
    Abandoned::Domain::Puzzles::SignalOutput Supply{Abandoned::Domain::Puzzles::SignalMode::Toggle};
    bool bCrossedWithItem = false;
    bool bInterrupted = false;
    bool bExitOccupied = false;
    bool bDev20Run = false;
    bool bDev20Crossed = false;
    bool bDev20Departed = false;
    bool bDev20Recovered = false;
};

UCLASS()
class APortalLabCharacter final : public APuzzleCharacter
{
    GENERATED_BODY()

private:
    friend class APortalLabGameMode;
};

UCLASS()
class APortalLabHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
};
