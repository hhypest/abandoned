#pragma once

#include "Components/ActorComponent.h"
#include "PulseDoorVerification.generated.h"

class APowerLabGameMode;
class APowerLabCharacter;

UCLASS()
class UPulseDoorVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UPulseDoorVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static void Start(APowerLabGameMode& Lab, bool bCapture);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
    enum class EPhase { Ready, RepeatPress, Expiry, CrossExit, ReturnSwitch, OpenReturnDoor, LocalRecovery, Reset };
    void Check(const APowerLabGameMode& Lab, APowerLabCharacter& Player);
    void Next(EPhase NextPhase);
    void Fail(const TCHAR* Reason);

    EPhase Phase = EPhase::Ready;
    float Elapsed = 0.0f;
    bool bCapture = false;
#endif
};
