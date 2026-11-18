#include "Verification/TrainingVerification.h"
#include "Verification/VerificationPlayerPose.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Infrastructure/Puzzles/CubePlacementComponent.h"
#include "Infrastructure/Puzzles/PuzzleInteractionSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PuzzleCube.h"
#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrainingVerification, Log, All);

using Abandoned::Verification::PlacePlayer;
#endif

UTrainingVerification::UTrainingVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
bool UTrainingVerification::IsRequested()
{
    return FParse::Param(FCommandLine::Get(), TEXT("VerifyTrainingChamber"));
}

void UTrainingVerification::Attach(ATrainingChamberGameMode& Chamber, const TArray<TObjectPtr<APuzzleCube>>& Ballast,
    const TArray<FTrainingPressurePlate>& Plates)
{
    auto* Scenario = NewObject<UTrainingVerification>(&Chamber);
    const auto* Layout = Chamber.GetLayout();
    if (!Layout || Layout->CubeSpawns.Num() != 3 || Ballast.Num() != 3 || Plates.Num() != 1)
    {
        Scenario->Fail(TEXT("expected three default training cubes and one plate"));
        return;
    }
    for (int32 Index = 0; Index < Ballast.Num(); ++Index)
    {
        Scenario->Cubes[Index] = Ballast[Index].Get();
    }
    Scenario->PlateVisual = Plates[0].Visual.Get();
    Scenario->PlateIndicator = Plates[0].Indicator.Get();
    Chamber.AddInstanceComponent(Scenario);
    Scenario->RegisterComponent();
    Scenario->AddTickPrerequisiteActor(&Chamber);
    Scenario->SetComponentTickEnabled(true);
}

void UTrainingVerification::Advance(EPhase Next, const TCHAR* Message)
{
    UE_LOG(LogTrainingVerification, Display, TEXT("Verification: %s"), Message);
    Elapsed = 0.0;
    Phase = Next;
}

void UTrainingVerification::StopMouseInput()
{
    if (auto* Controller = MouseOwner.Get())
    {
        Controller->ConsoleCommand(TEXT("Input.-key Mouse2D"), false);
    }
    MouseOwner.Reset();
}

void UTrainingVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogTrainingVerification, Error, TEXT("Training verification failed: %s (phase %d)."), Reason, static_cast<int32>(Phase));
    SetComponentTickEnabled(false);
    StopMouseInput();
    Input.Release();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UTrainingVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    StopMouseInput();
    Input.Release();
    Super::EndPlay(Reason);
}

void UTrainingVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    Elapsed += DeltaSeconds;
    if (Elapsed > 8.0)
    {
        Fail(TEXT("timed out waiting for the player or a physical transition"));
        return;
    }
    // Observe this input tick before gravity moves a freshly reset cube on the next frame.
    Input.Release();
    const auto* Chamber = Cast<ATrainingChamberGameMode>(GetOwner());
    if (!Chamber || !Chamber->GetLayout() || !IsValid(Chamber->GetPlate()) || !IsValid(Chamber->GetDoor()) ||
        !Cubes[0].IsValid() || !Cubes[1].IsValid() || !Cubes[2].IsValid() || !PlateVisual.IsValid() || !PlateIndicator.IsValid())
    {
        Fail(TEXT("training fixture disappeared"));
        return;
    }
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (Player)
    {
        Verify(*Chamber, *Player, *Controller);
    }
}

void UTrainingVerification::Verify(const ATrainingChamberGameMode& Chamber, ATrainingChamberCharacter& Player, APlayerController& Controller)
{
    const auto* Layout = Chamber.GetLayout();
    const auto* Door = Chamber.GetDoor();
    APuzzleCube& Small = *Cubes[0];
    APuzzleCube& Large = *Cubes[1];
    APuzzleCube& Step = *Cubes[2];
    UBoxComponent* Cube = Small.GetBody();

    const auto AimFrom = [&Player](FVector Here, FVector Target)
    {
        const FVector Eye = Here + FVector(0.0, 0.0, Abandoned::MovementProfile::CameraHeight);
        PlacePlayer(Player, FTransform((Target - Eye).Rotation(), Here));
    };
    const auto AimToHold = [Layout, &AimFrom](const FVector& Target, const FVector& Approach)
    {
        const double Height = Target.Z - (Layout->Spawn.Z + Abandoned::MovementProfile::CameraHeight);
        const double Forward = FMath::Sqrt(FMath::Max(0.0, FMath::Square(APuzzleCharacter::HoldDistanceCm) - FMath::Square(Height)));
        const FVector Here = Target - Approach * Forward;
        AimFrom(FVector(Here.X, Here.Y, Layout->Spawn.Z), Target);
    };
    const FVector SmallStart = Layout->CubeSpawns[0].Center;

    switch (Phase)
    {
    case EPhase::Initial:
        if (Elapsed >= 0.35f)
        {
            if (Chamber.IsPlatePressed() || Door->GetComponentLocation() != Layout->Door.Center)
            {
                Fail(TEXT("Door must start closed"));
                return;
            }
            if (Small.GetHalfSize() != 27.5 || Large.GetHalfSize() != 40.0 ||
                !FMath::IsNearlyEqual(Cube->GetMass(), 5.0f, 0.01f) ||
                !FMath::IsNearlyEqual(Large.GetBody()->GetMass(), 10.0f, 0.01f))
            {
                Fail(TEXT("Cube collision dimensions or Chaos masses are incorrect"));
                return;
            }
            PlacePlayer(Player, FTransform(FRotator::ZeroRotator, FVector(Layout->Plate.Center.X, Layout->Plate.Center.Y,
                Layout->GetPlateTop() + Abandoned::MovementProfile::StandingHalfHeight + 2.0)));
            Advance(EPhase::StandOnPlate, TEXT("Closed door confirmed. Standing on the plate."));
        }
        break;
    case EPhase::StandOnPlate:
        if (Chamber.IsPlatePressed())
        {
            if (Player.GetMovementBase() != Chamber.GetPlate() || !FMath::IsNearlyEqual(Chamber.GetMeasuredMass(), Layout->PlayerMassKg))
            {
                Fail(TEXT("The grounded player should be the only load on the plate"));
                return;
            }
            OnPlate = Player.GetActorLocation();
            Input.Press(Controller, EKeys::R);
            Advance(EPhase::CheckPlateReset, TEXT("Resetting with the player on the plate."));
        }
        break;
    case EPhase::CheckPlateReset:
        if (Chamber.IsPlatePressed() || Chamber.IsExitEnabled() || !PlateVisual->GetRelativeLocation().IsNearlyZero() ||
            !FMath::IsNearlyEqual(PlateIndicator->Intensity, Layout->PlateLight.Intensity))
        {
            Fail(TEXT("Reset left the pressure plate active"));
            return;
        }
        PlacePlayer(Player, FTransform(FRotator::ZeroRotator, OnPlate + FVector(0.0, 0.0, 2.0)));
        Advance(EPhase::StandAfterReset, TEXT("Reset released the loaded plate and restored its visual and indicator."));
        break;
    case EPhase::StandAfterReset:
        if (Chamber.IsPlatePressed())
        {
            Player.Jump();
            Advance(EPhase::JumpOffPlate, TEXT("The plate can be pressed again after reset."));
        }
        break;
    case EPhase::JumpOffPlate:
        if (Player.GetCharacterMovement()->IsFalling() && !Chamber.IsPlatePressed())
        {
            Player.StopJumping();
            PlacePlayer(Player, FTransform(FRotator::ZeroRotator, Layout->Spawn));
            MouseOwner = &Controller;
            Controller.ConsoleCommand(TEXT("Input.+key Mouse2D X=0 Y=1"), false);
            Advance(EPhase::LookUp, TEXT("Jumping unloaded the plate. Testing upward mouse movement."));
        }
        break;
    case EPhase::LookUp:
        if (Elapsed >= 0.15f)
        {
            if (Controller.GetControlRotation().GetNormalized().Pitch <= 0.0)
            {
                Fail(TEXT("Upward mouse movement did not raise the view"));
                return;
            }
            Controller.SetControlRotation(FRotator::ZeroRotator);
            Controller.ConsoleCommand(TEXT("Input.+key Mouse2D X=0 Y=-1"), false);
            Advance(EPhase::LookDown, TEXT("Upward mouse movement raised the view."));
        }
        break;
    case EPhase::LookDown:
        if (Elapsed >= 0.15f)
        {
            StopMouseInput();
            if (Controller.GetControlRotation().GetNormalized().Pitch >= 0.0)
            {
                Fail(TEXT("Downward mouse movement did not lower the view"));
                return;
            }
            UE_LOG(LogTrainingVerification, Display, TEXT("Verification: Downward mouse movement lowered the view."));
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Input.Press(Controller, EKeys::E);
            Advance(EPhase::Pickup, TEXT("Picking up the small cube."));
        }
        break;
    case EPhase::Pickup:
        {
            if (Player.GetHeldCube() != &Small)
            {
                Fail(TEXT("Pickup trace or physics handle missed the cube"));
                return;
            }
            const FVector Target(Layout->Plate.Center.X, Layout->Plate.Center.Y,
                Layout->GetPlateTop() + Small.GetHalfSize() + 42.0);
            AimToHold(Target, FVector::ForwardVector);
            Advance(EPhase::Carry, TEXT("Cube picked up with the gameplay trace and physics handle."));
        }
        break;
    case EPhase::Carry:
        if (Player.GetHeldCube() != &Small || Chamber.IsPlatePressed())
        {
            Fail(TEXT("The held cube must stay in the grip and leave the plate unpowered"));
            return;
        }
        if (FVector::DistSquared2D(Cube->GetComponentLocation(), Layout->Plate.Center) < FMath::Square(16.0) &&
            Cube->GetComponentLocation().Z > Layout->GetPlateTop() + Small.GetHalfSize() &&
            Cube->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(30.0))
        {
            if (!Player.GetPlacementPreview().IsVisible() || Player.GetPlacementPreview().IsPreviewBlocked() ||
                Player.GetPlacementPreview().GetPreviewSupport() != Chamber.GetPlate() ||
                Player.GetPlacementPreview().GetCollisionEnabled() != ECollisionEnabled::NoCollision)
            {
                Fail(TEXT("Expected a collision-free placement ghost on the plate"));
                return;
            }
            if (!bCapturedGhost && FParse::Param(FCommandLine::Get(), TEXT("CapturePlacementGhost")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("PlacementGhost"), false, false);
                bCapturedGhost = true;
                break;
            }
            ExpectedPlacement = Player.GetPlacementPreview().GetComponentLocation();
            Input.Press(Controller, EKeys::E);
            Advance(EPhase::CheckPlacement, TEXT("Placing the small cube on the plate."));
        }
        break;
    case EPhase::CheckPlacement:
        if (Small.GetSupport() != Chamber.GetPlate() || Cube->IsSimulatingPhysics() ||
            !Small.GetActorLocation().Equals(ExpectedPlacement, 0.1) || Player.GetPlacementPreview().IsVisible())
        {
            Fail(TEXT("Released cube did not snap to the plate"));
            return;
        }
        Advance(EPhase::Open, TEXT("Held cube snapped to the plate at the ghost position."));
        break;
    case EPhase::Open:
        if (Elapsed >= 0.5 && FMath::IsNearlyEqual(Chamber.GetMeasuredMass(), Small.GetMassKg()) && !Chamber.IsPlatePressed())
        {
            Fail(TEXT("settled 5 kg cube did not activate the plate"));
            return;
        }
        if (Chamber.IsPlatePressed() && Chamber.IsDoorOpen())
        {
            PlacePlayer(Player, FTransform(FRotator::ZeroRotator,
                FVector(Layout->Door.Center.X - 180.0, Layout->Door.Center.Y, Layout->Spawn.Z)));
            Advance(EPhase::Cross, TEXT("Settled cube pressed the plate and opened the door."));
        }
        break;
    case EPhase::Cross:
        if (!Chamber.HasReachedExit())
        {
            Player.AddMovementInput(FVector::ForwardVector);
            break;
        }
        {
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Input.Press(Controller, EKeys::E);
            Advance(EPhase::CheckDetachedPlate, TEXT("Removing the small cube from the plate."));
        }
        break;
    case EPhase::CheckDetachedPlate:
        if (Player.GetHeldCube() != &Small || Small.GetSupport())
        {
            Fail(TEXT("Could not remove the cube from the plate"));
            return;
        }
        Advance(EPhase::Close, TEXT("Player walked through the door and picked the cube off the plate."));
        break;
    case EPhase::Close:
        if (!Chamber.IsPlatePressed() && FMath::IsNearlyEqual(Door->GetComponentLocation().Z, Layout->Door.Center.Z, 0.1))
        {
            Controller.SetControlRotation(FRotator::ZeroRotator);
            Input.Press(Controller, EKeys::LeftMouseButton);
            Advance(EPhase::Throw, TEXT("Removing the cube closed the door. Testing left mouse throw."));
        }
        break;
    case EPhase::Throw:
        if (!Player.GetHeldCube())
        {
            if (!Cube->IsSimulatingPhysics() || Small.GetSupport() || Player.GetPlacementPreview().IsVisible() ||
                Cube->GetPhysicsLinearVelocity().X < GetDefault<UPuzzleInteractionSettings>()->GetThrowImpulse() / Small.GetMassKg() * 0.75)
            {
                Fail(TEXT("Left mouse did not throw the free cube forward"));
                return;
            }
            Input.Press(Controller, EKeys::R);
            Advance(EPhase::CheckThrowReset, TEXT("Left mouse threw the cube forward."));
        }
        break;
    case EPhase::CheckThrowReset:
        if (Chamber.HasReachedExit() || Player.GetHeldCube() || FVector::DistSquared(SmallStart, Cube->GetComponentLocation()) > 1.0)
        {
            Fail(TEXT("Reset did not restore the initial chamber state"));
            return;
        }
        Small.SetActorLocation(FVector(SmallStart.X, SmallStart.Y, -420.0), false, nullptr, ETeleportType::TeleportPhysics);
        Advance(EPhase::Return, TEXT("Reset restored player and cube."));
        break;
    case EPhase::Return:
        if (FVector::DistSquared(SmallStart, Cube->GetComponentLocation()) < FMath::Square(10.0))
        {
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Input.Press(Controller, EKeys::E);
            Advance(EPhase::PickupReturned, TEXT("Picking up the returned cube."));
        }
        break;
    case EPhase::PickupReturned:
        {
            if (Player.GetHeldCube() != &Small)
            {
                Fail(TEXT("Could not pick up the returned small cube"));
                return;
            }
            const FVector Target = Large.GetActorLocation() + FVector(0.0, 0.0,
                Large.GetHalfSize() + Small.GetHalfSize() + 40.0);
            AimToHold(Target, FVector::RightVector);
            Advance(EPhase::CarryToStack, TEXT("Lost cube returned. Carrying it to the large cube."));
        }
        break;
    case EPhase::CarryToStack:
        if (FVector::DistSquared2D(Small.GetActorLocation(), Large.GetActorLocation()) < FMath::Square(14.0) &&
            Cube->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(25.0))
        {
            if (!Player.GetPlacementPreview().IsVisible() || Player.GetPlacementPreview().IsPreviewBlocked() ||
                Player.GetPlacementPreview().GetPreviewSupport() != Large.GetBody())
            {
                Fail(TEXT("Expected a placement ghost on the stack"));
                return;
            }
            ExpectedPlacement = Player.GetPlacementPreview().GetComponentLocation();
            Input.Press(Controller, EKeys::E);
            Advance(EPhase::CheckStack, TEXT("Placing the small cube on the large cube."));
        }
        break;
    case EPhase::CheckStack:
        {
            if (Small.GetSupport() != Large.GetBody() || Large.GetStackMass() != 15.0f ||
                !Small.GetActorLocation().Equals(ExpectedPlacement, 1.0) || Player.GetPlacementPreview().IsVisible())
            {
                Fail(TEXT("Small cube did not snap to the large cube and add its mass"));
                return;
            }
            const FVector Start = Step.GetActorLocation() + FVector(-6.0, 0.0,
                Step.GetHalfSize() + Abandoned::MovementProfile::StandingHalfHeight + 1.0);
            PlacePlayer(Player, FTransform(FRotator::ZeroRotator, Start));
            Advance(EPhase::StartAscent, TEXT("Small cube snapped to large cube, total mass 15 kg."));
        }
        break;
    case EPhase::StartAscent:
        if (Player.GetMovementBase() == Step.GetBody())
        {
            Player.AddMovementInput(FVector::ForwardVector);
            if (Player.GetActorLocation().X >= Step.GetActorLocation().X + 7.0)
            {
                Player.Jump();
                Advance(EPhase::JumpToStack, TEXT("Player jumped toward the stack after a short run-up."));
            }
        }
        break;
    case EPhase::JumpToStack:
        Player.AddMovementInput(FVector::ForwardVector * 0.35);
        if (Player.GetMovementBase() == Cube)
        {
            Player.StopJumping();
            Player.Jump();
            Advance(EPhase::JumpToLedge, TEXT("Player landed on the stack and jumped toward the ledge."));
        }
        break;
    case EPhase::JumpToLedge:
        Player.AddMovementInput(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Large.GetActorLocation().X + 140.0 &&
            Player.GetCharacterMovement()->IsMovingOnGround() &&
            Player.GetActorLocation().Z > Small.GetActorLocation().Z + 145.0)
        {
            Advance(EPhase::RemoveSupport, TEXT("Player climbed from the stack onto the practice ledge."));
        }
        break;
    case EPhase::RemoveSupport:
        {
            const FVector Current = Large.GetActorLocation();
            AimFrom(FVector(Current.X, Current.Y - 155.0, Layout->Spawn.Z), Current);
            Input.Press(Controller, EKeys::E);
            Advance(EPhase::CheckDetachedStack, TEXT("Lifting the lower cube."));
        }
        break;
    case EPhase::CheckDetachedStack:
        if (Player.GetHeldCube() != &Large || Small.GetSupport() || !Cube->IsSimulatingPhysics())
        {
            Fail(TEXT("Picking up the lower block did not release its upper block"));
            return;
        }
        Input.Press(Controller, EKeys::R);
        Advance(EPhase::FinalReset, TEXT("Upper cube detached and resumed physics. Resetting the stand."));
        break;
    case EPhase::FinalReset:
        if (Player.GetHeldCube() || Player.GetPlacementPreview().IsVisible() || Chamber.HasReachedExit() ||
            Chamber.IsPlatePressed() || Chamber.IsExitEnabled() || !Door->GetComponentLocation().Equals(Layout->Door.Center, 0.1) ||
            !Player.GetActorLocation().Equals(Layout->Spawn, 5.0))
        {
            Fail(TEXT("Final reset did not restore player, grip, plate and exit"));
            return;
        }
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cubes); ++Index)
        {
            if (Cubes[Index]->GetSupport() || !Cubes[Index]->GetBody()->IsSimulatingPhysics() ||
                !Cubes[Index]->GetActorLocation().Equals(Layout->CubeSpawns[Index].Center, 1.0))
            {
                Fail(TEXT("Reset did not free and restore all cubes"));
                return;
            }
        }
        UE_LOG(LogTrainingVerification, Display,
            TEXT("Verification passed: player weight, placement ghost, throw, staircase, detach and reset."));
        SetComponentTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}
#endif
