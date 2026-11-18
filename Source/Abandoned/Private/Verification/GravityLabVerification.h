#pragma once

#include "Components/ActorComponent.h"
#include "GravityLabVerification.generated.h"

class AGravityLabCharacter;
class AGravityLabGameMode;

UCLASS()
class UGravityLabVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UGravityLabVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static void AttachIfRequested(AGravityLabGameMode& Lab, AGravityLabCharacter& Player);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
    enum class EPhase
    {
        Stack, ReleaseStack, MeasureMass, LandOnWall, WalkOnWall, Reset, LandOnCeiling, WalkOnCeiling,
        LandOnRightWall, LeaveField
    };

    void Check(const AGravityLabGameMode& Lab, AGravityLabCharacter& Player);
    void Next(EPhase NextPhase, const TCHAR* Message);
    void Fail(const TCHAR* Reason);

    EPhase Phase = EPhase::Stack;
    float Elapsed = 0.0f;
    double CeilingStartX = 0.0;
    bool bCapture = false;
#endif
};
