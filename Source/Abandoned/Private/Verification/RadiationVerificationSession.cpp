#include "Verification/RadiationVerificationSession.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#endif

bool URadiationVerificationSession::ShouldCreateSubsystem(UObject* Outer) const
{
#if WITH_DEV_AUTOMATION_TESTS
    return GIsAutomationTesting || FParse::Param(FCommandLine::Get(), TEXT("VerifyRadiation")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyWristTerminal"));
#else
    return false;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
URadiationVerificationSession* URadiationVerificationSession::Find(const UObject& Context)
{
    const UWorld* World = Context.GetWorld();
    return World ? UGameInstance::GetSubsystem<URadiationVerificationSession>(World->GetGameInstance()) : nullptr;
}

void URadiationVerificationSession::Finish(const UObject& Context, bool bPassed)
{
    if (auto* Run = Find(Context); Run && Run->bRunInEditor)
    {
        if (!Run->Result.IsSet()) { Run->Result = bPassed; }
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}
#endif
