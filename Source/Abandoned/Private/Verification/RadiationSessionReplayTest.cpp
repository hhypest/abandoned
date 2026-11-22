#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Verification/RadiationVerificationSession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadiationSessionReplayTest, "Abandoned.Verification.RadiationTerminalReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadiationSessionReplayTest::RunTest(const FString& Parameters)
{
    if (!GEditor || GEditor->PlayWorld)
    {
        AddError(TEXT("Start the radiation replay from an idle editor."));
        return false;
    }
    for (int32 Visit = 1; Visit <= 2; ++Visit)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
        ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([this, Visit]()
        {
            UWorld* World = GEditor->PlayWorld;
            if (!World || !World->GetFirstPlayerController()) { return false; }
            auto* Run = URadiationVerificationSession::Find(*World);
            if (!TestNotNull(TEXT("PIE owns a radiation verification session"), Run)) { return true; }
            if (!Run->bRunInEditor)
            {
                if (!TestFalse(TEXT("A fresh run starts before lethal exposure"), Run->bObservedLethalDose || Run->Result.IsSet())) { return true; }
                Run->bRunInEditor = true;
                using namespace Abandoned::Application::Expedition;
                Abandoned::Infrastructure::Expedition::TravelToWorld(World, VanguardDestination{VanguardStop::RadiationPassage});
            }
            if (!Run->Result.IsSet()) { return false; }
            TestTrue(TEXT("Recovery, wrist controls, station and resupply completed"), Run->Result.GetValue() && Run->bObservedLethalDose);
            AddInfo(FString::Printf(TEXT("Finished radiation and wrist route in PIE session %d."), Visit));
            return true;
        }, [this]() { AddError(TEXT("Timed out waiting for the radiation and wrist route.")); return true; }, 100.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
        ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]() { return !GEditor->PlayWorld; },
            [this]() { AddError(TEXT("PIE did not stop.")); return true; }, 30.0f));
    }
    return true;
}
#endif
