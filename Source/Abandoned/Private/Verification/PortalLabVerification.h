#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "PortalLabVerification.generated.h"

class APortalLabGameMode;
class APuzzleCube;
struct FPortalLabFixture;

UCLASS()
class UPortalLabVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UPortalLabVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static void AttachIfRequested(APortalLabGameMode& Lab);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase
    {
        Closed, Power, View, ReadCapture, LaunchCube, FreeTransit, BlockedExit, Pickup, LaunchPlayer, CarriedTransit,
        WaitAtExit, ReverseTransit, ReturnToEntrance, CutPower, Reset
    };
    void Check(FPortalLabFixture& Fixture);
    void Next(EPhase NextPhase, const TCHAR* Message);
    void Fail(const TCHAR* Reason);
    EPhase Phase = EPhase::Closed;
    float Elapsed = 0.0f;
    TWeakObjectPtr<APuzzleCube> HeldCube;
    FVerificationKeyPress Input;
    bool bCapture = false;
    bool bCapturedFirstFrame = false;
#endif
};
