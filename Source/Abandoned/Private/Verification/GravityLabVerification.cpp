#include "Verification/GravityLabVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Puzzles/GravityLab.h"
#include "Puzzles/GravityLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogGravityLab, Log, All);
#endif

UGravityLabVerification::UGravityLabVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UGravityLabVerification::AttachIfRequested(AGravityLabGameMode& Lab, AGravityLabCharacter& Player)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("VerifyGravityLab")))
    {
        return;
    }
    auto* Test = Lab.FindComponentByClass<UGravityLabVerification>();
    if (!Test)
    {
        Test = NewObject<UGravityLabVerification>(&Lab);
        Test->bCapture = FParse::Param(FCommandLine::Get(), TEXT("CaptureGravityLab"));
        Lab.AddInstanceComponent(Test);
        Test->AddTickPrerequisiteActor(&Lab);
        Test->RegisterComponent();
        Test->SetComponentTickEnabled(true);
    }
    // Observe the field applied to this position, then let movement consume the staged input.
    Player.AddTickPrerequisiteComponent(Test);
    Player.GetCharacterMovement()->AddTickPrerequisiteComponent(Test);
}

void UGravityLabVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    const auto* Lab = Cast<AGravityLabGameMode>(GetOwner());
    const auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<AGravityLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Lab || !IsValid(Player) || !IsValid(Lab->Layout) || !IsValid(Lab->Small) || !IsValid(Lab->Large))
    {
        Fail(TEXT("Lost the gravity lab player or ballast"));
        return;
    }
    Elapsed += DeltaSeconds;
    if (Elapsed > 7.0f)
    {
        UE_LOG(LogGravityLab, Display, TEXT("Player %s, gravity %s."),
            *Player->GetActorLocation().ToCompactString(),
            *Player->GetCharacterMovement()->GetGravityDirection().ToCompactString());
        Fail(TEXT("Timed out waiting for the physical field transition"));
        return;
    }
    Check(*Lab, *Player);
}

void UGravityLabVerification::Next(EPhase NextPhase, const TCHAR* Message)
{
    Phase = NextPhase;
    Elapsed = 0.0f;
    UE_LOG(LogGravityLab, Display, TEXT("Gravity lab: %s"), Message);
}

void UGravityLabVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogGravityLab, Error, TEXT("Gravity lab verification failed at %d: %s."), static_cast<int32>(Phase), Reason);
    SetComponentTickEnabled(false);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UGravityLabVerification::Check(const AGravityLabGameMode& Lab, AGravityLabCharacter& Player)
{
    using Direction = Abandoned::Domain::Puzzles::GravityDirection;
    const UGravityLabLayout& Layout = *Lab.Layout;
    APuzzleCube& Small = *Lab.Small;
    APuzzleCube& Large = *Lab.Large;
    const auto SetDirectionAtTerminal = [&Layout, &Player](Direction NewDirection)
    {
        Player.PlaceAtLab(FVector(Layout.Terminal.X, Layout.Terminal.Y + 142.0, Layout.Spawn.Z), Layout.Terminal);
        Player.SelectDirection(NewDirection);
    };
    const auto UsePowerSwitch = [&Layout, &Player]
    {
        Player.PlaceAtLab(FVector(Layout.PowerSwitch.X, Layout.PowerSwitch.Y + 142.0, Layout.Spawn.Z), Layout.PowerSwitch);
        Player.Interact();
    };
    switch (Phase)
    {
    case EPhase::Stack:
        if (Elapsed >= 0.15f)
        {
            const double ContactZ = Large.GetHalfSize() + 0.2;
            const FVector Base(Layout.LargeStart.X, Layout.LargeStart.Y, ContactZ);
            Large.ResetPosition(Base);
            const FVector Top = Base + FVector(0, 0, Large.GetHalfSize() + Small.GetHalfSize() + 0.2);
            if (!Small.SnapTo(*Large.GetBody(), Top, FRotator::ZeroRotator) ||
                Small.GetBody()->IsSimulatingPhysics() || Large.GetBody()->IsSimulatingPhysics())
            {
                Fail(TEXT("Could not set up a snapped stack under ordinary gravity"));
                return;
            }
            SetDirectionAtTerminal(Direction::Left);
            UsePowerSwitch();
            if (!Lab.IsPowered() || Lab.Selected != Direction::Left)
            {
                Fail(TEXT("The terminal and power switch were unreachable"));
                return;
            }
            Next(EPhase::ReleaseStack, TEXT("Set left at the terminal and powered the field."));
        }
        break;
    case EPhase::ReleaseStack:
        if (Small.HasSnapConstraints() || Large.HasSnapConstraints() ||
            !Small.GetBody()->IsSimulatingPhysics() || !Large.GetBody()->IsSimulatingPhysics())
        {
            Fail(TEXT("Sideways gravity did not release the snapped stack"));
            return;
        }
        Small.ResetPosition(Layout.SmallStart + FVector(0, 0, 250));
        Large.ResetPosition(Layout.LargeStart + FVector(0, 0, 250));
        Next(EPhase::MeasureMass, TEXT("Sideways gravity released the stack and restored physics to both cubes."));
        break;
    case EPhase::MeasureMass:
        if (Elapsed >= 0.26f)
        {
            const double LightSpeed = -Small.GetVelocity().Y;
            const double HeavySpeed = -Large.GetVelocity().Y;
            if (!Player.GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector) ||
                !FMath::IsNearlyEqual(Small.GetBody()->GetMass(), 5.0f, 0.05f) ||
                !FMath::IsNearlyEqual(Large.GetBody()->GetMass(), 10.0f, 0.05f) ||
                HeavySpeed < 100.0 || LightSpeed <= HeavySpeed * 1.6 || LightSpeed >= HeavySpeed * 2.4)
            {
                Fail(TEXT("Gravity escaped its boundary or did not respect body mass"));
                return;
            }
            UE_LOG(LogGravityLab, Display, TEXT("Gravity lab: lateral speeds %.1f cm/s (5 kg), %.1f cm/s (10 kg)."),
                LightSpeed, HeavySpeed);
            Player.PlaceAtLab(FVector(Layout.Field.Center.X - 330, 10, Layout.Goal.Center.Z),
                FVector(Layout.Goal.Center.X, 10, Layout.Goal.Center.Z + 75));
            Next(EPhase::LandOnWall, TEXT("Crossed the field boundary with the character."));
        }
        break;
    case EPhase::LandOnWall:
        if (Player.GetCharacterMovement()->IsMovingOnGround() &&
            Player.GetActorLocation().Y < Layout.Goal.Center.Y + 110.0)
        {
            if (Player.GetCharacterMovement()->GetGravityDirection().Y > -0.99 ||
                Player.GetActorUpVector().Y < 0.99)
            {
                Fail(TEXT("The movement frame or capsule did not land on the left wall"));
                return;
            }
            Next(EPhase::WalkOnWall, TEXT("Landed on the wall with the capsule aligned."));
        }
        break;
    case EPhase::WalkOnWall:
        if (Player.GetActorLocation().X < Layout.Goal.Center.X - 20.0)
        {
            Player.Move(FVector::ForwardVector);
        }
        if (const double Offset = Player.GetActorLocation().Z - Layout.Goal.Center.Z; FMath::Abs(Offset) > 22.0)
        {
            Player.Move(FVector::RightVector * FMath::Clamp(Offset / 130.0, -0.6, 0.6));
        }
        if (Lab.HasReachedGoal())
        {
            const FVector ViewUp = FVector::VectorPlaneProject(FVector::RightVector,
                Player.Camera->GetForwardVector()).GetSafeNormal();
            if (Player.Camera->GetUpVector().Dot(ViewUp) < 0.97)
            {
                UE_LOG(LogGravityLab, Display, TEXT("Camera up %s, capsule up %s, camera rotation %s, visual field up %s, pitch %.2f yaw %.2f."),
                    *Player.Camera->GetUpVector().ToCompactString(), *Player.GetActorUpVector().ToCompactString(),
                    *Player.Camera->GetComponentRotation().ToCompactString(),
                    *Player.ViewFrame.GetUpVector().ToCompactString(), Player.Pitch, Player.Yaw);
                Fail(TEXT("The eye horizon did not follow the new vertical"));
                return;
            }
            if (bCapture)
            {
                Player.Yaw = 175.0;
                Player.Pitch = -9.0;
                Player.UpdateView(1.0f);
                FScreenshotRequest::RequestScreenshot(TEXT("GravityLab-Walk"), false, false);
            }
            Next(EPhase::Reset, TEXT("Reached the marked target using gravity-relative movement."));
        }
        break;
    case EPhase::Reset:
        if (Elapsed >= 0.15f)
        {
            Player.ResetChamber();
            if (Lab.IsPowered() || Lab.HasReachedGoal() || Lab.Selected != Direction::Down ||
                !Player.GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector) ||
                !Player.GetActorLocation().Equals(Layout.Spawn, 1.0) ||
                !Small.GetBody()->IsGravityEnabled() || !Small.GetActorLocation().Equals(Layout.SmallStart, 1.0) ||
                !Large.GetBody()->IsGravityEnabled() || !Large.GetActorLocation().Equals(Layout.LargeStart, 1.0))
            {
                Fail(TEXT("Reset did not restore gravity, power, objective and starting positions"));
                return;
            }
            SetDirectionAtTerminal(Direction::Up);
            UsePowerSwitch();
            Player.PlaceAtLab(Layout.Field.Center, Layout.Field.Center + FVector(500, 0, 75));
            Player.ToggleHorizon();
            Next(EPhase::LandOnCeiling, TEXT("Reset and selected ceiling gravity with instant horizon."));
        }
        break;
    case EPhase::LandOnCeiling:
        if (Player.GetCharacterMovement()->IsMovingOnGround() && Player.GetActorLocation().Z > 490.0)
        {
            if (Player.GetCharacterMovement()->GetGravityDirection().Z < 0.99 || Player.Camera->GetUpVector().Z > -0.99)
            {
                Fail(TEXT("Ceiling gravity or instant horizon did not apply"));
                return;
            }
            CeilingStartX = Player.GetActorLocation().X;
            Next(EPhase::WalkOnCeiling, TEXT("Landed on the ceiling with instant horizon."));
        }
        break;
    case EPhase::WalkOnCeiling:
        if (!Player.GetCharacterMovement()->IsMovingOnGround() ||
            Player.GetCharacterMovement()->GetGravityDirection().Z < 0.99 || Player.Camera->GetUpVector().Z > -0.99)
        {
            Fail(TEXT("Lost ceiling contact or horizon while walking"));
            return;
        }
        if (Player.GetActorLocation().X < CeilingStartX + 100.0)
        {
            Player.Move(FVector::ForwardVector);
        }
        else
        {
            Player.ResetChamber();
            SetDirectionAtTerminal(Direction::Right);
            UsePowerSwitch();
            Player.PlaceAtLab(Layout.Field.Center, Layout.Field.Center + FVector(500, 0, 75));
            Next(EPhase::LandOnRightWall, TEXT("Walked 100 cm on the ceiling. Entered the right-wall field."));
        }
        break;
    case EPhase::LandOnRightWall:
        if (Player.GetCharacterMovement()->IsMovingOnGround() && Player.GetActorLocation().Y > 240.0)
        {
            if (Player.GetCharacterMovement()->GetGravityDirection().Y < 0.99)
            {
                Fail(TEXT("The right-wall setting did not become active"));
                return;
            }
            Next(EPhase::LeaveField, TEXT("Landed on the right wall."));
        }
        break;
    case EPhase::LeaveField:
        Player.Move(FVector::ForwardVector);
        if (Player.GetActorLocation().X > (Layout.Field.Center.X + Layout.Field.Size.X * 0.5) + 10.0)
        {
            if (!Player.GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector) ||
                Player.Camera->GetUpVector().Z < 0.99)
            {
                Fail(TEXT("Leaving the field did not restore ordinary gravity and horizon"));
                return;
            }
            UE_LOG(LogGravityLab, Display, TEXT("Gravity lab verification passed: boundaries, mass, wall movement, ceiling and reset."));
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif
