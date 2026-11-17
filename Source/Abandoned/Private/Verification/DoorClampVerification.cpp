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
