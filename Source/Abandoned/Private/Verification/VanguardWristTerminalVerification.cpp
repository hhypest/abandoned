#include "Verification/VanguardWristTerminalVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"
#include "Engine/SkeletalMesh.h"
#include "Components/PoseableMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/WidgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Widgets/Text/STextBlock.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Expedition/VanguardSession.h"
#include "Verification/RadiationVerificationSession.h"

#include "Components/BoxComponent.h"
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
    CrouchKey.Release();
    Super::EndPlay(Reason);
}

bool UVanguardWristTerminalVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogWristTerminal, Error, TEXT("Wrist terminal verification failed: %s (phase %d)."),
        Reason, static_cast<int>(Phase));
    Phase = EPhase::Failed;
    Key.Release();
    CrouchKey.Release();
    URadiationVerificationSession::Finish(*this, false);
    return false;
}

bool UVanguardWristTerminalVerification::CheckExposure(const AVanguardCharacter& Player, double DosePercent)
{
    if (!Player.IsWristTerminalOpen() || !Player.WristDisplay->IsVisible() || !Player.DisplayBody.IsValid() ||
        !Player.DisplayHeading->GetText().EqualTo(FText::FromString(TEXT("FLARE / EXPOSED"))) ||
        !Player.DisplayPrimary->GetText().ToString().StartsWith(TEXT("ENDS IN")) ||
        !Player.DisplayBody->GetText().ToString().Contains(FString::Printf(TEXT("DOSE    %.1f%%"), DosePercent)))
    {
        return Fail(TEXT("the raised wrist should display the accumulating exposure dose"));
    }
    if (bCapture)
    {
        FScreenshotRequest::RequestScreenshot(TEXT("Wrist-Terminal-Flare"), false, false);
    }
    UE_LOG(LogWristTerminal, Display, TEXT("Wrist status matched the live exposure dose."));
    return true;
}

bool UVanguardWristTerminalVerification::CheckGlass(const APlayerController& PC, const UWidgetComponent& Display)
{
    int32 Width = 0, Height = 0;
    PC.GetViewportSize(Width, Height);
    const FMinimalViewInfo& View = PC.PlayerCameraManager->GetCameraCacheView();
    const FTransform Glass = Display.GetComponentTransform();
    const FVector2D Size = Display.GetDrawSize();
    FBox2D Rect(ForceInit);
    double Nearest = TNumericLimits<double>::Max();
    for (const double Y : {-0.5, 0.5})
    {
        for (const double Z : {-0.5, 0.5})
        {
            const FVector Corner = Glass.TransformPosition(FVector(0, Y * Size.X, Z * Size.Y));
            const FVector Rendered = View.TransformWorldToFirstPerson(Corner, false);
            Nearest = FMath::Min(Nearest, FVector::DotProduct(Rendered - View.Location, View.Rotation.Vector()));
            FVector2D Pixel;
            if (!PC.ProjectWorldLocationToScreen(Rendered, Pixel))
            {
                return Fail(TEXT("raised glass corner projected behind the camera"));
            }
            Rect += Pixel;
        }
    }
    UE_LOG(LogWristTerminal, Display,
        TEXT("EQP-01 glass at pitch %.1f: %.0f x %.0f px in %dx%d, rectangle %s .. %s, nearest corner %.2f cm."),
        View.Rotation.Pitch, Rect.GetSize().X, Rect.GetSize().Y, Width, Height,
        *Rect.Min.ToString(), *Rect.Max.ToString(), Nearest);
    if (Nearest <= View.GetFinalPerspectiveNearClipPlane() || Rect.Min.X < 0 || Rect.Min.Y < 0 ||
        Rect.Max.X >= Width || Rect.Max.Y >= Height || Rect.GetSize().Y < Height * 0.075 ||
        FVector::DotProduct(Glass.GetUnitAxis(EAxis::X), (View.Location - Glass.GetLocation()).GetSafeNormal()) < 0.7)
    {
        return Fail(TEXT("all four EQP-01 glass corners must clear the near plane and viewport and face the eye"));
    }
    return true;
}

bool UVanguardWristTerminalVerification::CheckStep(const AVanguardSegment& Route, AVanguardCharacter& Player, float DeltaSeconds)
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
    const auto* Run = URadiationVerificationSession::Find(Route);
    if (!Session || !Run || !PC)
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
        return Player.WristTerminal.Page() == Page && Player.WristDisplay->IsVisible() && Player.DisplayBody.IsValid() && Player.DisplayBody->GetText().ToString().Contains(Text);
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
        if (Player.IsWristTerminalOpen() || Player.WristDisplay->IsVisible())
        {
            return Fail(TEXT("the wrist should start lowered"));
        }
        {
            const auto* Body = Player.GetSuitBody();
            const auto* Casing = Player.GetWristCasing();
            const auto& Visuals = *GetDefault<UVanguardMantleVisuals>();
            if (!Body || !Body->IsVisible() || !Body->GetSkinnedAsset() || !Body->GetMaterial(0) ||
                Body->GetSkinnedAsset()->GetName() != TEXT("SK_EQP00") ||
                Body->GetParentBone(TEXT("terminal_l")) != FName(TEXT("lowerarm_l")) ||
                !Player.DisplayPrimary.IsValid() || !Player.WristDisplay->GetSlateWidget().IsValid() ||
                Player.WristDisplay->GetWidgetSpace() != EWidgetSpace::World ||
                !Casing || !Casing->GetStaticMesh() || Casing->GetStaticMesh()->GetName() != TEXT("EQP-01") ||
                Casing->GetAttachParent() != Body || Casing->GetAttachSocketName() != FName(TEXT("terminal_l")) ||
                Player.WristDisplay->GetAttachParent() != Casing ||
                !Visuals.WristSurface.IsValid() || Player.WristDisplay->GetMaterial(0) != Visuals.WristSurface.Get())
            {
                return Fail(TEXT("EQP-00, its left forearm-mounted display and display material are required"));
            }
            RestTerminalHeight = Body->GetSocketLocation(TEXT("terminal_l")).Z - Player.Camera->GetComponentLocation().Z;
        }
        if (bCapture && !Route.GetRouteState().AtStation() && !Run->bObservedLethalDose)
        {
            StandingView = PC->GetControlRotation();
            PC->SetControlRotation(FRotator(-82.0, StandingView.Yaw, 0.0));
            CrouchKey.Press(*PC, EKeys::LeftControl);
            Phase = EPhase::LookAtKnees;
            break;
        }
        Key.Press(*PC, EKeys::T);
        Phase = EPhase::Status;
        break;
    case EPhase::LookAtKnees:
        if (Elapsed < 0.35f) { return false; }
        if (!Player.bIsCrouched) { return Fail(TEXT("the suit must follow the player's crouch")); }
        Capture(TEXT("EQP00-Crouch"));
        Phase = EPhase::Stand;
        break;
    case EPhase::Stand:
        CrouchKey.Release();
        PC->SetControlRotation(StandingView);
        if (Player.bIsCrouched) { return false; }
        Key.Press(*PC, EKeys::T);
        Phase = EPhase::Status;
        break;
    case EPhase::Status:
        // Allow the normal raise animation to settle independently of screenshot capture.
        if (Elapsed < 0.35f) { return false; }
        {
            const auto* Body = Player.GetSuitBody();
            const FVector Eye = Player.Camera->GetComponentLocation();
            const FVector Terminal = Body->GetSocketLocation(TEXT("terminal_l"));
            const double Rise = Terminal.Z - Eye.Z - RestTerminalHeight;
            if (Rise < 15.0 || FVector::DotProduct(Terminal - Eye, Player.Camera->GetForwardVector()) <= 0.0)
            {
                return Fail(TEXT("the terminal bone must rise at least 15 cm into view"));
            }
            const FTransform Glass = Player.WristDisplay->GetComponentTransform();
            const FVector Center = Glass.GetLocation();
            UE_LOG(LogWristTerminal, Display, TEXT("EQP-00 wrist raised %.1f cm; glass in view space %s."),
                Rise, *Player.Camera->GetComponentTransform().InverseTransformPosition(Center).ToCompactString());
            if (!CheckGlass(*PC, *Player.WristDisplay)) { return false; }
        }
        if (!Player.DisplayPrimary->GetText().ToString().Contains(FString::Printf(TEXT("OXYGEN  %.1f%%"), Route.GetRouteState().GetOxygenPercent())) ||
            (Route.GetRouteState().AtStation() ? !Shows(SuitPage::Status, TEXT("DOSE    0.0%")) :
            !Shows(SuitPage::Status, TEXT("NEXT FLARE IN")) || Route.GetRouteState().GetOxygenPercent() >= 100.0f))
        {
            return Fail(TEXT("live status at the safe approach or decontaminated station"));
        }
        if (!Route.GetRouteState().AtStation() && !Run->bObservedLethalDose)
        {
            Capture(TEXT("Wrist-Terminal-Status"));
            StandingView = PC->GetControlRotation();
            Phase = EPhase::TurnWithWrist;
            break;
        }
        Key.Press(*PC, EKeys::Tab);
        Phase = EPhase::Signal;
        break;
    case EPhase::TurnWithWrist:
    {
        // Reproduce the review case and both steep limits using the actual controller.
        constexpr double Pitches[] = {45.0, 85.0, -85.0, 45.0};
        if (LookStep == UE_ARRAY_COUNT(Pitches))
        {
            CrouchKey.Release();
            PC->SetControlRotation(StandingView);
            Phase = EPhase::RestoreView;
            break;
        }
        if (LookStep == 3) { CrouchKey.Press(*PC, EKeys::LeftControl); }
        PC->SetControlRotation(FRotator(Pitches[LookStep], StandingView.Yaw + 25.0, 0.0));
        Phase = EPhase::MeasureView;
        break;
    }
    case EPhase::MeasureView:
        if (Elapsed < 0.20f) { return false; }
        if (!Player.WristDisplay->IsVisible() || Player.bIsCrouched != (LookStep == 3) ||
            !PC->PlayerCameraManager->GetCameraRotation().Equals(PC->GetControlRotation(), 0.1))
        {
            return Fail(TEXT("player view and stance must reach the requested reading pose"));
        }
        if (Player.bIsCrouched)
        {
            const double Floor = Player.GetActorLocation().Z - Player.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            for (const FName Foot : {FName(TEXT("foot_l")), FName(TEXT("foot_r"))})
            {
                const double Ankle = Player.GetSuitBody()->GetSocketLocation(Foot).Z - Floor;
                if (Ankle < 0.0 || Ankle > 20.0)
                {
                    UE_LOG(LogWristTerminal, Error, TEXT("Crouched reading ankle %s is %.2f cm above the floor."), *Foot.ToString(), Ankle);
                    return Fail(TEXT("reading in a grounded crouch must keep both boots on the floor"));
                }
            }
        }
        if (!CheckGlass(*PC, *Player.WristDisplay)) { return false; }
        if (LookStep == 0) { Capture(TEXT("Wrist-Terminal-Pitch45")); }
        ++LookStep;
        Phase = EPhase::TurnWithWrist;
        break;
    case EPhase::RestoreView:
        if (Elapsed < 0.20f) { return false; }
        if (Player.bIsCrouched) { return Fail(TEXT("player should stand and keep the wrist raised")); }
        if (!CheckGlass(*PC, *Player.WristDisplay)) { return false; }
        Key.Press(*PC, EKeys::Tab);
        Phase = EPhase::Signal;
        break;
    case EPhase::Signal:
        if (!Shows(SuitPage::Signal, Route.GetRouteState().AtStation() ? TEXT("Exterior forecast offline") : TEXT("Forecast receiving")))
        {
            return Fail(TEXT("signal should reflect the currently available link"));
        }
        Key.Press(*PC, EKeys::Tab);
        Phase = EPhase::Archive;
        break;
    case EPhase::Archive:
        if (!Shows(SuitPage::Archive, TEXT("Flare forecast acquired.")) ||
            Session->Suit.Records().Size() != (Route.GetRouteState().AtStation() ? 2u : 1u))
        {
            return Fail(TEXT("acquired record should survive travel and occur once"));
        }
        if (Run->bObservedLethalDose && !Route.GetRouteState().AtStation())
        {
            UE_LOG(LogWristTerminal, Display, TEXT("Wrist archive after checkpoint recovery: one forecast record."));
        }
        if (Route.GetRouteState().AtStation())
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
        if (!Route.GetRouteState().AtStation())
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
        if (!Route.GetRouteState().HasResupplied() || Route.GetRouteState().GetOxygenPercent() != 100.0f || Session->Suit.Records().Size() != 3u ||
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
