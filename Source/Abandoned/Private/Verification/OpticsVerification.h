#pragma once

#include "Components/ActorComponent.h"
#include "OpticsVerification.generated.h"

class APowerLabGameMode;
class APowerLabCharacter;

UCLASS()
class UOpticsVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UOpticsVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static void AttachIfRequested(APowerLabGameMode& Lab);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
    enum class EPhase
    {
        Unpowered, DirectSignal, SuitBlocksBeam, RestoredSignal, CubeBlocksBeam, CrossFirstDoor, PlaceMirror,
        AimMirror, ReflectedSignal, ApproachExit, CrossExit, MirrorClamped, ReleaseMirror,
        AdjustFreeMirror, LostReflection, RestoredReflection, CaptureReflection
    };
    void Check(APowerLabGameMode& Lab, APowerLabCharacter& Player, float Delta);
    void Next(EPhase NextPhase, const TCHAR* Message);
    void Fail(const TCHAR* Reason);

    EPhase Phase = EPhase::Unpowered;
    float Elapsed = 0.0f;
    bool bFreeReflector = false;
    bool bCapture = false;
#endif
};
