#pragma once

#include "CoreMinimal.h"
#include "Application/Expedition/EntryCourse.h"
#include "Application/Puzzles/ChamberHintReader.h"
#include "Domain/Expedition/Dev20.h"
#include "Domain/Puzzles/ChamberCircuit.h"
#include "Domain/Puzzles/PlateBalance.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"
#include "Player/AbandonedCharacter.h"
#include "TrainingChamber.generated.h"

class ATrainingChamberCharacter;
class ADev20Actor;
class APuzzleCube;
class UBoxComponent;
class UMaterialInterface;
class UPhysicsHandleComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UTrainingChamberLayout;

USTRUCT()
struct FTrainingPressurePlate
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Body;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Visual;

    UPROPERTY(Transient)
    TObjectPtr<UPointLightComponent> Indicator;

    bool bPressed = false;
};

UCLASS()
class ATrainingChamberGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    ATrainingChamberGameMode();
    void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    void Tick(float DeltaSeconds) override;

    UBoxComponent* GetPlate() const { return Plates.IsEmpty() ? nullptr : Plates[0].Body.Get(); }
    bool IsPressurePlate(const UBoxComponent* Body) const;
    UBoxComponent* GetButton() const { return Button; }
    UBoxComponent* GetHintTerminal() const { return HintTerminal; }
    const Abandoned::Application::Puzzles::ChamberHintReader& GetHints() const { return HintReader; }
    void ReadNextHint() { HintReader.Activate(); }
    const UTrainingChamberLayout* GetLayout() const { return Layout; }
    bool IsButtonLatched() const { return Circuit.GetState().ButtonLatched; }
    float GetMeasuredMass() const { return PlateLoads.IsEmpty() ? 0.0f : PlateLoads[0]; }
    const TArray<float>& GetPlateLoads() const { return PlateLoads; }
    bool IsExitEnabled() const;
    bool DidCheckpointFallback() const { return Course.CheckpointFallback; }
    void PressButton();
    bool IsPlatePressed() const { return !Plates.IsEmpty() && Plates[0].bPressed; }
    bool IsDoorOpen() const;
    bool HasClampedObject() const { return DoorClamp.HasObject(); }
    bool HasReachedExit() const { return bReachedExit; }
    bool IsDev20Run() const { return bDev20Run; }
    const TCHAR* GetDev20Status(const ATrainingChamberCharacter& Player) const;
    void ResetChamber(ATrainingChamberCharacter& Player);

private:
    void ReturnCube(ATrainingChamberCharacter& Player, int32 Index, bool bUseDispenser = false);
    float GetPlateMass(const ATrainingChamberCharacter& Player, int32 Index) const;
    void ObserveCircuit(const ATrainingChamberCharacter& Player, float DeltaSeconds);
    void ObserveHints(const ATrainingChamberCharacter& Player, float DeltaSeconds);
    bool MoveDoor(ATrainingChamberCharacter& Player, float DeltaSeconds);
    void SetPlatePressed(int32 Index, bool bPressed);
    bool HasStackAtExit() const;
    void SelectLayout(const FString& Options);
    void SaveEntryCheckpoint();
    void AdvanceCourse(const ATrainingChamberCharacter& Player);

#if WITH_DEV_AUTOMATION_TESTS
    enum class EVerificationPhase
    {
        Initial, StandOnPlate, StandAfterReset, JumpOffPlate, LookUp, LookDown, Carry, Open, Cross, Close, Throw,
        Return, CarryToStack, StartAscent, JumpToStack, JumpToLedge, RemoveSupport
    };
    void VerifyChamber(ATrainingChamberCharacter& Player, float DeltaSeconds);
    void VerifyEntryChamber(ATrainingChamberCharacter& Player, float DeltaSeconds);
    void VerifyBalanceChamber(ATrainingChamberCharacter& Player, float DeltaSeconds);
    void VerifyHints(ATrainingChamberCharacter& Player, float DeltaSeconds);
    void VerifyDoorClamp(ATrainingChamberCharacter& Player, float DeltaSeconds);
    EVerificationPhase VerificationPhase = EVerificationPhase::Initial;
    float VerificationElapsed = 0.0f;
    bool bVerifyChamber = false;
    bool bVerificationScreenshotRequested = false;
    bool bVerifyEntryCourse = false;
    bool bVerifyEntryCheckpoint = false;
    bool bVerifiedWeightReset = false;
    bool bVerifyDev20 = false;
    bool bVerifyBalance = false;
    bool bVerifyHints = false;
    bool bVerifyDoorClamp = false;
    enum class EDoorClampTest { PowerOn, Open, Clamp, Hold, Release, Reopen, ClampForReset, OpenForPlayer, CrushPlayer };
    EDoorClampTest DoorClampTest = EDoorClampTest::PowerOn;
    double DoorClampTestSeconds = 0.0;
    FVector ClampedObjectPosition = FVector::ZeroVector;
    FVector ObstructedDoorPosition = FVector::ZeroVector;
    double HintTestSeconds = 0.0;
    int HintTestStep = 0;
    int32 Dev20TestPhase = 0;
    int32 CourseTestStep = 0;
    float CourseTestTime = 0.0f;
#endif

    UPROPERTY(Transient)
    TArray<TObjectPtr<APuzzleCube>> Cubes;

    UPROPERTY(Transient)
    TObjectPtr<ADev20Actor> Dev20;

    UPROPERTY(Transient)
    TArray<FTrainingPressurePlate> Plates;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Button;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> HintTerminal;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> ButtonVisual;

    UPROPERTY(Transient)
    TObjectPtr<const UTrainingChamberLayout> Layout;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Door;

    TArray<float> PlateLoads;
    Abandoned::Application::Expedition::CourseStart Course;
    Abandoned::Domain::Puzzles::ChamberCircuit Circuit;
    Abandoned::Domain::Puzzles::PlateBalance Balance;
    Abandoned::Application::Puzzles::ChamberHintReader HintReader;
    Abandoned::Infrastructure::Puzzles::SlidingDoorClamp DoorClamp;
    bool bReachedExit = false;
    bool bDev20Run = false;
    bool bDev20Recovering = false;
    std::optional<Abandoned::Domain::Expedition::Dev20Stage> Dev20Stage;
};

UCLASS()
class ATrainingChamberCharacter : public AAbandonedCharacter
{
    GENERATED_BODY()

public:
    ATrainingChamberCharacter();
    APuzzleCube* GetHeldCube() const;
    ADev20Actor* GetHeldDev20() const;
    APuzzleCube* FindReachableCube() const;
    const TCHAR* GetPlacementPrompt() const;
    bool CanReadHintTerminal() const;
    void ReleaseHeldObject();

protected:
    void BeginPlay() override;
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Tick(float DeltaSeconds) override;
    void UnPossessed() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Interact();
    virtual void ResetChamber();
    virtual bool InteractWithDevice(UPrimitiveComponent&) { return false; }
    virtual void PlaceHeldDevice(UBoxComponent&) {}
    UPrimitiveComponent* TraceInteractable() const;
    UBoxComponent* GetHeldBody() const;
    bool GrabBody(UBoxComponent& Body, float MassKg);
    bool TransitHeldBody(const FTransform& Through);

private:
    friend class ATrainingChamberGameMode;
    struct FPlacement
    {
        UBoxComponent* Support = nullptr;
        FVector Center;
        FRotator Rotation;
        const TCHAR* BlockReason = nullptr;
    };
    bool FindPlacement(const APuzzleCube& Cube, FPlacement& Placement) const;
    void UpdatePlacementGhost(const APuzzleCube* Held);
    void ThrowHeldObject();
    ATrainingChamberGameMode* GetChamber() const;

    UPROPERTY()
    TObjectPtr<UPhysicsHandleComponent> Grip;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> PlacementGhost;

    TWeakObjectPtr<UBoxComponent> PreviewSupport;
    bool bPreviewBlocked = false;
    const TCHAR* PreviewBlockReason = nullptr;
};

UCLASS()
class ATrainingChamberHUD final : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;

private:
    void DrawHintPage(const UTrainingChamberLayout& Layout, int Page, int AvailablePages);
};
