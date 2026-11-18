#include "Verification/VanguardWristTerminalVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRoute.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogWristTerminal, Log, All);

UVanguardWristTerminalVerification* UVanguardWristTerminalVerification::Attach(AActor& Owner, double SpawnHeight)
{
    auto* Check = NewObject<UVanguardWristTerminalVerification>(&Owner);
    Check->SpawnZ = SpawnHeight;
    Check->bCapture = FParse::Param(FCommandLine::Get(), TEXT("CaptureWristTerminal"));
    Owner.AddInstanceComponent(Check);
    Check->RegisterComponent();
    return Check;
}

void UVanguardWristTerminalVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    Key.Release();
    Super::EndPlay(Reason);
}

bool UVanguardWristTerminalVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogWristTerminal, Error, TEXT("Wrist terminal verification failed: %s (phase %d)."),
        Reason, static_cast<int>(Phase));
    Phase = EPhase::Failed;
    Key.Release();
    FPlatformMisc::RequestExitWithStatus(false, 1);
    return false;
}

bool UVanguardWristTerminalVerification::CheckExposure(const AVanguardCharacter& Player, double DosePercent)
{
    if (!Player.IsWristTerminalOpen() || !Player.WristDisplay->IsVisible() ||
        !Player.WristReadout.Contains(FString::Printf(TEXT("DOSE    %.1f%%"), DosePercent)))
    {
        return Fail(TEXT("the raised wrist should display the accumulating exposure dose"));
    }
    UE_LOG(LogWristTerminal, Display, TEXT("Wrist status matched the live exposure dose."));
    return true;
}

bool UVanguardWristTerminalVerification::CheckStep(const AVanguardRouteGameMode& Route, AVanguardCharacter& Player, float DeltaSeconds)
{
    using Abandoned::Application::Expedition::SuitPage;
    using Abandoned::Domain::Expedition::SuitRecord;
    if (Phase == EPhase::Failed)
    {
        return false;
    }
    if (Key.Release())
    {
        Elapsed = 0.0f;
        return false;
    }
    const auto* Session = UGameInstance::GetSubsystem<UVanguardSession>(Route.GetGameInstance());
    APlayerController* PC = Cast<APlayerController>(Player.GetController());
    if (!Session || !PC)
    {
        return Fail(TEXT("session and player controller required"));
    }
    Elapsed += DeltaSeconds;
    const bool bCaptureStep = bCapture && (Phase == EPhase::Status || Phase == EPhase::Archive);
    if ((Phase == EPhase::Open && Elapsed < 0.55f) || (bCaptureStep && Elapsed < 0.22f))
    {
        return false;
    }

    const auto Shows = [&](SuitPage Page, const TCHAR* Text)
    {
        return Player.WristTerminal.Page() == Page && Player.WristDisplay->IsVisible() && Player.WristReadout.Contains(Text);
    };
    const auto Capture = [&](const TCHAR* Name)
    {
        if (bCapture)
        {
            FScreenshotRequest::RequestScreenshot(Name, false, false);
        }
    };

    switch (Phase)
    {
    case EPhase::Open:
        if (Player.IsWristTerminalOpen() || Player.WristCasing->IsVisible())
        {
            return Fail(TEXT("the wrist should start lowered"));
        }
        if (!Player.WristCasing->GetStaticMesh())
        {
            return Fail(TEXT("the wrist casing mesh should be available"));
        }
        Key.Press(*PC, EKeys::T);
        Phase = EPhase::Status;
        break;
    case EPhase::Status:
        if (!Player.WristReadout.Contains(FString::Printf(TEXT("OXYGEN  %.1f%%"), Route.GetOxygenPercent())) ||
            (Route.AtStation() ? !Shows(SuitPage::Status, TEXT("DOSE    0.0%")) :
            !Shows(SuitPage::Status, TEXT("NEXT FLARE IN")) || Route.GetOxygenPercent() >= 100.0f))
        {
            return Fail(TEXT("live status at the safe approach or decontaminated station"));
        }
        if (!Route.AtStation() && !Session->bRecoveredFromRadiation)
        {
            Capture(TEXT("Wrist-Terminal-Status"));
        }
        Key.Press(*PC, EKeys::Tab);
        Phase = EPhase::Signal;
        break;
    case EPhase::Signal:
        if (!Shows(SuitPage::Signal, Route.AtStation() ? TEXT("Exterior forecast offline") : TEXT("Forecast receiving")))
        {
            return Fail(TEXT("signal should reflect the currently available link"));
        }
        Key.Press(*PC, EKeys::Tab);
        Phase = EPhase::Archive;
        break;
    case EPhase::Archive:
        if (!Shows(SuitPage::Archive, TEXT("Flare forecast acquired.")) ||
            Session->Suit.Records().Size() != (Route.AtStation() ? 2u : 1u))
        {
            return Fail(TEXT("acquired record should survive travel and occur once"));
        }
        if (Session->bRecoveredFromRadiation && !Route.AtStation())
        {
            UE_LOG(LogWristTerminal, Display, TEXT("Wrist archive after checkpoint recovery: one forecast record."));
        }
        if (Route.AtStation())
        {
            Capture(TEXT("Wrist-Terminal-Archive"));
            Key.Press(*PC, EKeys::PageDown);
        }
        else
        {
            Key.Press(*PC, EKeys::Tab);
        }
        Phase = EPhase::ReadArchive;
        break;
    case EPhase::ReadArchive:
        if (!Route.AtStation())
        {
            if (Player.WristTerminal.Page() != SuitPage::Status)
            {
                return Fail(TEXT("status page should remain raised while crossing the gallery"));
            }
            return true;
        }
        if (!Shows(SuitPage::Archive, TEXT("Decontamination complete.")) || !IsValid(Route.GetSupplyCabinet()))
        {
            return Fail(TEXT("decontamination record should follow the forecast record"));
        }
        {
            const FVector Cabinet = Route.GetSupplyCabinet()->GetComponentLocation();
            const FVector From(Cabinet.X - 170.0, Cabinet.Y, SpawnZ);
            const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
            Player.PlaceAt(FTransform((Cabinet - Eye).Rotation(), From));
        }
        Phase = EPhase::Resupply;
        break;
    case EPhase::Resupply:
        Key.Press(*PC, EKeys::E);
        Phase = EPhase::CheckResupply;
        break;
    case EPhase::CheckResupply:
        if (!Route.HasResupplied() || Route.GetOxygenPercent() != 100.0f || Session->Suit.Records().Size() != 3u ||
            Session->Suit.Records().At(2) != SuitRecord::OxygenRestored)
        {
            return Fail(TEXT("resupply should restore oxygen and append an entry with the wrist raised"));
        }
        Key.Press(*PC, EKeys::PageDown);
        Phase = EPhase::ReadResupply;
        break;
    case EPhase::ReadResupply:
        if (!Shows(SuitPage::Archive, TEXT("Suit reservoir refilled.")))
        {
            return Fail(TEXT("the resupply record should be readable"));
        }
        Key.Press(*PC, EKeys::PageUp);
        Phase = EPhase::PreviousRecord;
        break;
    case EPhase::PreviousRecord:
        if (!Shows(SuitPage::Archive, TEXT("Decontamination complete.")))
        {
            return Fail(TEXT("page up should return to the previous record"));
        }
        Key.Press(*PC, EKeys::T);
        Phase = EPhase::Close;
        break;
    case EPhase::Close:
        if (Player.IsWristTerminalOpen() || Player.WristDisplay->IsVisible())
        {
            return Fail(TEXT("toggle should lower the wrist and hide its screen"));
        }
        UE_LOG(LogWristTerminal, Display,
            TEXT("Wrist terminal verification passed: live forecast and exposure, link handoff, archive retained across reset and travel, station resupply and controls."));
        return true;
    case EPhase::Failed: return false;
    }
    Elapsed = 0.0f;
    return false;
}
#endif
