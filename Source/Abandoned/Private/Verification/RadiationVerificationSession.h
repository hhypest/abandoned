#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "RadiationVerificationSession.generated.h"

UCLASS()
class URadiationVerificationSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    bool ShouldCreateSubsystem(UObject* Outer) const override;
#if WITH_DEV_AUTOMATION_TESTS
    static URadiationVerificationSession* Find(const UObject& Context);
    static void Finish(const UObject& Context, bool bPassed);
    bool bObservedLethalDose = false;
    bool bRunInEditor = false;
    TOptional<bool> Result;
#endif
};
