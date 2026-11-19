#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "PortalDev20Verification.generated.h"

class ADev20Actor;
class APuzzleCube;
class APortalLabGameMode;
class UBoxComponent;
class UPortalLabLayout;

UCLASS()
class UPortalDev20Verification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UPortalDev20Verification();
#if WITH_DEV_AUTOMATION_TESTS
    static void AttachIfRequested(APortalLabGameMode& Lab, const UPortalLabLayout& Layout,
        ADev20Actor& Unit, UBoxComponent& Console, APuzzleCube& ExitBlocker, bool bRecoveryArrival);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase { Inspect, Power, Take, CheckGrip, Approach, Interrupted, BlockedExit, Launch, Cross, Depart, RecoveryArrival };
    void Advance(EPhase Next);
    void Fail(const TCHAR* Reason);

    TWeakObjectPtr<const UPortalLabLayout> Layout;
    TWeakObjectPtr<ADev20Actor> Unit;
    TWeakObjectPtr<UBoxComponent> Console;
    TWeakObjectPtr<APuzzleCube> Blocker;
    FVerificationKeyPress Input;
    EPhase Phase = EPhase::Inspect;
    float Elapsed = 0.0f;
    int32 BlockedBeforeApproach = 0;
    bool bPowerLossVerified = false;
    bool bCapture = false;
#endif
};
