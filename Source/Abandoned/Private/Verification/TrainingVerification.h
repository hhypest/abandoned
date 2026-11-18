#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "TrainingVerification.generated.h"

class APuzzleCube;
class ATrainingChamberCharacter;
class ATrainingChamberGameMode;
class UPointLightComponent;
class UStaticMeshComponent;
struct FTrainingPressurePlate;

UCLASS()
class UTrainingVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UTrainingVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static bool IsRequested();
    static void Attach(ATrainingChamberGameMode& Chamber, const TArray<TObjectPtr<APuzzleCube>>& Cubes,
        const TArray<FTrainingPressurePlate>& Plates);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase
    {
        Initial, StandOnPlate, CheckPlateReset, StandAfterReset, JumpOffPlate, LookUp, LookDown, Pickup, Carry,
        CheckPlacement, Open, Cross, CheckDetachedPlate, Close, Throw, CheckThrowReset, Return, PickupReturned,
        CarryToStack, CheckStack, StartAscent, JumpToStack, JumpToLedge, RemoveSupport, CheckDetachedStack, FinalReset
    };
    void Verify(const ATrainingChamberGameMode& Chamber, ATrainingChamberCharacter& Player, APlayerController& Controller);
    void Advance(EPhase Next, const TCHAR* Message);
    void Fail(const TCHAR* Reason);
    void StopMouseInput();

    EPhase Phase = EPhase::Initial;
    double Elapsed = 0.0;
    TWeakObjectPtr<APuzzleCube> Cubes[3];
    TWeakObjectPtr<UStaticMeshComponent> PlateVisual;
    TWeakObjectPtr<UPointLightComponent> PlateIndicator;
    TWeakObjectPtr<APlayerController> MouseOwner;
    FVerificationKeyPress Input;
    FVector ExpectedPlacement = FVector::ZeroVector;
    FVector OnPlate = FVector::ZeroVector;
    bool bCapturedGhost = false;
#endif
};
