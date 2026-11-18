#include "Verification/OpticsVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PowerLab.h"
#include "Puzzles/PowerLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpticsVerification, Log, All);
#endif

UOpticsVerification::UOpticsVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UOpticsVerification::AttachIfRequested(APowerLabGameMode& Lab)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("VerifyPowerLab")))
    {
        auto* Test = NewObject<UOpticsVerification>(&Lab);
        Test->bFreeReflector = FParse::Param(FCommandLine::Get(), TEXT("VerifyFreeReflector"));
        Test->bCapture = FParse::Param(FCommandLine::Get(), TEXT("CapturePowerLab"));
        Lab.AddInstanceComponent(Test);
        Test->RegisterComponent();
        Test->AddTickPrerequisiteActor(&Lab);
        Test->SetComponentTickEnabled(true);
    }
}

void UOpticsVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    Elapsed += DeltaSeconds;
    auto* Lab = Cast<APowerLabGameMode>(GetOwner());
    const auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<APowerLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player)
    {
        if (Elapsed > 6.0f)
        {
            Fail(TEXT("Timed out waiting for the optical lab player."));
        }
        return;
    }
    if (!Lab || !IsValid(Lab->Layout) || !IsValid(Lab->Cube) || !IsValid(Lab->Mirror) ||
        !IsValid(Lab->MirrorSurface) || !IsValid(Lab->Reflection.Door) || !IsValid(Lab->Interruption.Door))
    {
        Fail(TEXT("Optical lab fixture disappeared."));
        return;
    }
    Check(*Lab, *Player, DeltaSeconds);
}

void UOpticsVerification::Next(EPhase NextPhase, const TCHAR* Message)
{
    UE_LOG(LogOpticsVerification, Display, TEXT("Power lab: %s"), Message);
    Phase = NextPhase;
    Elapsed = 0.0f;
}

void UOpticsVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogOpticsVerification, Error, TEXT("Power lab verification failed at step %d: %s"), static_cast<int32>(Phase), Reason);
    SetComponentTickEnabled(false);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UOpticsVerification::Check(APowerLabGameMode& Lab, APowerLabCharacter& Player, float Delta)
{
    constexpr float ExpectedMirrorMassKg = 8.0f;
    constexpr double DockRadiusCm = 100.0;
    const UPowerLabLayout& Layout = *Lab.Layout;
    UBoxComponent& Mirror = *Lab.Mirror;
    const FOpticalLane& Interruption = Lab.Interruption;
    FOpticalLane& Reflection = Lab.Reflection;
    const auto LookFrom = [&Player, &Layout](FVector Feet, FVector At)
    {
        const FVector Center(Feet.X, Feet.Y, Layout.Spawn.Z);
        const FVector Eye = Center + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((At - Eye).Rotation(), Center));
    };
    const FVector MirrorPlacement = Layout.MirrorDock +
        (bFreeReflector ? FVector(-140, 125, 0) : FVector::ZeroVector);
    if (Elapsed > 6.0f)
    {
        if (bFreeReflector && (Phase == EPhase::PlaceMirror || Phase == EPhase::AimMirror || Phase == EPhase::ReflectedSignal))
        {
            UE_LOG(LogOpticsVerification, Error, TEXT("Free mirror pose %s, up %.3f, speed %.2f, spin %.2f, aimable %d."),
                *Mirror.GetComponentLocation().ToCompactString(), Mirror.GetUpVector().Z,
                Mirror.GetPhysicsLinearVelocity().Size(), Mirror.GetPhysicsAngularVelocityInDegrees().Size(),
                Lab.CanAimMirror(Player));
        }
        Fail(TEXT("Timed out waiting for a physical transition."));
        return;
    }
    switch (Phase)
    {
    case EPhase::Unpowered:
        if (Elapsed < 0.2f)
        {
            return;
        }
        if (Lab.Course.GetPower().DirectEmitter || Lab.Course.GetPower().ReflectionEmitter)
        {
            Fail(TEXT("The unpowered lab emitted a signal."));
            return;
        }
        LookFrom(Layout.Button + FVector(0, 135, 0), Layout.Button);
        Player.Interact();
        Next(EPhase::DirectSignal, TEXT("Used the supply button."));
        break;
    case EPhase::DirectSignal:
        if (Lab.bDirectSignal)
        {
            LookFrom(Layout.Interruption.Emitter + FVector(0, 320, 0), Layout.Interruption.Receiver);
            Next(EPhase::SuitBlocksBeam, TEXT("Receiver acquired the direct beam."));
        }
        break;
    case EPhase::SuitBlocksBeam:
        if (!Lab.bDirectSignal && Elapsed >= 0.12f)
        {
            Player.ResetChamber();
            if (Lab.Course.GetPower().DirectEmitter || !Interruption.Door->GetComponentLocation().Equals(Layout.Interruption.Door))
            {
                Fail(TEXT("Reset left the circuit or first door active."));
                return;
            }
            LookFrom(Layout.Button + FVector(0, 135, 0), Layout.Button);
            Player.Interact();
            Next(EPhase::RestoredSignal, TEXT("Suit blocked the beam and reset cleared the circuit."));
        }
        break;
    case EPhase::RestoredSignal:
        if (Lab.bDirectSignal)
        {
            Lab.Cube->ResetPosition(FVector(Layout.Interruption.Emitter.X, 20.0, 42.0));
            Next(EPhase::CubeBlocksBeam, TEXT("Put a physical ballast cube across the beam."));
        }
        break;
    case EPhase::CubeBlocksBeam:
        if (!Lab.bDirectSignal && Interruption.Door->GetComponentLocation().Z > Layout.Interruption.Door.Z + 250)
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout.Interruption.Door - FVector(135, 0, 38)));
            Next(EPhase::CrossFirstDoor, TEXT("Loss of light enabled the second bay and opened the first door."));
        }
        break;
    case EPhase::CrossFirstDoor:
        Player.AddMovementInput(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Layout.Interruption.Door.X + 115)
        {
            LookFrom(Layout.Mirror + FVector(-125, 0, 0), Layout.Mirror);
            Player.Interact();
            if (Player.GetHeldBody() != &Mirror)
            {
                Fail(TEXT("Could not pick up the portable mirror."));
                return;
            }
            Player.AimMirror();
            if (Player.IsAiming())
            {
                Fail(TEXT("held reflector entered adjustment mode"));
                return;
            }
            LookFrom(MirrorPlacement + FVector(0, 127, 0), MirrorPlacement);
            Next(EPhase::PlaceMirror, TEXT("Crossed the door and picked up the mirror."));
        }
        break;
    case EPhase::PlaceMirror:
        if (FVector::DistSquared(Mirror.GetComponentLocation(), MirrorPlacement) < FMath::Square(45.0))
        {
            Player.Interact();
            if (Lab.bMirrorDocked == bFreeReflector || Player.GetHeldBody())
            {
                Fail(TEXT("Mirror placement did not enter the expected support state."));
                return;
            }
            LookFrom(Layout.Reflection.Emitter + FVector(-105, 100, 0), Layout.Reflection.Emitter);
            Player.Interact();
            if (Player.Aim != EOpticsAim::Emitter)
            {
                Fail(TEXT("Emitter interaction did not enter aiming."));
                return;
            }
            LookFrom(Player.GetActorLocation(), Lab.MirrorSurface->GetComponentLocation());
            Next(EPhase::AimMirror, bFreeReflector ? TEXT("Placed the carried mirror off the dock and aimed the emitter.")
                : TEXT("Docked the carried mirror and entered live emitter aiming."));
        }
        break;
    case EPhase::AimMirror:
        if (Elapsed >= 0.12f && Lab.CanAimMirror(Player))
        {
            Player.Interact();
            LookFrom(Mirror.GetComponentLocation() + FVector(-119, 72, 0), Lab.MirrorSurface->GetComponentLocation());
            Player.AimMirror();
            if (Player.Aim != EOpticsAim::Mirror)
            {
                Fail(TEXT("Mirror interaction did not enter aiming."));
                return;
            }
            LookFrom(Player.GetActorLocation(), Layout.Reflection.Receiver);
            Next(EPhase::ReflectedSignal, TEXT("Aimed the reflector with the same trace as mouse input."));
        }
        break;
    case EPhase::ReflectedSignal:
        if (Lab.bReflectedSignal && Reflection.Door->GetComponentLocation().Z > Layout.Reflection.Door.Z + 250)
        {
            Player.Interact();
            if (bCapture)
            {
                LookFrom(MirrorPlacement + FVector(-380, -120, 0),
                    (Lab.MirrorSurface->GetComponentLocation() + Layout.Reflection.Receiver) * 0.5);
            }
            const EPhase AfterReflection = bFreeReflector ? EPhase::AdjustFreeMirror : EPhase::ApproachExit;
            Next(bCapture ? EPhase::CaptureReflection : AfterReflection, TEXT("Real reflected ray reached its receiver and opened the exit."));
        }
        break;
    case EPhase::ApproachExit:
        if (Elapsed >= 0.15f)
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout.Reflection.Door - FVector(130, 0, 38)));
            Next(EPhase::CrossExit, TEXT("Approaching the powered exit."));
        }
        break;
    case EPhase::CrossExit:
        Player.AddMovementInput(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Layout.Reflection.Door.X + 95)
        {
            Lab.bMirrorDocked = false;
            FVector UnderDoor = Layout.Reflection.Door;
            UnderDoor.Z += Mirror.GetUnscaledBoxExtent().Z - Reflection.Door->GetUnscaledBoxExtent().Z + 1.0;
            Mirror.SetWorldLocationAndRotation(UnderDoor, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
            Mirror.SetSimulatePhysics(true);
            Mirror.SetPhysicsLinearVelocity(FVector::ZeroVector);
            Mirror.SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            Reflection.Direction = FVector::UpVector;
            Next(EPhase::MirrorClamped, TEXT("Optical exit passed. Closing the unpowered door over the mirror."));
        }
        break;
    case EPhase::MirrorClamped:
        if (Reflection.Clamp.Holds(&Mirror))
        {
            if (Mirror.IsSimulatingPhysics())
            {
                Fail(TEXT("the door did not retain the mirror"));
                return;
            }
            Next(EPhase::ReleaseMirror, TEXT("Optical door clamped the portable mirror."));
        }
        break;
    case EPhase::ReleaseMirror:
        Lab.MoveDoor(Reflection.Door, Reflection.Clamp, Layout.Reflection.Door, true, Delta, Player);
        if (!Reflection.Clamp.HasObject())
        {
            if (!Mirror.IsSimulatingPhysics() || !FMath::IsNearlyEqual(Mirror.GetMass(), ExpectedMirrorMassKg, 0.05f))
            {
                Fail(TEXT("opening did not restore mirror physics and mass"));
                return;
            }
            UE_LOG(LogOpticsVerification, Display, TEXT("Power lab verification passed: switch, blockers, reflection, exit and mirror clamp."));
            SetComponentTickEnabled(false);
            Lab.StartPulseVerification(bCapture);
        }
        break;
    case EPhase::AdjustFreeMirror:
        if (Elapsed >= 0.15f)
        {
            if (Lab.bMirrorDocked || !Mirror.IsSimulatingPhysics() ||
                FVector::Dist2D(Mirror.GetComponentLocation(), Layout.MirrorDock) <= DockRadiusCm)
            {
                Fail(TEXT("the free reflector route used the dock"));
                return;
            }
            LookFrom(Mirror.GetComponentLocation() + FVector(-119, 72, 0), Lab.MirrorSurface->GetComponentLocation());
            Player.AimMirror();
            if (Player.Aim != EOpticsAim::Mirror)
            {
                Fail(TEXT("a settled reflector outside the dock could not be aimed again"));
                return;
            }
            LookFrom(Player.GetActorLocation(), Layout.Reflection.Receiver + FVector(0, 240, 75));
            Next(EPhase::LostReflection, TEXT("Turned the free reflector away from its receiver."));
        }
        break;
    case EPhase::LostReflection:
        if (!Lab.bReflectedSignal && Reflection.Door->GetComponentLocation().Equals(Layout.Reflection.Door, 1.0))
        {
            LookFrom(Player.GetActorLocation(), Layout.Reflection.Receiver);
            Next(EPhase::RestoredReflection, TEXT("Turning off target closed the real door. Aimed at the receiver again."));
        }
        break;
    case EPhase::RestoredReflection:
        if (Lab.bReflectedSignal && Reflection.Door->GetComponentLocation().Z > Layout.Reflection.Door.Z + 250)
        {
            Player.Interact();
            UE_LOG(LogOpticsVerification, Display, TEXT("Free reflector verification passed: placed off-dock, lost signal and reopened."));
            Next(EPhase::ApproachExit, TEXT("Free reflector solution restored the reflected signal."));
        }
        break;
    case EPhase::CaptureReflection:
        if (Elapsed >= 0.18f)
        {
            FScreenshotRequest::RequestScreenshot(bFreeReflector ? TEXT("PowerLab-Free-Reflector")
                : TEXT("PowerLab-Reflection"), false, false);
            Next(bFreeReflector ? EPhase::AdjustFreeMirror : EPhase::ApproachExit, TEXT("Requested optical capture."));
        }
        break;
    }
}
#endif
