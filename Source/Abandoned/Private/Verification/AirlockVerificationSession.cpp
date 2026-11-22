#include "Verification/AirlockVerificationSession.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

bool UAirlockVerificationSession::ShouldCreateSubsystem(UObject* Outer) const
{
#if WITH_DEV_AUTOMATION_TESTS
    const TCHAR* Args = FCommandLine::Get();
    return GIsAutomationTesting || FParse::Param(Args, TEXT("VerifyButtonAirlock")) ||
        FParse::Param(Args, TEXT("VerifyWeightAirlock")) || FParse::Param(Args, TEXT("VerifyBalanceAirlock")) ||
        FParse::Param(Args, TEXT("VerifyPassageAirlock")) || FParse::Param(Args, TEXT("VerifyStationAirlock"));
#else
    return false;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
UAirlockVerificationSession* UAirlockVerificationSession::Find(const AActor& Context)
{
    return UGameInstance::GetSubsystem<UAirlockVerificationSession>(Context.GetGameInstance());
}
#endif
