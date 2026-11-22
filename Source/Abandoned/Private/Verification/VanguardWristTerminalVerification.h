#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "VanguardWristTerminalVerification.generated.h"

class AVanguardCharacter;
class AVanguardSegment;
class UWidgetComponent;

UCLASS()
class UVanguardWristTerminalVerification final : public UActorComponent
{
    GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
public:
    static UVanguardWristTerminalVerification* Attach(AActor& Owner, double SpawnHeight);
    // Let the radiation scenario move the player once the terminal is ready for this segment.
    bool CheckStep(const AVanguardSegment& Route, AVanguardCharacter& Player, float DeltaSeconds);
    bool CheckExposure(const AVanguardCharacter& Player, double DosePercent);
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase
    {
        Open, LookAtFeet, LookAtKnees, Stand, Status, TurnWithWrist, MeasureView, RestoreView, Signal, Archive, ReadArchive, Resupply, CheckResupply, ReadResupply,
        PreviousRecord, Close, Failed
    };
    bool CheckGlass(const APlayerController& PC, const UWidgetComponent& Display);
    bool Fail(const TCHAR* Reason);

    EPhase Phase = EPhase::Open;
    FVerificationKeyPress Key;
    FVerificationKeyPress CrouchKey;
    FRotator StandingView = FRotator::ZeroRotator;
    int32 LookStep = 0;
    double RestTerminalHeight = 0.0;
    double SpawnZ = 0.0;
    float Elapsed = 0.0f;
    bool bCapture = false;
#endif
};
