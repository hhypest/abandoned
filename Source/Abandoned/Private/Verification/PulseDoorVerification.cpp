#include "Verification/PulseDoorVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PowerLab.h"
#include "Puzzles/PowerLabLayout.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogPulseVerification, Log, All);
#endif

UPulseDoorVerification::UPulseDoorVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UPulseDoorVerification::Start(APowerLabGameMode& Lab, bool bCapture)
{
    auto* Test = NewObject<UPulseDoorVerification>(&Lab);
    Test->bCapture = bCapture;
    Lab.AddInstanceComponent(Test);
    Test->AddTickPrerequisiteActor(&Lab);
    Test->RegisterComponent();
    Test->SetComponentTickEnabled(true);
}

void UPulseDoorVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    const auto* Lab = Cast<APowerLabGameMode>(GetOwner());
    const auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<APowerLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!IsValid(Player) || !Lab || !IsValid(Lab->Layout) || !IsValid(Lab->PulseDoor) ||
        !IsValid(Lab->Mirror) || !IsValid(Lab->MirrorSurface))
    {
        Fail(TEXT("Lost the player or a device during the timed passage."));
        return;
    }
    Elapsed += DeltaSeconds;
    Check(*Lab, *Player);
}

void UPulseDoorVerification::Next(EPhase NextPhase)
{
    Phase = NextPhase;
    Elapsed = 0.0f;
}

void UPulseDoorVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogPulseVerification, Error, TEXT("Pulse verification failed at step %d: %s"), static_cast<int32>(Phase), Reason);
    SetComponentTickEnabled(false);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UPulseDoorVerification::Check(const APowerLabGameMode& Lab, APowerLabCharacter& Player)
{
    const UPowerLabLayout& Layout = *Lab.Layout;
    const auto LookFrom = [&Player, &Layout](FVector Point, const FVector& At)
    {
        Point.Z = Layout.Spawn.Z;
        const FVector Eye = Point + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((At - Eye).Rotation(), Point));
    };
    const auto Use = [&Player, &LookFrom](const FVector& Switch)
    {
        LookFrom(Switch + FVector(0, 135, 0), Switch);
        Player.Interact();
    };
    const auto DoorClosed = [&Lab, &Layout]
    {
        return Lab.PulseDoor->GetComponentLocation().Equals(Layout.PulseDoor.Center, 1.0);
    };
    if (Elapsed > FMath::Max(6.0f, Layout.PulseSeconds + 2.0f))
    {
        Fail(TEXT("timed door did not reach the expected physical state"));
        return;
    }
    switch (Phase)
    {
    case EPhase::Ready:
        if (!DoorClosed() || Lab.Pulse.IsComplete() || Lab.Pulse.IsPowered())
        {
            Fail(TEXT("timed bay should start closed and ready"));
            return;
        }
        Use(Layout.PulseButton);
        if (!Lab.Pulse.IsPowered() || !FMath::IsNearlyEqual(Lab.GetPulseSeconds(), Layout.PulseSeconds))
        {
            Fail(TEXT("E did not start a fresh pulse"));
            return;
        }
        Next(EPhase::RepeatPress);
        break;
    case EPhase::RepeatPress:
        if (Elapsed >= 0.65f)
        {
            const float Before = Lab.GetPulseSeconds();
            Player.Interact();
            if (Before <= 0.0f || Before >= Layout.PulseSeconds || !FMath::IsNearlyEqual(Lab.GetPulseSeconds(), Before))
            {
                Fail(TEXT("a repeat press extended the live pulse"));
                return;
            }
            if (bCapture)
            {
                LookFrom(Layout.PulseButton + FVector(-180, 145, 0), Layout.PulseDoor.Center + FVector(0, -28, 52));
                FScreenshotRequest::RequestScreenshot(TEXT("PowerLab-Pulse"), false, false);
            }
            Next(EPhase::Expiry);
        }
        break;
    case EPhase::Expiry:
        if (!Lab.Pulse.IsPowered() && DoorClosed())
        {
            Use(Layout.PulseButton);
            if (!Lab.Pulse.IsPowered() || Lab.Pulse.IsComplete())
            {
                Fail(TEXT("a timed out attempt could not be retried"));
                return;
            }
            Next(EPhase::CrossExit);
        }
        break;
    case EPhase::CrossExit:
        if (Lab.Pulse.IsComplete())
        {
            if (!Lab.Pulse.IsPowered())
            {
                Fail(TEXT("player reached the exit after the pulse expired"));
                return;
            }
            UE_LOG(LogPulseVerification, Display, TEXT("Timed door crossed with %.2f seconds remaining."), Lab.GetPulseSeconds());
            Next(EPhase::ReturnSwitch);
        }
        else
        {
            const FVector Direction = (Layout.PulseExit.Center - Player.GetActorLocation()).GetSafeNormal2D();
            Player.AddMovementInput(Direction);
        }
        break;
    case EPhase::ReturnSwitch:
        if (!Lab.Pulse.IsPowered() && DoorClosed())
        {
            Use(Layout.PulseReturn);
            if (!Lab.Pulse.IsPowered() || !Lab.Pulse.IsComplete())
            {
                Fail(TEXT("return switch did not reopen the completed passage"));
                return;
            }
            Next(EPhase::OpenReturnDoor);
        }
        break;
    case EPhase::OpenReturnDoor:
        if (Lab.PulseDoor->GetComponentLocation().Z > Layout.PulseDoor.Center.Z + 250.0)
        {
            const FVector Threshold(Layout.PulseDoor.Center.X, Layout.PulseDoor.Center.Y, Layout.Spawn.Z);
            LookFrom(Threshold, Threshold + FVector::ForwardVector * 180.0);
            Next(EPhase::LocalRecovery);
        }
        break;
    case EPhase::LocalRecovery:
        if (!Lab.Pulse.IsPowered() && DoorClosed())
        {
            if (!Player.GetActorLocation().Equals(Layout.PulseRetry, 3.0) || !Lab.Course.GetPower().ReflectionEmitter)
            {
                Fail(TEXT("the closing door did not return the player locally with optics still powered"));
                return;
            }
            Use(Layout.PulseButton);
            if (!Lab.Pulse.IsPowered())
            {
                Fail(TEXT("the pulse could not restart after the doorway recovery"));
                return;
            }
            Next(EPhase::Reset);
        }
        break;
    case EPhase::Reset:
        Player.ResetChamber();
        if (Lab.Pulse.IsPowered() || Lab.Pulse.IsComplete() || !DoorClosed() || Lab.DisplayedPulseTenths != 0 ||
            !Player.GetActorLocation().Equals(Layout.Spawn, 3.0) || Lab.bMirrorDocked || !Lab.Mirror->IsSimulatingPhysics() ||
            !Lab.Mirror->GetComponentLocation().Equals(Layout.Mirror, 1.0) ||
            !Lab.MirrorSurface->GetRelativeRotation().Equals(FRotator(0, -45, 0), 0.1))
        {
            Fail(TEXT("reset left a pulse, open door or completion"));
            return;
        }
        UE_LOG(LogPulseVerification, Display,
            TEXT("Timed passage verification passed: countdown, expiry, retry, crossing, return switch, local recovery and reset."));
        SetComponentTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}
#endif
