#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "PlateBalanceVerification.generated.h"

class APuzzleCube;
class ATrainingChamberGameMode;
class UPointLightComponent;
class UStaticMeshComponent;
struct FTrainingPressurePlate;

UCLASS()
class UPlateBalanceVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UPlateBalanceVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static bool IsStandaloneRequested();
    static void Attach(ATrainingChamberGameMode& Chamber, const TArray<TObjectPtr<APuzzleCube>>& Cubes,
        const TArray<FTrainingPressurePlate>& Plates);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase
    {
        Empty, EqualUnderload, SetupUneven, Uneven, SetupFirst, AimAtPlate, PlaceOnPlate, StackOnPlate,
        FirstSolution, CarryOntoStack, Overload, Reopened, AfterReset, SecondSolution, Capture, ApproachExit, WalkToExit
    };
    struct FPlateDisplay
    {
        TWeakObjectPtr<UStaticMeshComponent> Visual;
        TWeakObjectPtr<UPointLightComponent> Indicator;
        FVector RestLocation;
    };
    void Step(EPhase Next);
    void Fail(const TCHAR* Reason);
    bool IsReset(const ATrainingChamberGameMode& Chamber) const;
    EPhase Phase = EPhase::Empty;
    double PhaseSeconds = 0.0;
    TArray<TWeakObjectPtr<APuzzleCube>> Cubes;
    TArray<FPlateDisplay> Displays;
    FVerificationKeyPress Input;
#endif
};
