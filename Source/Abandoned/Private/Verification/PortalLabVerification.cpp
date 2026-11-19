#include "Verification/PortalLabVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Infrastructure/Puzzles/PortalRoomViewComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Puzzles/PortalLab.h"
#include "Puzzles/PortalLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "UnrealClient.h"
#include "Verification/CarryVerification.h"
#include "Verification/PortalShapeVerification.h"
#include "Verification/VerificationPlayerPose.h"

DEFINE_LOG_CATEGORY_STATIC(LogPortalLabTest, Log, All);

// Stack-local access to this world's actors. Reset crossing history when staging a teleport.
struct FPortalLabFixture
{
    APortalLabGameMode& Lab;
    APortalLabCharacter& Player;
    APlayerController& Controller;

    bool IsReady() const
    {
        return IsValid(Lab.Layout) && IsValid(Lab.Transit) && IsValid(Lab.RoomView) && IsValid(Lab.RoomView->TextureTarget) &&
            Lab.Mouths.Num() == 2 && IsValid(Lab.Mouths[0].Console) && IsValid(Lab.Mouths[0].Shutter) &&
            IsValid(Lab.RoomView->Windows[0]) && IsValid(Lab.RoomView->Windows[1]) &&
            Lab.Cubes.Num() == 2 && IsValid(Lab.Cubes[0]) && IsValid(Lab.Cubes[1]);
    }
    const UPortalLabLayout& Layout() const { return *Lab.Layout; }
    APuzzleCube& Cube(int32 Index) const { return *Lab.Cubes[Index]; }
    const FPortalMouth& Mouth(int32 Index) const { return Lab.Mouths[Index]; }
    const UStaticMeshComponent& Window(int32 Index) const { return *Lab.RoomView->Windows[Index]; }
    void HoldPortalForProfile(bool bFreezeCapture)
    {
        // Keep the powered aperture and its last verified image in the same scene.
        Lab.RoomView->SetVisibility(!bFreezeCapture, false);
        UE_LOG(LogPortalLabTest, Display, TEXT("Portal profile ready: %s, target %dx%d, view %s."),
            bFreezeCapture ? TEXT("frozen") : TEXT("active"), Lab.RoomView->TextureTarget->SizeX,
            Lab.RoomView->TextureTarget->SizeY, *Controller.GetControlRotation().ToString());
        CSV_EVENT_GLOBAL(TEXT("PortalProfileReady"));
    }

    int32 BlockedTransfers() const { return Lab.Transit->GetStats().Blocked; }

    void Place(const FTransform& Pose)
    {
        Abandoned::Verification::PlacePlayer(Player, Pose);
        Lab.PlayerState = {};
    }
    void AimFrom(FVector At, const FVector& Target)
    {
        At.Z = Layout().Spawn.Z;
        const FVector Eye = At + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Place(FTransform((Target - Eye).Rotation(), At));
    }
    void PlaceCube(int32 Index, const FVector& Center)
    {
        Cube(Index).ResetPosition(Center);
        Lab.CubeStates[Index] = {};
    }

    bool RejectsSphere()
    {
        auto* Sphere = NewObject<USphereComponent>(&Player);
        const FTransform& Entrance = Mouth(0).Pose;
        Sphere->SetWorldLocation(Entrance.TransformPosition(FVector(-1, 0, 0)));
        FPortalTravelerState Probe{Entrance.TransformPosition(FVector(1, 0, 0)), 0, true};
        const FTransform Before = Player.GetActorTransform();
        const FVector Speed = Player.GetCharacterMovement()->Velocity;
        Lab.Transit->Observe(*Sphere, Probe, Lab.Supply.IsPowered());
        return Probe.bUnsupportedShape && !Probe.bTracked && Probe.Approach == INDEX_NONE &&
            Player.GetActorTransform().Equals(Before) && Player.GetCharacterMovement()->Velocity.Equals(Speed);
    }
    bool DestroyHeldCube(UObjectCarryComponent& Grip)
    {
        const float UnloadedSpeed = Player.GetCharacterMovement()->MaxWalkSpeed;
        APuzzleCube* Temporary = Lab.SpawnCube(0);
        if (!Temporary)
        {
            return false;
        }
        const bool bGrabbed = Grip.TryGrab(*Temporary->GetBody(), Temporary->GetMassKg());
        const bool bDestroyed = Temporary->Destroy();
        return bGrabbed && bDestroyed && !Grip.HeldBody() &&
            FMath::IsNearlyEqual(Player.GetCharacterMovement()->MaxWalkSpeed, UnloadedSpeed);
    }
    bool ReadCenter(FLinearColor& Sample) const
    {
        UTextureRenderTarget2D& Target = *Lab.RoomView->TextureTarget;
        const int32 X = Target.SizeX / 2;
        const int32 Y = Target.SizeY / 2;
        auto* Resource = Target.GameThread_GetRenderTargetResource();
        TArray<FLinearColor> Pixels;
        if (!Resource || !Resource->ReadLinearColorPixels(Pixels, FReadSurfaceDataFlags(RCM_MinMax),
            FIntRect(X - 1, Y - 1, X + 2, Y + 2)) || Pixels.IsEmpty())
        {
            return false;
        }
        Sample = Pixels[Pixels.Num() / 2];
        return true;
    }
};
#endif

UPortalLabVerification::UPortalLabVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UPortalLabVerification::AttachIfRequested(APortalLabGameMode& Lab)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("VerifyPortalLab")))
    {
        auto* Test = NewObject<UPortalLabVerification>(&Lab);
        Test->bCapture = FParse::Param(FCommandLine::Get(), TEXT("CapturePortalLab"));
        Lab.AddInstanceComponent(Test);
        Test->RegisterComponent();
        Test->AddTickPrerequisiteActor(&Lab);
        Test->SetComponentTickEnabled(true);
    }
}

void UPortalLabVerification::Next(EPhase NextPhase, const TCHAR* Message)
{
    UE_LOG(LogPortalLabTest, Display, TEXT("Portal lab: %s"), Message);
    Elapsed = 0.0f;
    Phase = NextPhase;
}

void UPortalLabVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogPortalLabTest, Error, TEXT("Portal lab test failed: %s (phase %d)."), Reason, static_cast<int32>(Phase));
    SetComponentTickEnabled(false);
    Input.Release();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UPortalLabVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    Input.Release();
    if (APlayerController* Viewer = ScriptedViewer.Get())
    {
        Viewer->SetIgnoreLookInput(false);
    }
    Super::EndPlay(Reason);
}

void UPortalLabVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    Input.Release();
    Elapsed += DeltaSeconds;
    if (Elapsed > 8.0f)
    {
        Fail(TEXT("Timed out waiting for the player or a physical transition"));
        return;
    }
    auto* Lab = Cast<APortalLabGameMode>(GetOwner());
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<APortalLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Lab || !Player)
    {
        return;
    }
    FPortalLabFixture Fixture{*Lab, *Player, *Controller};
    if (!Fixture.IsReady())
    {
        Fail(TEXT("Portal fixture disappeared"));
        return;
    }
    if (bCapture && !bCapturedFirstFrame && Fixture.Window(0).IsVisible())
    {
        FScreenshotRequest::RequestScreenshot(TEXT("PortalLab-FirstVisible"), false, false);
        bCapturedFirstFrame = true;
        return; // Keep the first viewport capture and the steady-state capture on distinct frames.
    }
    Check(Fixture);
}

void UPortalLabVerification::Check(FPortalLabFixture& Fixture)
{
    auto& Player = Fixture.Player;
    const auto& Layout = Fixture.Layout();
    const FPortalTransitObservation Transit = Fixture.Lab.GetTransitObservation();
    const auto VelocityRotated = [&Transit]
    {
        return Transit.Before.X > 300.0 && Transit.After.Equals(FVector(-Transit.Before.Y, Transit.Before.X, Transit.Before.Z), 0.05);
    };
    const auto WalkForward = [&Player]
    {
        Player.AddMovementInput(FRotator(0, Player.GetControlRotation().Yaw, 0).Vector());
    };
    switch (Phase)
    {
    case EPhase::Closed:
        if (Elapsed >= 0.2f)
        {
            const FVector Center = Layout.Arrival.Center;
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(ClosedPortal), false, &Player);
            if (!Abandoned::Verification::VerifyPortalShapeProjection())
            {
                Fail(TEXT("Incorrect capsule or rotated box aperture extent"));
                return;
            }
            FHitResult Hit;
            GetWorld()->LineTraceSingleByChannel(Hit, Center - FVector(100, 0, 0),
                Center + FVector(100, 0, 0), ECC_Visibility, Query);
            if (Transit.bPowered || Hit.GetComponent() != Fixture.Mouth(0).Shutter)
            {
                Fail(TEXT("The unpowered portal was not solid"));
                return;
            }
            // The scenario stages its own view. Desktop mouse motion must not rotate the camera between screenshots.
            ScriptedViewer = &Fixture.Controller;
            ScriptedViewer->SetIgnoreLookInput(true);
            Fixture.AimFrom(Layout.Arrival.Console + FVector(0, 120, 0), Layout.Arrival.Console);
            Input.Press(Fixture.Controller, EKeys::E);
            Next(EPhase::Power, TEXT("Testing the console from the closed entrance."));
        }
        break;
    case EPhase::Power:
        {
            if (!Transit.bPowered)
            {
                Fail(TEXT("Portal console did not power the pair"));
                return;
            }
            if (!Fixture.RejectsSphere())
            {
                Fail(TEXT("Unsupported shape changed the traveler or retained a pending crossing"));
                return;
            }
            UE_LOG(LogPortalLabTest, Display, TEXT("Portal collision shapes verified: capsule, rotated box and rejected sphere."));
            auto* Carry = Player.FindComponentByClass<UObjectCarryComponent>();
            if (!Carry || !Abandoned::Verification::VerifyRejectedCarryTransit(
                    *Carry, *Fixture.Cube(0).GetBody(), *Player.GetCharacterMovement()))
            {
                Fail(TEXT("Failed re-grab did not restore carry state"));
                return;
            }
            Fixture.PlaceCube(0, Layout.SmallCube);
            UE_LOG(LogPortalLabTest, Display, TEXT("Rejected carry transit restored pose, velocities, pawn response and movement load."));
            if (!Fixture.DestroyHeldCube(*Carry))
            {
                Fail(TEXT("Destroying a held cube left a grip or movement load"));
                return;
            }
            Fixture.Place(FTransform(FRotator::ZeroRotator, Layout.Arrival.Center - FVector(320, 0, 53)));
            Next(EPhase::View, TEXT("Unpowered shutter blocked the aperture. Console supplied the pair."));
        }
        break;
    case EPhase::View:
        if (Elapsed >= 0.50f)
        {
            if (!Fixture.Window(0).IsVisible() || Fixture.Window(1).IsVisible())
            {
                Fail(TEXT("Portal view failed to select the entrance"));
                return;
            }
            if (bCapture)
            {
                FScreenshotRequest::RequestScreenshot(TEXT("PortalLab-Active"), false, false);
            }
            Next(EPhase::ReadCapture, TEXT("Checking the rendered aperture."));
        }
        break;
    case EPhase::ReadCapture:
        if (!FScreenshotRequest::IsScreenshotRequested())
        {
            FLinearColor Sample;
            if (!Fixture.ReadCenter(Sample))
            {
                Fail(TEXT("Could not read the linked aperture capture"));
                return;
            }
            UE_LOG(LogPortalLabTest, Display, TEXT("Linked scene center %s via %s."),
                *Sample.ToString(), *GetNameSafe(Fixture.Window(0).GetMaterial(0)));
            if (Sample.R <= 0.0f || Sample.R <= Sample.B)
            {
                Fail(TEXT("The aperture capture did not contain the destination's warm wall panel"));
                return;
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("ProfilePortal")))
            {
                Fixture.HoldPortalForProfile(FParse::Param(FCommandLine::Get(), TEXT("FreezePortalCapture")));
                SetComponentTickEnabled(false);
                return;
            }
            Next(EPhase::LaunchCube, TEXT("Powered entrance captured the linked room."));
        }
        break;
    case EPhase::LaunchCube:
        if (Elapsed >= 0.15f)
        {
            Fixture.Place(FTransform(FRotator::ZeroRotator, Layout.Spawn));
            Fixture.PlaceCube(0, Layout.Arrival.Center - FVector(100, 0, 46));
            Fixture.Cube(0).GetBody()->SetPhysicsLinearVelocity(FVector(1350, 0, 30));
            Next(EPhase::FreeTransit, TEXT("Sent a free physical cube through the frame."));
        }
        break;
    case EPhase::FreeTransit:
        if (Transit.Transfers == 1)
        {
            if (!VelocityRotated() || Fixture.Cube(0).GetActorLocation().X < Layout.Departure.Center.X - 80.0)
            {
                Fail(TEXT("The free cube did not arrive with the rotated velocity"));
                return;
            }
            Fixture.PlaceCube(0, Layout.SmallCube);
            Fixture.PlaceCube(1, Layout.Departure.Center + FVector(0, 38, -103));
            Fixture.Place(FTransform(FRotator::ZeroRotator, Layout.Arrival.Center - FVector(135, 0, 53)));
            Next(EPhase::BlockedExit, TEXT("Free cube retained speed through the 90 degree turn. Testing a blocked exit."));
        }
        break;
    case EPhase::BlockedExit:
        WalkForward();
        if (Fixture.BlockedTransfers() > 0)
        {
            if (Transit.Transfers != 1 || Player.GetActorLocation().X >= Layout.Arrival.Center.X)
            {
                Fail(TEXT("An occupied exit admitted the player"));
                return;
            }
            Fixture.PlaceCube(1, Layout.LargeCube);
            Fixture.AimFrom(Layout.SmallCube - FVector(130, 0, 0), Fixture.Cube(0).GetActorLocation());
            Input.Press(Fixture.Controller, EKeys::E);
            Next(EPhase::Pickup, TEXT("Occupied exit stopped transit. Picking up the small cube."));
        }
        break;
    case EPhase::Pickup:
        HeldCube = Player.GetHeldCube();
        if (HeldCube.Get() != &Fixture.Cube(0))
        {
            Fail(TEXT("Player did not pick up the test cube"));
            return;
        }
        Fixture.Place(FTransform(FRotator(-18, 0, 0), Layout.Arrival.Center - FVector(235, 0, 53)));
        Next(EPhase::LaunchPlayer, TEXT("Player picked up the cube through the gameplay trace."));
        break;
    case EPhase::LaunchPlayer:
        if (Elapsed >= 0.28f)
        {
            Player.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            Player.GetCharacterMovement()->Velocity = FVector(660, 0, 560);
            Next(EPhase::CarriedTransit, TEXT("Entering the portal airborne with the cube held."));
        }
        break;
    case EPhase::CarriedTransit:
        WalkForward();
        if (Transit.Transfers == 2)
        {
            if (!HeldCube.IsValid() || Player.GetHeldCube() != HeldCube.Get() ||
                !VelocityRotated() || FMath::Abs(Transit.Before.Z) < 25.0 ||
                !FMath::IsNearlyEqual(Player.GetViewRotation().Yaw, 90.0, 1.0) ||
                !Player.GetCharacterMovement()->Velocity.Equals(Transit.After, 0.1))
            {
                Fail(TEXT("Transfer lost motion, aim or ownership of the held object"));
                return;
            }
            Next(EPhase::WaitAtExit, TEXT("Player arrived airborne with the same cube, rotated view and unchanged speed."));
        }
        break;
    case EPhase::WaitAtExit:
        if (Elapsed > 0.22f)
        {
            if (Transit.Transfers != 2 || !HeldCube.IsValid() || Player.GetHeldCube() != HeldCube.Get())
            {
                Fail(TEXT("Immediate reverse transfer or loss of grip"));
                return;
            }
            Fixture.Place(FTransform(FRotator(-18, -90, 0), Layout.Departure.Center + FVector(0, 238, -53)));
            Next(EPhase::ReverseTransit, TEXT("No repeat transit at the exit. Returning with the same cube."));
        }
        break;
    case EPhase::ReverseTransit:
        if (Elapsed > 0.30f)
        {
            WalkForward();
        }
        if (Transit.Transfers == 3)
        {
            if (!HeldCube.IsValid() || Player.GetHeldCube() != HeldCube.Get() ||
                Transit.Before.Y >= -300.0 || !Transit.After.Equals(FVector(Transit.Before.Y, -Transit.Before.X, Transit.Before.Z), 0.05))
            {
                Fail(TEXT("Reverse transit lost the cube or the inverse velocity transform"));
                return;
            }
            Next(EPhase::ReturnToEntrance, TEXT("The inverse passage retained the cube and speed."));
        }
        break;
    case EPhase::ReturnToEntrance:
        if (Elapsed > 0.22f)
        {
            Fixture.Place(FTransform(FRotator(-18, 0, 0), Layout.Arrival.Center - FVector(238, 0, 53)));
            Next(EPhase::CutPower, TEXT("Approaching the first portal again to test loss of power."));
        }
        break;
    case EPhase::CutPower:
        if (Elapsed > 0.30f)
        {
            WalkForward();
            if (Transit.bApproachingArrival)
            {
                Fixture.Lab.UseDevice(Player, *Fixture.Mouth(0).Console);
                Next(EPhase::Reset, TEXT("Cut the supply while the carried object overlapped the active frame."));
            }
        }
        break;
    case EPhase::Reset:
        if (Transit.bInterrupted)
        {
            if (Fixture.Window(0).IsVisible() || Fixture.Window(1).IsVisible())
            {
                Fail(TEXT("Power loss left the linked room visible"));
                return;
            }
            const auto* Grip = Player.FindComponentByClass<UObjectCarryComponent>();
            if (Transit.bPowered || HeldCube.IsValid() || !Grip || Grip->HeldBody() ||
                !Player.GetActorLocation().Equals(Layout.Spawn, 4.0))
            {
                Fail(TEXT("Power loss did not destroy the item and restore the checkpoint"));
                return;
            }
            UE_LOG(LogPortalLabTest, Display, TEXT("Portal lab verification passed: view, power, free cube, blocked exit, carried item, inertia and interruption."));
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif
