#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "VanguardRouteVerification.generated.h"

class AVanguardCharacter;
class AVanguardSegment;
class UBoxComponent;
class UVanguardMantleVerification;
class UVanguardRouteLayout;

UCLASS()
class UVanguardRouteVerification final : public UActorComponent
{
    GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
public:
    static UVanguardRouteVerification* AttachIfRequested(AVanguardSegment& Route, const UVanguardRouteLayout& Layout, UBoxComponent* Door);
    static bool AllowsResupplySave();
    void CheckStep(AVanguardSegment& Route, AVanguardCharacter& Player, float DeltaSeconds);
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void CheckPassage(AVanguardCharacter& Player, float DeltaSeconds);
    void CheckStation(const AVanguardSegment& Route, AVanguardCharacter& Player);
    void Finish(bool bPassed);

    FVerificationKeyPress FlashlightKey;
    TWeakObjectPtr<UVanguardMantleVerification> MantleCheck;
    TWeakObjectPtr<UBoxComponent> Airlock;
    FVector ClosedStationGate = FVector::ZeroVector;
    float WaitingAtStation = 0;
    bool bRevealedStation = false;
    FVector ClosedDoor = FVector::ZeroVector;
    double SpawnZ = 0.0;
    float Elapsed = 0.0f;
    int32 FlashlightStep = 0;
    bool bCapture = false;
    bool bCapturedArrival = false;
    bool bDoorOpened = false;
    bool bDoorPassed = false;
    bool bFinished = false;
#endif
};
