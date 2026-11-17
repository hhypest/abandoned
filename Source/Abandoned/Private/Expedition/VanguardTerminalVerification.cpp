#include "Expedition/VanguardRoute.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRouteLayout.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputKeyEventArgs.h"
#include "Player/MovementProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogWristTerminal, Log, All);

bool AVanguardRouteGameMode::VerifyWristTerminal(AVanguardCharacter& Player, float DeltaSeconds)
{
    using Abandoned::Application::Expedition::SuitPage;
    using Abandoned::Domain::Expedition::SuitRecord;
    const UVanguardSession* Session = GetSession();
    APlayerController* PC = Cast<APlayerController>(Player.GetController());
    TerminalVerificationSeconds += DeltaSeconds;
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogWristTerminal, Error, TEXT("Wrist terminal verification failed: %s (input step %d)."),
            Reason, TerminalInputStep);
        bVerifyRadiation = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return false;
    };
    if (!Session || !PC)
    {
        return Fail(TEXT("session and player controller required"));
    }
    const bool bCaptureStep = bCaptureWristTerminal && (TerminalInputStep == 2 || TerminalInputStep == 6);
    if ((TerminalInputStep == 0 && TerminalVerificationSeconds < 0.55f) ||
        (bCaptureStep && TerminalVerificationSeconds < 0.22f))
    {
        return false;
    }

    const auto Send = [&](const FKey& Key, EInputEvent Event = IE_Pressed)
    {
        PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, Event, FPlatformTime::Cycles64()));
    };
    const auto Shows = [&](SuitPage Page, const TCHAR* Text)
    {
        return Player.WristTerminal.Page() == Page && Player.WristDisplay->IsVisible() && Player.WristReadout.Contains(Text);
    };
    const auto Capture = [&](const TCHAR* Name)
    {
        if (bCaptureWristTerminal)
        {
            FScreenshotRequest::RequestScreenshot(Name, false, false);
        }
    };

    switch (TerminalInputStep)
    {
    case 0:
        if (Player.IsWristTerminalOpen() || Player.WristCasing->IsVisible())
        {
            return Fail(TEXT("the wrist should start lowered"));
        }
        if (!Player.WristCasing->GetStaticMesh())
        {
            return Fail(TEXT("the wrist casing mesh should be available"));
        }
        Send(EKeys::T);
        break;
    case 1: Send(EKeys::T, IE_Released); break;
    case 2:
        if (AtStation() ? !Shows(SuitPage::Status, TEXT("DOSE    0.0%")) :
            !Shows(SuitPage::Status, TEXT("NEXT FLARE IN")) || GetOxygenPercent() >= 100.0f)
        {
            return Fail(TEXT("live status at the safe approach or decontaminated station"));
        }
        if (!AtStation() && !Session->bRecoveredFromRadiation)
        {
            Capture(TEXT("Wrist-Terminal-Status"));
        }
        Send(EKeys::Tab);
        break;
    case 3: Send(EKeys::Tab, IE_Released); break;
    case 4:
        if (!Shows(SuitPage::Signal, AtStation() ? TEXT("Exterior forecast offline") : TEXT("Forecast receiving")))
        {
            return Fail(TEXT("signal should reflect the currently available link"));
        }
        Send(EKeys::Tab);
        break;
    case 5: Send(EKeys::Tab, IE_Released); break;
    case 6:
        if (!Shows(SuitPage::Archive, TEXT("Flare forecast acquired.")) ||
            Session->Suit.Records().Size() != (AtStation() ? 2u : 1u))
        {
            return Fail(TEXT("acquired record should survive travel and occur once"));
        }
        if (AtStation())
        {
            Capture(TEXT("Wrist-Terminal-Archive"));
            Send(EKeys::PageDown);
        }
        else
        {
            Send(EKeys::Tab);
        }
        break;
    case 7: Send(AtStation() ? EKeys::PageDown : EKeys::Tab, IE_Released); break;
    case 8:
        if (!AtStation())
        {
            if (Player.WristTerminal.Page() != SuitPage::Status)
            {
                return Fail(TEXT("status page should remain raised while crossing the gallery"));
            }
            return true;
        }
        if (!Shows(SuitPage::Archive, TEXT("Decontamination complete.")) || !IsValid(SupplyCabinet))
        {
            return Fail(TEXT("decontamination record should follow the forecast record"));
        }
        {
            const FVector Cabinet = SupplyCabinet->GetComponentLocation();
            const FVector From(Cabinet.X - 170.0, Cabinet.Y, Layout->Spawn.Z);
            const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
            Player.PlaceAt(FTransform((Cabinet - Eye).Rotation(), From));
        }
        break;
    case 9: Send(EKeys::E); break;
    case 10: Send(EKeys::E, IE_Released); break;
    case 11:
        if (!HasResupplied() || GetOxygenPercent() != 100.0f ||
            Session->Suit.Records().At(2) != SuitRecord::OxygenRestored)
        {
            return Fail(TEXT("resupply should restore oxygen and append an entry with the wrist raised"));
        }
        Send(EKeys::PageDown);
        break;
    case 12: Send(EKeys::PageDown, IE_Released); break;
    case 13:
        if (!Shows(SuitPage::Archive, TEXT("Suit reservoir refilled.")))
        {
            return Fail(TEXT("the resupply record should be readable"));
        }
        Send(EKeys::PageUp);
        break;
    case 14: Send(EKeys::PageUp, IE_Released); break;
    case 15:
        if (!Shows(SuitPage::Archive, TEXT("Decontamination complete.")))
        {
            return Fail(TEXT("page up should return to the previous record"));
        }
        Send(EKeys::T);
        break;
    case 16: Send(EKeys::T, IE_Released); break;
    case 17:
        if (Player.IsWristTerminalOpen() || Player.WristDisplay->IsVisible())
        {
            return Fail(TEXT("toggle should lower the wrist and hide its screen"));
        }
        UE_LOG(LogWristTerminal, Display,
            TEXT("Wrist terminal verification passed: live forecast and exposure, link handoff, archive retained across reset and travel, station resupply and controls."));
        return true;
    }
    ++TerminalInputStep;
    TerminalVerificationSeconds = 0.0f;
    return false;
}
#endif
