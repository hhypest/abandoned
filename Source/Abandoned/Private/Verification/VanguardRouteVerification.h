#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "VanguardRouteVerification.generated.h"

class AVanguardCharacter;
class AVanguardRouteGameMode;
class UBoxComponent;
class UVanguardMantleVerification;
class UVanguardRouteLayout;

UCLASS()
class UVanguardRouteVerification final : public UActorComponent
{
    GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
public:
    static UVanguardRouteVerification* AttachIfRequested(AVanguardRouteGameMode& Route, const UVanguardRouteLayout& Layout, UBoxComponent* Door);
    static bool AllowsResupplySave();
    void CheckStep(const AVanguardRouteGameMode& Route, AVanguardCharacter& Player, float DeltaSeconds);
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void CheckPassage(AVanguardCharacter& Player, float DeltaSeconds);
    void CheckStation(const AVanguardRouteGameMode& Route, AVanguardCharacter& Player);
    void Finish(bool bPassed);

    FVerificationKeyPress FlashlightKey;
    TWeakObjectPtr<UVanguardMantleVerification> MantleCheck;
    TWeakObjectPtr<UBoxComponent> Airlock;
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
