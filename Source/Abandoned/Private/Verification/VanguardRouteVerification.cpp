#include "Verification/VanguardRouteVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/GameViewportClient.h"
#include "Expedition/VanguardRoute.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Expedition/VanguardRouteLayout.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "Verification/VanguardMantleVerification.h"

DEFINE_LOG_CATEGORY_STATIC(LogVanguardRouteTest, Log, All);

namespace
{
bool WantsRouteVerification()
{
    return FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCourse")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCheckpoint")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguard"));
}
}

UVanguardRouteVerification* UVanguardRouteVerification::AttachIfRequested(
    AVanguardRouteGameMode& Route, const UVanguardRouteLayout& Layout, UBoxComponent* Door)
{
    if (!WantsRouteVerification())
    {
        return nullptr;
    }

    auto* Check = NewObject<UVanguardRouteVerification>(&Route);
    Check->bCapture = FParse::Param(FCommandLine::Get(), TEXT("CaptureVanguard"));
    Check->SpawnZ = Layout.Spawn.Z;
    if (const auto* Passage = Cast<UVanguardPassageLayout>(&Layout))
    {
        Check->Airlock = Door;
        Check->ClosedDoor = Passage->Door.Center;
        if (Passage->bEnableMantle)
        {
            Check->MantleCheck = UVanguardMantleVerification::Attach(Route, Passage->MantleLedges[0], Check->bCapture);
        }
    }
    Route.AddInstanceComponent(Check);
    Check->RegisterComponent();
    return Check;
}

bool UVanguardRouteVerification::AllowsResupplySave()
{
    return !WantsRouteVerification() || FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCheckpoint"));
}

void UVanguardRouteVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    FlashlightKey.Release();
    Super::EndPlay(Reason);
}

void UVanguardRouteVerification::Finish(bool bPassed)
{
    bFinished = true;
    FlashlightKey.Release();
    FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}

void UVanguardRouteVerification::CheckStep(const AVanguardRouteGameMode& Route, AVanguardCharacter& Player, float DeltaSeconds)
{
    if (bFinished)
    {
        return;
    }
    Elapsed += DeltaSeconds;
    if (Elapsed > 35.0f)
    {
        UE_LOG(LogVanguardRouteTest, Error, TEXT("Vanguard verification timed out at %s."), *Player.GetActorLocation().ToCompactString());
        Finish(false);
        return;
    }
    if (bCapture && !bCapturedArrival && Elapsed >= 0.55f)
    {
        FScreenshotRequest::RequestScreenshot(Route.GetRouteState().AtStation() ? TEXT("Vanguard-Station") : TEXT("Vanguard-Airlock"), false, false);
        bCapturedArrival = true;
    }
    if (Route.GetRouteState().AtStation())
    {
        if (Elapsed >= 0.95f)
        {
            CheckStation(Route, Player);
        }
    }
    else if (Elapsed >= 0.9f)
    {
        CheckPassage(Player, DeltaSeconds);
    }
}

void UVanguardRouteVerification::CheckPassage(AVanguardCharacter& Player, float DeltaSeconds)
{
    constexpr bool ExpectedOn[] = {false, true, true, false, false, true, true};
    if (FlashlightStep < UE_ARRAY_COUNT(ExpectedOn))
    {
        auto* PC = Cast<APlayerController>(Player.GetController());
        if (!PC || Player.IsFlashlightOn() != ExpectedOn[FlashlightStep])
        {
            UE_LOG(LogVanguardRouteTest, Error, TEXT("Vanguard shoulder flashlight failed to toggle."));
            Finish(false);
            return;
        }
        if (FlashlightStep == UE_ARRAY_COUNT(ExpectedOn) - 1)
        {
            UE_LOG(LogVanguardRouteTest, Display, TEXT("Vanguard shoulder flashlight switched on, off and on with F."));
        }
        else if (FlashlightStep % 2 == 0)
        {
            FlashlightKey.Press(*PC, EKeys::F);
        }
        else
        {
            FlashlightKey.Release();
        }
        ++FlashlightStep;
    }
    if (auto* Mantle = MantleCheck.Get(); Mantle && !Mantle->CheckStep(Player, DeltaSeconds))
    {
        // The mantle check has already reported the failure and requested exit.
        bFinished = true;
        FlashlightKey.Release();
        return;
    }

    Player.AddMovementInput(FVector::ForwardVector);
    const UBoxComponent* Door = Airlock.Get();
    if (!Door)
    {
        UE_LOG(LogVanguardRouteTest, Error, TEXT("Vanguard passage airlock is unavailable."));
        Finish(false);
        return;
    }
    const double X = Player.GetActorLocation().X;
    if (!bDoorOpened && X < ClosedDoor.X - 70.0 && Door->GetComponentLocation().Z > ClosedDoor.Z + 185.0)
    {
        bDoorOpened = true;
        UE_LOG(LogVanguardRouteTest, Display, TEXT("Verification observed the raised airlock before crossing."));
        if (bCapture)
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Vanguard-Airlock-Open"), false, false);
        }
    }
    if (!bDoorPassed && X > ClosedDoor.X + 70.0)
    {
        if (!bDoorOpened)
        {
            UE_LOG(LogVanguardRouteTest, Error, TEXT("Vanguard crossed the passage airlock without observing it open."));
            Finish(false);
            return;
        }
        bDoorPassed = true;
        UE_LOG(LogVanguardRouteTest, Display, TEXT("Verification crossed the moving passage airlock."));
    }
}

void UVanguardRouteVerification::CheckStation(const AVanguardRouteGameMode& Route, AVanguardCharacter& Player)
{
    const UBoxComponent* Supply = Route.GetSupplyCabinet();
    if (!IsValid(Supply))
    {
        UE_LOG(LogVanguardRouteTest, Error, TEXT("Vanguard station supply cabinet is unavailable."));
        Finish(false);
        return;
    }
    const FVector Cabinet = Supply->GetComponentLocation();
    const FVector From(Cabinet.X - 170.0, Cabinet.Y, SpawnZ);
    const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
    Player.PlaceAt(FTransform((Cabinet - Eye).Rotation(), From));
    const float Prior = Route.GetRouteState().GetOxygenPercent();
    Player.Interact();
    const float Current = Route.GetRouteState().GetOxygenPercent();
    const bool bPassed = Prior < 100.0f && Route.GetRouteState().HasResupplied() && Current == 100.0f && Player.IsFlashlightOn();
    if (bPassed)
    {
        UE_LOG(LogVanguardRouteTest, Display, TEXT("Vanguard shoulder flashlight stayed on across world travel."));
        UE_LOG(LogVanguardRouteTest, Display, TEXT("Vanguard verification passed: passage transit and station resupply. Oxygen %.2f -> %.2f."), Prior, Current);
    }
    else
    {
        UE_LOG(LogVanguardRouteTest, Error, TEXT("Vanguard verification failed: prior oxygen %.2f, current %.2f."), Prior, Current);
    }
    Finish(bPassed);
}
#endif
