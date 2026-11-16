#include "Puzzles/PowerLab.h"
#include "Puzzles/PowerLabLayout.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "World/Blockout.h"

namespace
{
UTextRenderComponent* AddPulseLabel(UBoxComponent& Body, FVector Offset, FRotator Rotation, float Size, const TCHAR* Text)
{
    AActor* Owner = Body.GetOwner();
    auto* Label = NewObject<UTextRenderComponent>(Owner);
    Owner->AddInstanceComponent(Label);
    Label->SetupAttachment(&Body);
    Label->SetRelativeLocation(Offset);
    Label->SetRelativeRotation(Rotation);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(Size);
    Label->SetText(FText::FromString(Text));
    Label->RegisterComponent();
    return Label;
}
}

bool APowerLabGameMode::SpawnPulseBay(UStaticMesh& Mesh, UMaterialInterface& Steel, UMaterialInterface& Panel,
    UMaterialInterface& Screen)
{
    const auto Box = [&](FVector Center, FVector Size, bool bSolid, UMaterialInterface& Material)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(*GetWorld(), Mesh, Center, Size,
            EComponentMobility::Movable, bSolid ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
        if (Parts.Visual)
        {
            Parts.Visual->SetMaterial(0, &Material);
        }
        return Parts;
    };
    const auto BuildSwitch = [&](const FVector& Point, const TCHAR* Text) -> UBoxComponent*
    {
        const auto Parts = Box(Point, FVector(54, 22, 42), true, Panel);
        const auto Lamp = Box(Point + FVector(0, 12, 11), FVector(38, 4, 7), false, *Inactive);
        if (!Parts.Collision || !Parts.Visual || !Lamp.Visual)
        {
            return nullptr;
        }
        PulseIndicators.Add(Lamp.Visual);
        AddPulseLabel(*Parts.Collision, FVector(0, 13.5, -5), FRotator(0, 90, 0), 10.0f, Text);
        return Parts.Collision;
    };
    PulseButton = BuildSwitch(Layout->PulseButton, TEXT("PULSE"));
    PulseReturn = BuildSwitch(Layout->PulseReturn, TEXT("RETURN"));
    const FSceneBlock& Closed = Layout->PulseDoor;
    const auto Door = Box(Closed.Center, Closed.Size, true, Steel);
    PulseDoor = Door.Collision;
    const auto Line = Box(FVector(Closed.Center.X - 200.0, Closed.Center.Y, 3.0),
        FVector(400, 10, 6), false, *Inactive);
    const auto Display = Box(Closed.Center + FVector(-39, 0, 178), FVector(12, 230, 62), false, Screen);
    if (!PulseButton || !PulseReturn || !PulseDoor || !Door.Visual || !Line.Visual || !Display.Collision || !Display.Visual)
    {
        return false;
    }
    PulseClamp.AddSigns(*PulseDoor);
    PulseIndicators.Add(Line.Visual);
    PulseClock = AddPulseLabel(*Display.Collision, FVector(-7, 0, 0), FRotator(0, 180, 0), 30.0f, TEXT("READY"));
    Pulse = Abandoned::Application::Puzzles::TimedPassage(Layout->PulseSeconds);
    UpdatePulseDisplay();
    return true;
}

bool APowerLabGameMode::IsInPulseBay(const FVector& Player) const
{
    return Layout && IsValid(PulseDoor) && Player.X > Layout->Reflection.Door.X + 90.0;
}

float APowerLabGameMode::GetPulseSeconds() const
{
    return Pulse.RemainingSeconds();
}

float APowerLabGameMode::GetPulseFraction() const
{
    return Layout && Layout->PulseSeconds > 0.0f ? FMath::Clamp(GetPulseSeconds() / Layout->PulseSeconds, 0.0f, 1.0f) : 0.0f;
}

void APowerLabGameMode::ObservePulse(float DeltaSeconds, const FVector& Location)
{
    const FSceneBlock& Exit = Layout->PulseExit;
    const FBox ExitZone(Exit.Center - Exit.Size * 0.5, Exit.Center + Exit.Size * 0.5);
    Pulse.Observe(DeltaSeconds, ExitZone.IsInside(Location));
    UpdatePulseDisplay();
}

void APowerLabGameMode::UpdatePulseDisplay()
{
    const int32 Tenths = FMath::CeilToInt(GetPulseSeconds() * 10.0f);
    if (DisplayedPulseTenths == Tenths)
    {
        return;
    }
    const bool bPowered = Pulse.IsPowered();
    for (UStaticMeshComponent* Indicator : PulseIndicators)
    {
        Indicator->SetMaterial(0, bPowered ? Amber.Get() : Inactive.Get());
    }
    PulseClock->SetText(FText::FromString(bPowered ? FString::Printf(TEXT("%.1f s"), Tenths / 10.0f) : TEXT("READY")));
    PulseClock->SetTextRenderColor(bPowered ? FColor(255, 200, 105) : FColor::White);
    DisplayedPulseTenths = Tenths;
}

void APowerLabGameMode::ResetPulseBay()
{
    Pulse.Reset();
    PulseClamp.Release();
    PulseDoor->SetWorldLocation(Layout->PulseDoor.Center, false, nullptr, ETeleportType::TeleportPhysics);
    UpdatePulseDisplay();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "Player/MovementProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogPulseVerification, Log, All);

void APowerLabGameMode::VerifyPulse(APowerLabCharacter& Player, float DeltaSeconds)
{
    PulseTestTime += DeltaSeconds;
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogPulseVerification, Error, TEXT("Pulse verification failed at step %d: %s"), PulseTestStep, Reason);
        bVerifyPulse = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto Next = [this]
    {
        ++PulseTestStep;
        PulseTestTime = 0.0f;
    };
    const auto LookFrom = [&Player, this](FVector Point, const FVector& At)
    {
        Point.Z = Layout->Spawn.Z;
        const FVector Eye = Point + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((At - Eye).Rotation(), Point));
    };
    const auto Use = [&Player, &LookFrom](const FVector& Switch)
    {
        LookFrom(Switch + FVector(0, 135, 0), Switch);
        Player.Interact();
    };
    const auto DoorClosed = [this]
    {
        return PulseDoor->GetComponentLocation().Equals(Layout->PulseDoor.Center, 1.0);
    };
    if (PulseTestTime > FMath::Max(6.0f, Layout->PulseSeconds + 2.0f))
    {
        Fail(TEXT("timed door did not reach the expected physical state"));
        return;
    }
    switch (PulseTestStep)
    {
    case 0:
        if (!DoorClosed() || Pulse.IsComplete() || Pulse.IsPowered())
        {
            Fail(TEXT("timed bay should start closed and ready"));
            return;
        }
        Use(Layout->PulseButton);
        if (!Pulse.IsPowered() || !FMath::IsNearlyEqual(GetPulseSeconds(), Layout->PulseSeconds))
        {
            Fail(TEXT("E did not start a fresh pulse"));
            return;
        }
        Next();
        break;
    case 1:
        if (PulseTestTime >= 0.65f)
        {
            const float Before = GetPulseSeconds();
            Player.Interact();
            if (Before <= 0.0f || Before >= Layout->PulseSeconds || !FMath::IsNearlyEqual(GetPulseSeconds(), Before))
            {
                Fail(TEXT("a repeat press extended the live pulse"));
                return;
            }
            if (bCapture)
            {
                LookFrom(Layout->PulseButton + FVector(-180, 145, 0), Layout->PulseDoor.Center + FVector(0, -28, 52));
                FScreenshotRequest::RequestScreenshot(TEXT("PowerLab-Pulse"), false, false);
            }
            Next();
        }
        break;
    case 2:
        if (!Pulse.IsPowered() && DoorClosed())
        {
            Use(Layout->PulseButton);
            if (!Pulse.IsPowered() || Pulse.IsComplete())
            {
                Fail(TEXT("a timed out attempt could not be retried"));
                return;
            }
            Next();
        }
        break;
    case 3:
        if (Pulse.IsComplete())
        {
            if (!Pulse.IsPowered())
            {
                Fail(TEXT("player reached the exit after the pulse expired"));
                return;
            }
            UE_LOG(LogPulseVerification, Display, TEXT("Timed door crossed with %.2f seconds remaining."), GetPulseSeconds());
            Next();
        }
        else
        {
            const FVector Direction = (Layout->PulseExit.Center - Player.GetActorLocation()).GetSafeNormal2D();
            Player.AddMovementInput(Direction);
        }
        break;
    case 4:
        if (!Pulse.IsPowered() && DoorClosed())
        {
            Use(Layout->PulseReturn);
            if (!Pulse.IsPowered() || !Pulse.IsComplete())
            {
                Fail(TEXT("return switch did not reopen the completed passage"));
                return;
            }
            Next();
        }
        break;
    case 5:
        if (PulseDoor->GetComponentLocation().Z > Layout->PulseDoor.Center.Z + 250.0)
        {
            const FVector Threshold(Layout->PulseDoor.Center.X, Layout->PulseDoor.Center.Y, Layout->Spawn.Z);
            LookFrom(Threshold, Threshold + FVector::ForwardVector * 180.0);
            Next();
        }
        break;
    case 6:
        if (!Pulse.IsPowered() && DoorClosed())
        {
            if (!Player.GetActorLocation().Equals(Layout->PulseRetry, 3.0) || !Course.GetPower().ReflectionEmitter)
            {
                Fail(TEXT("the closing door did not return the player locally with optics still powered"));
                return;
            }
            Use(Layout->PulseButton);
            if (!Pulse.IsPowered())
            {
                Fail(TEXT("the pulse could not restart after the doorway recovery"));
                return;
            }
            Next();
        }
        break;
    case 7:
        Player.ResetChamber();
        if (Pulse.IsPowered() || Pulse.IsComplete() || !DoorClosed() || DisplayedPulseTenths != 0 ||
            !Player.GetActorLocation().Equals(Layout->Spawn, 3.0))
        {
            Fail(TEXT("reset left a pulse, open door or completion"));
            return;
        }
        UE_LOG(LogPulseVerification, Display,
            TEXT("Timed passage verification passed: countdown, expiry, retry, crossing, return switch, local recovery and reset."));
        bVerifyPulse = false;
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}
#endif
