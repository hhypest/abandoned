#include "TrainingChamber/TrainingChamber.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "TrainingChamber/TrainingChamberLayout.h"

#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDoorClampVerification, Log, All);

void ATrainingChamberGameMode::VerifyDoorClamp(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    DoorClampTestSeconds += DeltaSeconds;
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogDoorClampVerification, Error, TEXT("Door clamp verification failed at step %d: %s."),
            static_cast<int>(DoorClampTest), Reason);
        bVerifyDoorClamp = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    if (DoorClampTestSeconds > 4.0 || Cubes.Num() < 3 || !IsValid(Cubes[0]) || !IsValid(Cubes[1]) || !IsValid(Cubes[2]))
    {
        Fail(TEXT("fixture missing or timed out"));
        return;
    }
    APuzzleCube& Lower = *Cubes[0];
    APuzzleCube& Upper = *Cubes[1];
    APuzzleCube& TestObject = *Cubes[2];
    UBoxComponent& Body = *TestObject.GetBody();
    const FVector DoorCenter = Layout->Door.Center;
    const auto Step = [this](EDoorClampTest Next)
    {
        DoorClampTest = Next;
        DoorClampTestSeconds = 0.0;
    };
    const auto Snap = [](APuzzleCube& Cube, UBoxComponent& Surface)
    {
        const double Height = Surface.GetScaledBoxExtent().Z + Cube.GetHalfSize() + 0.5;
        return Cube.SnapTo(Surface, Surface.GetComponentLocation() + FVector(0, 0, Height), FRotator::ZeroRotator);
    };
    const auto SupplyDoor = [&]()
    {
        PressButton();
        return Snap(Lower, *GetPlate()) && Snap(Upper, *Lower.GetBody());
    };
    const auto AimAtObject = [&]()
    {
        const FVector Target = TestObject.GetActorLocation();
        const FVector Position(Target.X - 145.0, Target.Y, Layout->Spawn.Z);
        const FVector Eye = Position + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Target - Eye).Rotation(), Position));
    };
    const auto SignsVisible = [this](bool Visible)
    {
        TArray<UTextRenderComponent*> Labels;
        Door->GetOwner()->GetComponents<UTextRenderComponent>(Labels);
        return Labels.Num() == 2 && !Labels.ContainsByPredicate([Visible](const UTextRenderComponent* Label)
        {
            return Label->IsVisible() != Visible;
        });
    };
    const auto Obstruct = [&]()
    {
        TestObject.ResetPosition(FVector(DoorCenter.X, DoorCenter.Y,
            DoorCenter.Z - Layout->Door.Size.Z * 0.5 + TestObject.GetHalfSize() + 1.0));
        ReturnCube(Player, 1);
    };

    switch (DoorClampTest)
    {
    case EDoorClampTest::PowerOn:
        if (DoorClampTestSeconds >= 0.25)
        {
            if (!SupplyDoor())
            {
                Fail(TEXT("could not load the pressure plate"));
                return;
            }
            Step(EDoorClampTest::Open);
        }
        break;
    case EDoorClampTest::Open:
        if (IsDoorOpen())
        {
            Obstruct();
            Step(EDoorClampTest::Clamp);
        }
        break;
    case EDoorClampTest::Clamp:
        if (DoorClamp.Holds(&Body))
        {
            ClampedObjectPosition = Body.GetComponentLocation();
            ObstructedDoorPosition = Door->GetComponentLocation();
            AimAtObject();
            Step(EDoorClampTest::Hold);
        }
        break;
    case EDoorClampTest::Hold:
        if (DoorClampTestSeconds >= 0.35)
        {
            if (!DoorClamp.Holds(&Body) || Body.IsSimulatingPhysics() || !SignsVisible(true) ||
                !Body.GetComponentLocation().Equals(ClampedObjectPosition, 0.1) ||
                !Door->GetComponentLocation().Equals(ObstructedDoorPosition, 0.1) ||
                Player.TraceInteractable() != &Body)
            {
                Fail(TEXT("the object, door or signs did not hold as expected"));
                return;
            }
            Player.Interact();
            if (Player.GetHeldBody() || !Snap(Upper, *Lower.GetBody()))
            {
                Fail(TEXT("clamped object could be picked up or power could not be restored"));
                return;
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("CaptureDoorClamp")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("Door-Clamped-Object"), false, false);
            }
            UE_LOG(LogDoorClampVerification, Display, TEXT("Door retained the object, rejected pickup and showed both warning signs."));
            Step(EDoorClampTest::Release);
        }
        break;
    case EDoorClampTest::Release:
        if (!DoorClamp.HasObject())
        {
            if (!Body.IsSimulatingPhysics() || !FMath::IsNearlyEqual(Body.GetMass(), TestObject.GetMassKg(), 0.05f) ||
                !SignsVisible(false) || !Abandoned::Domain::Puzzles::CanReleaseDoorClamp(
                    true, Door->Bounds.GetBox().Min.Z, Body.Bounds.GetBox().Max.Z))
            {
                Fail(TEXT("opening did not restore physics after the required clearance"));
                return;
            }
            AimAtObject();
            Player.Interact();
            if (Player.GetHeldBody() != &Body)
            {
                Fail(TEXT("released object could not be picked up"));
                return;
            }
            Player.ReleaseHeldObject();
            UE_LOG(LogDoorClampVerification, Display, TEXT("Door released the object with its original mass and pickup restored."));
            Step(EDoorClampTest::Reopen);
        }
        break;
    case EDoorClampTest::Reopen:
        if (IsDoorOpen())
        {
            Obstruct();
            Step(EDoorClampTest::ClampForReset);
        }
        break;
    case EDoorClampTest::ClampForReset:
        if (DoorClamp.Holds(&Body))
        {
            Player.ResetChamber();
            if (DoorClamp.HasObject() || !Body.IsSimulatingPhysics() || !SignsVisible(false) ||
                !Body.GetComponentLocation().Equals(Layout->CubeSpawns[2].Center, 0.1) ||
                !Door->GetComponentLocation().Equals(DoorCenter, 0.1) ||
                !SupplyDoor())
            {
                Fail(TEXT("reset did not restore a usable object and closed door"));
                return;
            }
            UE_LOG(LogDoorClampVerification, Display, TEXT("Chamber reset cleared the clamp, restored the cube and hid its signs."));
            Step(EDoorClampTest::OpenForPlayer);
        }
        break;
    case EDoorClampTest::OpenForPlayer:
        if (IsDoorOpen())
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(DoorCenter.X, DoorCenter.Y, Layout->Spawn.Z)));
            ReturnCube(Player, 1);
            Step(EDoorClampTest::CrushPlayer);
        }
        break;
    case EDoorClampTest::CrushPlayer:
        if (!IsButtonLatched() && Player.GetActorLocation().Equals(Layout->Spawn, 3.0) && !DoorClamp.HasObject())
        {
            UE_LOG(LogDoorClampVerification, Display, TEXT("Door clamp verification passed: capture, release, reset and player checkpoint."));
            bVerifyDoorClamp = false;
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif
