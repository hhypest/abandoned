#include "Verification/DoorClampVerification.h"

#include "TrainingChamber/TrainingChamber.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Puzzles/PuzzleCube.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDoorClampVerification, Log, All);

namespace
{
bool CheckWeightShell(UWorld& World)
{
    // Probe exposed faces in world coordinates so both assembly modes use the same oracle.
    const FCollisionObjectQueryParams Walls(ECC_WorldStatic);
    struct FFace { FVector Point; FVector Normal; };
    const FFace Faces[] = {
        {{350, 100, 0}, FVector::UpVector}, {{350, 100, 560}, FVector::DownVector},
        {{-200, 160, 250}, FVector::ForwardVector}, {{500, 600, 250}, -FVector::RightVector},
        {{500, -600, 250}, FVector::RightVector}, {{1175, -350, 250}, -FVector::ForwardVector},
        {{1175, 350, 250}, -FVector::ForwardVector}, {{1175, 0, 550}, -FVector::ForwardVector},
        {{1460, -100, 250}, FVector::RightVector}, {{1460, 100, 250}, -FVector::RightVector},
        {{1675, 0, 250}, -FVector::ForwardVector}
    };
    for (const auto& Face : Faces)
    {
        FHitResult Hit;
        if (!World.LineTraceSingleByObjectType(Hit, Face.Point + Face.Normal * 16, Face.Point - Face.Normal * 8, Walls)
            || !Hit.ImpactPoint.Equals(Face.Point, 0.5) || !Hit.ImpactNormal.Equals(Face.Normal, 0.01))
        {
            UE_LOG(LogDoorClampVerification, Error, TEXT("Incorrect Weight surface at %s"), *Face.Point.ToString());
            return false;
        }
    }
    FHitResult Hit;
    const bool bCorridorBlocked = World.SweepSingleByObjectType(Hit, FVector(1120, 0, 92), FVector(1620, 0, 92),
        FQuat::Identity, Walls, FCollisionShape::MakeCapsule(30, 90));
    const bool bBeyondFloorBlocked = World.LineTraceSingleByObjectType(Hit, FVector(1800, 400, 50),
        FVector(1800, 400, -250), Walls);
    return !bCorridorBlocked && !bBeyondFloorBlocked;
}
}
#endif

bool Abandoned::Verification::WantsDoorClampVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    return FParse::Param(FCommandLine::Get(), TEXT("VerifyDoorClamp"));
#else
    return false;
#endif
}

UDoorClampVerification::UDoorClampVerification()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UDoorClampVerification::Attach(ATrainingChamberGameMode& Chamber)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!Abandoned::Verification::WantsDoorClampVerification())
    {
        return;
    }
    auto* Verification = NewObject<UDoorClampVerification>(&Chamber);
    if (!CheckWeightShell(*Chamber.GetWorld()))
    {
        Verification->Fail(TEXT("Weight shell faces, capsule passage or floor edge"));
        return;
    }
    UE_LOG(LogDoorClampVerification, Display, TEXT("Weight shell verified: 11 faces, clear capsule passage and floor edge."));
    const UTrainingChamberLayout* Layout = Chamber.GetLayout();
    if (!Layout || Layout->CubeSpawns.Num() < 3)
    {
        Verification->Fail(TEXT("missing cube layout"));
        return;
    }
    // Match the fixture before the first physics tick can move its actors.
    for (TActorIterator<APuzzleCube> It(Chamber.GetWorld()); It; ++It)
    {
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (It->GetActorLocation().Equals(Layout->CubeSpawns[Index].Center, 0.1))
            {
                Verification->Cubes[Index] = *It;
            }
        }
    }
    Verification->bCaptureVisibility = FParse::Param(FCommandLine::Get(), TEXT("CaptureWeightVisibility"));
    if (Verification->bCaptureVisibility) { Verification->Step(EPhase::TurnCapture); }
    Chamber.AddInstanceComponent(Verification);
    Verification->RegisterComponent();
    Verification->AddTickPrerequisiteActor(&Chamber);
    Verification->SetComponentTickEnabled(true);
#endif
}

void UDoorClampVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
#if WITH_DEV_AUTOMATION_TESTS
    TotalSeconds += DeltaSeconds;
    auto* Chamber = Cast<ATrainingChamberGameMode>(GetOwner());
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Chamber || !Player || !Chamber->GetDoor())
    {
        if (TotalSeconds > 4.0)
        {
            Fail(TEXT("missing chamber or player"));
        }
        return;
    }
    Verify(*Chamber, *Player, DeltaSeconds);
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UDoorClampVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogDoorClampVerification, Error, TEXT("Door clamp verification failed at step %d: %s."),
        static_cast<int>(Phase), Reason);
    SetComponentTickEnabled(false);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UDoorClampVerification::Step(EPhase Next)
{
    Phase = Next;
    PhaseSeconds = 0.0;
}

void UDoorClampVerification::Verify(ATrainingChamberGameMode& Chamber, ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    PhaseSeconds += DeltaSeconds;
    const UTrainingChamberLayout* Layout = Chamber.GetLayout();
    if (PhaseSeconds > 4.0 || !Layout || !Cubes[0].IsValid() || !Cubes[1].IsValid() || !Cubes[2].IsValid())
    {
        Fail(TEXT("fixture missing or timed out"));
        return;
    }
    APuzzleCube& Lower = *Cubes[0];
    APuzzleCube& Upper = *Cubes[1];
    APuzzleCube& TestObject = *Cubes[2];
    UBoxComponent& Body = *TestObject.GetBody();
    UBoxComponent& Door = *Chamber.GetDoor();
    const FVector DoorCenter = Layout->Door.Center;
    const auto Snap = [](APuzzleCube& Cube, UBoxComponent& Surface)
    {
        const double Height = Surface.GetScaledBoxExtent().Z + Cube.GetHalfSize() + 0.5;
        return Cube.SnapTo(Surface, Surface.GetComponentLocation() + FVector(0, 0, Height), FRotator::ZeroRotator);
    };
    const auto SupplyDoor = [&]()
    {
        Chamber.PressButton();
        return Snap(Lower, *Chamber.GetPlate()) && Snap(Upper, *Lower.GetBody());
    };
    const auto PlacePlayer = [&](const FVector& Position, const FRotator& View)
    {
        Player.SetActorLocationAndRotation(Position, FRotator(0, View.Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Player.GetController()->SetControlRotation(View);
        Player.GetCharacterMovement()->StopMovementImmediately();
        Player.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    };
    const auto AimAtObject = [&]()
    {
        const FVector Target = TestObject.GetActorLocation();
        const FVector Position(Target.X - 145.0, Target.Y, Layout->Spawn.Z);
        const UCameraComponent* Camera = Player.FindComponentByClass<UCameraComponent>();
        const FVector Eye = Position + Camera->GetComponentLocation() - Player.GetActorLocation();
        PlacePlayer(Position, (Target - Eye).Rotation());
    };
    const auto Input = [&](const FKey& Key, EInputEvent Event)
    {
        auto* Controller = CastChecked<APlayerController>(Player.GetController());
        Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, Event, FPlatformTime::Cycles64()));
    };
    const auto SignsVisible = [&](bool Visible)
    {
        TArray<UTextRenderComponent*> Labels;
        Door.GetOwner()->GetComponents<UTextRenderComponent>(Labels);
        return Labels.Num() == 2 && !Labels.ContainsByPredicate([Visible](const UTextRenderComponent* Label)
        {
            return Label->IsVisible() != Visible;
        });
    };
    const auto HoldsObject = [&]()
    {
        return Chamber.HasClampedObject() && Abandoned::Infrastructure::Puzzles::IsDoorClamped(&Body);
    };
    const auto Obstruct = [&]()
    {
        TestObject.ResetPosition(FVector(DoorCenter.X, DoorCenter.Y,
            DoorCenter.Z - Layout->Door.Size.Z * 0.5 + TestObject.GetHalfSize() + 1.0));
        Upper.ResetPosition(Layout->CubeSpawns[1].Center);
    };
    switch (Phase)
    {
    case EPhase::TurnCapture:
        if (CaptureFrame == 0)
        {
            PlacePlayer(Layout->Spawn + FVector(160, 0, 0), FRotator(0, 180, 0));
        }
        // A snap can invalidate all query history. Also sweep back at 15 degrees
        // per frame after the opposite wall has occluded the puzzle for 24 frames.
        if (CaptureFrame == 60 || (CaptureFrame >= 90 && CaptureFrame <= 101) ||
            (CaptureFrame >= 126 && CaptureFrame <= 137))
        {
            const double Yaw = CaptureFrame == 60 ? 0.0 :
                15.0 * (CaptureFrame <= 101 ? CaptureFrame - 89 : 137 - CaptureFrame);
            const FRotator View(-8, Yaw, 0);
            Player.GetController()->SetControlRotation(View);
            Player.FaceRotation(View, DeltaSeconds);
        }
        if (CaptureFrame == 59 || (CaptureFrame >= 60 && CaptureFrame <= 64) || CaptureFrame == 85 ||
            (CaptureFrame >= 126 && CaptureFrame <= 140))
        {
            FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Weight-Turn-%03d"), CaptureFrame), false, false);
        }
        if (++CaptureFrame == 150)
        {
            PlacePlayer(FVector(DoorCenter.X - 410, DoorCenter.Y, Layout->Spawn.Z), FRotator(-4, 0, 0));
            CaptureFrame = 0;
            Step(EPhase::PowerOn);
        }
        break;
    case EPhase::PowerOn:
        if (PhaseSeconds >= 0.25)
        {
            if (!SupplyDoor())
            {
                Fail(TEXT("could not load the pressure plate"));
                return;
            }
            Step(EPhase::Open);
        }
        break;
    case EPhase::Open:
        if (bCaptureVisibility)
        {
            FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Weight-Opening-%03d"), CaptureFrame++), false, false);
        }
        if (Chamber.IsDoorOpen())
        {
            Obstruct();
            Step(EPhase::Clamp);
        }
        break;
    case EPhase::Clamp:
        if (HoldsObject())
        {
            ClampedObjectPosition = Body.GetComponentLocation();
            ObstructedDoorPosition = Door.GetComponentLocation();
            AimAtObject();
            Step(EPhase::Hold);
        }
        break;
    case EPhase::Hold:
        if (PhaseSeconds >= 0.35)
        {
            if (!HoldsObject() || Body.IsSimulatingPhysics() || !SignsVisible(true) ||
                !Body.GetComponentLocation().Equals(ClampedObjectPosition, 0.1) ||
                !Door.GetComponentLocation().Equals(ObstructedDoorPosition, 0.1))
            {
                Fail(TEXT("the object, door or signs did not hold as expected"));
                return;
            }
            Input(EKeys::E, IE_Pressed);
            Step(EPhase::ReleaseBlockedInput);
        }
        break;
    case EPhase::ReleaseBlockedInput:
        Input(EKeys::E, IE_Released);
        Step(EPhase::CheckBlockedPickup);
        break;
    case EPhase::CheckBlockedPickup:
        if (Player.GetHeldCube() || !Snap(Upper, *Lower.GetBody()))
        {
            Fail(TEXT("clamped object could be picked up or power could not be restored"));
            return;
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("CaptureDoorClamp")))
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Door-Clamped-Object"), false, false);
        }
        UE_LOG(LogDoorClampVerification, Display, TEXT("Door retained the object, rejected pickup and showed both warning signs."));
        Step(EPhase::Release);
        break;
    case EPhase::Release:
        if (!Chamber.HasClampedObject())
        {
            if (!Body.IsSimulatingPhysics() || !FMath::IsNearlyEqual(Body.GetMass(), TestObject.GetMassKg(), 0.05f) ||
                !SignsVisible(false) || !Abandoned::Domain::Puzzles::CanReleaseDoorClamp(
                    true, Door.Bounds.GetBox().Min.Z, Body.Bounds.GetBox().Max.Z))
            {
                Fail(TEXT("opening did not restore physics after the required clearance"));
                return;
            }
            AimAtObject();
            Input(EKeys::E, IE_Pressed);
            Step(EPhase::ReleasePickupInput);
        }
        break;
    case EPhase::ReleasePickupInput:
        Input(EKeys::E, IE_Released);
        Step(EPhase::CheckPickup);
        break;
    case EPhase::CheckPickup:
        if (Player.GetHeldCube() != &TestObject)
        {
            Fail(TEXT("released object could not be picked up"));
            return;
        }
        Player.ReleaseHeldObject();
        UE_LOG(LogDoorClampVerification, Display, TEXT("Door released the object with its original mass and pickup restored."));
        Step(EPhase::Reopen);
        break;
    case EPhase::Reopen:
        if (Chamber.IsDoorOpen())
        {
            Obstruct();
            Step(EPhase::ClampForReset);
        }
        break;
    case EPhase::ClampForReset:
        if (HoldsObject())
        {
            Input(EKeys::R, IE_Pressed);
            Step(EPhase::ReleaseResetInput);
        }
        break;
    case EPhase::ReleaseResetInput:
        Input(EKeys::R, IE_Released);
        Step(EPhase::CheckReset);
        break;
    case EPhase::CheckReset:
        if (Chamber.HasClampedObject() || !Body.IsSimulatingPhysics() || !SignsVisible(false) ||
            !Body.GetComponentLocation().Equals(Layout->CubeSpawns[2].Center, 3.0) ||
            !Door.GetComponentLocation().Equals(DoorCenter, 0.1) || !SupplyDoor())
        {
            Fail(TEXT("reset did not restore a usable object and closed door"));
            return;
        }
        UE_LOG(LogDoorClampVerification, Display, TEXT("Chamber reset cleared the clamp, restored the cube and hid its signs."));
        Step(EPhase::OpenForPlayer);
        break;
    case EPhase::OpenForPlayer:
        if (Chamber.IsDoorOpen())
        {
            PlacePlayer(FVector(DoorCenter.X, DoorCenter.Y, Layout->Spawn.Z), FRotator::ZeroRotator);
            Upper.ResetPosition(Layout->CubeSpawns[1].Center);
            Step(EPhase::CrushPlayer);
        }
        break;
    case EPhase::CrushPlayer:
        if (!Chamber.IsButtonLatched() && Player.GetActorLocation().Equals(Layout->Spawn, 3.0) && !Chamber.HasClampedObject())
        {
            UE_LOG(LogDoorClampVerification, Display, TEXT("Door clamp verification passed: capture, release, reset and player checkpoint."));
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif
