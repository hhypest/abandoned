#pragma once

#include "Components/ActorComponent.h"
#include "VanguardRadiationVerification.generated.h"

class AVanguardCharacter;
class AVanguardSegment;
class UVanguardRadiationLayout;
class UVanguardRouteLayout;
class UVanguardSession;
class UVanguardWristTerminalVerification;

UCLASS()
class UVanguardRadiationVerification final : public UActorComponent
{
    GENERATED_BODY()

#if WITH_DEV_AUTOMATION_TESTS
public:
    static UVanguardRadiationVerification* AttachIfRequested(AVanguardSegment& Route, const UVanguardRouteLayout& Layout);
    void CheckStep(const AVanguardSegment& Route, AVanguardCharacter& Player, float DeltaSeconds);
    void ObserveDoseLimit(UVanguardSession& Session);

private:
    enum class EPhase
    {
        Gate, WaitForFlare, LeaveShelter, ReturnToShelter, HoldInShelter, RecoverOutside,
        LethalExposure, RunToStation, Finished
    };
    void Fail(const TCHAR* Reason);

    TWeakObjectPtr<const UVanguardRadiationLayout> PassageLayout;
    TWeakObjectPtr<UVanguardWristTerminalVerification> WristCheck;
    EPhase Phase = EPhase::Gate;
    float Elapsed = 0.0f;
    float ShelterSeconds = 0.0f;
    double ProtectedDose = 0.0;
    bool bCapture = false;
#endif
};
