#pragma once

#include "Components/ActorComponent.h"
#include "VanguardFallVerification.generated.h"

class AVanguardCharacter;
class AVanguardSegment;
class UAudioComponent;
class USoundWave;

UCLASS()
class UVanguardFallVerification final : public UActorComponent
{
    GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
public:
    static UVanguardFallVerification* Attach(AVanguardSegment& Route);
    void CheckStep(AVanguardSegment& Route, AVanguardCharacter& Player, float DeltaSeconds);
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase { PlacePlatform, WalkOff, Falling, Recover, Failed };
    void FinishDrop();
    void Fail(const TCHAR* Reason);
    void ReleaseFixture();
    void ObserveBreath(const UAudioComponent*, const USoundWave*, float PlayedFraction);

    EPhase Phase = EPhase::PlacePlatform;
    int32 DropIndex = 0;
    float Elapsed = 0.0f;
    bool bVerifyAudio = false;
    bool bBreathDecoded = false;
    TWeakObjectPtr<AActor> PlatformActor;
    TWeakObjectPtr<UAudioComponent> ObservedBreath;
    FDelegateHandle PlaybackHandle;
#endif
};
