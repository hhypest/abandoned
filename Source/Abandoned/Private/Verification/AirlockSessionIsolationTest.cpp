#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Verification/AirlockVerificationSession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAirlockSessionIsolationTest, "Abandoned.Verification.AirlockSessionIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAirlockSessionIsolationTest::RunTest(const FString& Parameters)
{
    if (!GEditor || GEditor->PlayWorld)
    {
        AddError(TEXT("Start the isolation test from an idle editor."));
        return false;
    }

    for (int32 Visit = 1; Visit <= 2; ++Visit)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
        ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([this, Visit]()
        {
            UWorld* World = GEditor->PlayWorld;
            auto* Controller = World ? World->GetFirstPlayerController() : nullptr;
            if (!Controller) { return false; }
            auto* Session = UAirlockVerificationSession::Find(*Controller);
            if (TestNotNull(TEXT("PIE game instance owns the verification session"), Session))
            {
                TestTrue(TEXT("Chamber and passage visits start at zero"), Session->Chamber.Visits == 0 && Session->Passage.Visits == 0);
                TestTrue(TEXT("No previous passage observation"), Session->Passage.SourceWorld.IsExplicitlyNull());
                TestTrue(TEXT("No previous station observation"), Session->Station.Controller.IsExplicitlyNull() && Session->Station.LastOxygen == 0);
                Session->Chamber.Visits = 2;
                Session->Passage.Visits = 2;
                Session->Passage.SourceWorld = World;
                Session->Station.Controller = Controller;
                Session->Station.LastOxygen = 94.0f;
                AddInfo(FString::Printf(TEXT("Checked initial state and recorded observations in PIE session %d."), Visit));
            }
            return true;
        }, [this]() { AddError(TEXT("PIE player did not start.")); return true; }, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
        ADD_LATENT_AUTOMATION_COMMAND(FUntilCommand([]() { return !GEditor->PlayWorld; },
            [this]() { AddError(TEXT("PIE session did not end.")); return true; }, 30.0f));
    }
    return true;
}
#endif
