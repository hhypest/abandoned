#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "VanguardWristTerminalVerification.generated.h"

class AVanguardCharacter;
class AVanguardSegment;

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
        Open, LookAtKnees, Stand, Status, Signal, Archive, ReadArchive, Resupply, CheckResupply, ReadResupply,
        PreviousRecord, Close, Failed
    };
    bool Fail(const TCHAR* Reason);

    EPhase Phase = EPhase::Open;
    FVerificationKeyPress Key;
    FVerificationKeyPress CrouchKey;
    FRotator StandingView = FRotator::ZeroRotator;
    double RestTerminalHeight = 0.0;
    double SpawnZ = 0.0;
    float Elapsed = 0.0f;
    bool bCapture = false;
#endif
};
