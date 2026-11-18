#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "ChamberHintsVerification.generated.h"

class ATrainingChamberGameMode;
class UEntryWeightLayout;

UCLASS()
class UChamberHintsVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UChamberHintsVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static bool IsRequested();
    static UEntryWeightLayout* CreateFixtureLayout(ATrainingChamberGameMode& Chamber);
    static void Attach(ATrainingChamberGameMode& Chamber);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase
    {
        OpenTerminal, CheckFirstPage, CheckLockedPages, ArmSwitch, WaitForSecondEntry,
        ReadSecondEntry, LookAway, WaitForThirdEntry, ReadThirdEntry, Reset, CheckReset
    };
    void Fail(const TCHAR* Reason);
    EPhase Phase = EPhase::OpenTerminal;
    double Elapsed = 0.0;
    double SwitchPressedAt = 0.0;
    int ExpectedPage = 1;
    FVerificationKeyPress Input;
#endif
};
