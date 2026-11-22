#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "VanguardMantleVerification.generated.h"

class AVanguardCharacter;
struct FSceneBlock;

UCLASS()
class UVanguardMantleVerification final : public UActorComponent
{
    GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
public:
    static UVanguardMantleVerification* Attach(AActor& Owner, const FSceneBlock& Ledge, bool bCapture);
    // The route calls this after flashlight input and before forward movement. False aborts the route.
    bool CheckStep(AVanguardCharacter& Player, float DeltaSeconds);
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase
    {
        Approach, ReleaseTap, WaitForLanding, DuckRunUp, DuckJump, DuckLanding, InspectCrouch,
        PlaceBlocker, BlockedHold, Hold, PullUp, WaitForTop, Done
    };
    void ReleaseTestInputsAndBlocker();

    EPhase Phase = EPhase::Approach;
    float Elapsed = 0.0f;
    double Front = 0.0;
    double MaxContactError = 0.0;
    double StandingLegHeight = 0.0;
    double Top = 0.0;
    bool bCapture = false;
    bool bTapLeftGround = false;
    bool bDuckedInAir = false;
    bool bCapturedApproach = false;
    bool bSawHandhold = false;
    bool bVerifiedHandhold = false;
    FVerificationKeyPress JumpKey;
    FVerificationKeyPress CrouchKey;
    TWeakObjectPtr<AActor> TestBlocker;
#endif
};
