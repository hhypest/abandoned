#include "Verification/PlateBalanceVerification.h"
#include "Verification/PassageAirlockVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Infrastructure/Puzzles/CubePlacementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PuzzleCube.h"
#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeightBalanceTest, Log, All);

namespace
{
bool HasLoads(const ATrainingChamberGameMode& Chamber, float A, float B, float C)
{
    return FMath::IsNearlyEqual(Chamber.GetMeasuredMass(0), A, 0.01f) &&
        FMath::IsNearlyEqual(Chamber.GetMeasuredMass(1), B, 0.01f) && FMath::IsNearlyEqual(Chamber.GetMeasuredMass(2), C, 0.01f);
}

bool Snap(APuzzleCube& Cube, UBoxComponent& Surface)
{
    const double Z = Surface.GetScaledBoxExtent().Z + Cube.GetHalfSize() + 0.5;
    return Cube.SnapTo(Surface, Surface.GetComponentLocation() + FVector(0, 0, Z), FRotator::ZeroRotator);
}
}
#endif

UPlateBalanceVerification::UPlateBalanceVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
bool UPlateBalanceVerification::IsStandaloneRequested()
{
    return FParse::Param(FCommandLine::Get(), TEXT("VerifyBalanceChamber"));
}

void UPlateBalanceVerification::Attach(ATrainingChamberGameMode& Chamber, const TArray<TObjectPtr<APuzzleCube>>& Ballast,
    const TArray<FTrainingPressurePlate>& Plates)
{
    auto* Scenario = NewObject<UPlateBalanceVerification>(&Chamber);
    if (Ballast.Num() != 6 || Plates.Num() != 3 || !Chamber.GetLayout() || Chamber.GetLayout()->CubeSpawns.Num() != 6)
    {
        Scenario->Fail(TEXT("invalid balance fixture"));
        return;
    }
    for (APuzzleCube* Cube : Ballast)
    {
        Scenario->Cubes.Add(Cube);
    }
    for (const auto& Plate : Plates)
    {
        Scenario->Displays.Add(FPlateDisplay{Plate.Visual.Get(), Plate.Indicator.Get(), Plate.RestLocation});
    }
    Chamber.AddInstanceComponent(Scenario);
    Scenario->RegisterComponent();
    Scenario->AddTickPrerequisiteActor(&Chamber);
    Scenario->SetComponentTickEnabled(true);
}

void UPlateBalanceVerification::Step(EPhase Next)
{
    Phase = Next;
    PhaseSeconds = 0.0;
}

void UPlateBalanceVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogWeightBalanceTest, Error, TEXT("Balance test failed: %s (phase %d)."), Reason, static_cast<int32>(Phase));
    SetComponentTickEnabled(false);
    Input.Release();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UPlateBalanceVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    Input.Release();
    Super::EndPlay(Reason);
}

bool UPlateBalanceVerification::IsReset(const ATrainingChamberGameMode& Chamber) const
{
    const auto& Layout = *Chamber.GetLayout();
    if (!HasLoads(Chamber, 0, 0, 0) || Chamber.IsExitEnabled() || !Chamber.GetDoor()->GetComponentLocation().Equals(Layout.Door.Center))
    {
        return false;
    }
    for (int32 Index = 0; Index < Displays.Num(); ++Index)
    {
        const auto& Display = Displays[Index];
        if (Chamber.IsPlatePressed(Index) || !Display.Visual.IsValid() || !Display.Visual->GetRelativeLocation().Equals(Display.RestLocation, 0.01) ||
            !Display.Indicator.IsValid() || !FMath::IsNearlyEqual(Display.Indicator->Intensity, Layout.PlateLight.Intensity))
        {
            return false;
        }
    }
    return true;
}

void UPlateBalanceVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    PhaseSeconds += DeltaSeconds;
    if (PhaseSeconds > 8.0)
    {
        Fail(TEXT("timed out waiting for the player, physics or exit"));
        return;
    }
    if (Input.Release())
    {
        return;
    }
    auto* Chamber = Cast<ATrainingChamberGameMode>(GetOwner());
    const auto* Layout = Chamber ? Chamber->GetLayout() : nullptr;
    if (!Layout || !IsValid(Chamber->GetDoor()) || !IsValid(Chamber->GetPlate(0)) || !IsValid(Chamber->GetPlate(1)) ||
        !IsValid(Chamber->GetPlate(2)) || Cubes.ContainsByPredicate([](const auto& Cube) { return !Cube.IsValid(); }))
    {
        Fail(TEXT("balance fixture disappeared"));
        return;
    }
    if (Chamber->HasReachedExit())
    {
        if (Phase != EPhase::WalkToExit)
        {
            Fail(TEXT("exit reached before both solutions and reset"));
            return;
        }
        SetComponentTickEnabled(false);
        if (IsStandaloneRequested())
        {
            UE_LOG(LogWeightBalanceTest, Display, TEXT("Balance chamber verification passed: three plates, two solutions, reset and exit."));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        else
        {
            UE_LOG(LogWeightBalanceTest, Display, TEXT("Entry course verification passed: balance released the passage."));
        }
        return;
    }
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player)
    {
        return;
    }
    const auto PlacePlayer = [&](const FVector& Position, const FRotator& View)
    {
        Player->SetActorLocationAndRotation(Position, FRotator(0, View.Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Controller->SetControlRotation(View);
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    };
    const auto AimFrom = [&](double X, double Y, const FVector& Target)
    {
        const FVector Position(X, Y, Layout->Spawn.Z);
        PlacePlayer(Position, (Target - Position - FVector(0, 0, Abandoned::MovementProfile::CameraHeight)).Rotation());
    };
    const auto PickUp = [&](const APuzzleCube& Cube)
    {
        const FVector Target = Cube.GetActorLocation();
        AimFrom(Target.X - 140.0, Target.Y, Target);
        Input.Press(*Controller, EKeys::E);
    };
    auto& A = *Chamber->GetPlate(0);
    auto& B = *Chamber->GetPlate(1);
    auto& C = *Chamber->GetPlate(2);
    namespace Airlock = Abandoned::Verification::PassageAirlock;
    switch (Phase)
    {
    case EPhase::Empty:
        if (FParse::Param(FCommandLine::Get(), TEXT("CapturePassageDoor")) && PhaseSeconds < 1.0)
        {
            PlacePlayer(FVector(Layout->Door.Center.X - 310.0, Layout->Door.Center.Y, Layout->Spawn.Z), FRotator::ZeroRotator);
            if (PhaseSeconds > 0.55 && !bCapturedDoor)
            {
                FScreenshotRequest::RequestScreenshot(TEXT("Balance-Door-Facing"), false, false);
                bCapturedDoor = true;
            }
            break;
        }
        if (PhaseSeconds >= 0.35)
        {
            if (Airlock::Requested() && Airlock::ReturnedToBalance(*Player)) { SetComponentTickEnabled(false); return; }
            if (!IsReset(*Chamber))
            {
                Fail(TEXT("an empty chamber must stay closed"));
                return;
            }
            for (int32 Index = 0; Index < 3; ++Index)
            {
                auto& Cube = *Cubes[Index + 2];
                const auto& Plate = *Chamber->GetPlate(Index);
                Cube.ResetPosition(Plate.GetComponentLocation() + FVector(0, 0, Plate.GetScaledBoxExtent().Z + Cube.GetHalfSize() + 1.0));
            }
            Step(EPhase::EqualUnderload);
        }
        break;
    case EPhase::EqualUnderload:
        if (PhaseSeconds >= 0.35)
        {
            if (!HasLoads(*Chamber, 5, 5, 5))
            {
                Fail(TEXT("three small cubes must register 5 kg on each plate"));
                return;
            }
            if (Chamber->IsExitEnabled())
            {
                Fail(TEXT("15 kg distributed evenly must not open the door"));
                return;
            }
            Input.Press(*Controller, EKeys::R);
            Step(EPhase::SetupUneven);
        }
        break;
    case EPhase::SetupUneven:
        if (!IsReset(*Chamber) || !Snap(*Cubes[0], A) || !Snap(*Cubes[2], *Cubes[0]->GetBody()) ||
            !Snap(*Cubes[3], *Cubes[2]->GetBody()) || !Snap(*Cubes[1], B))
        {
            Fail(TEXT("could not set up 20/10/0 after reset"));
            return;
        }
        Step(EPhase::Uneven);
        break;
    case EPhase::Uneven:
        if (PhaseSeconds >= 0.35)
        {
            if (!HasLoads(*Chamber, 20, 10, 0) || Chamber->IsExitEnabled())
            {
                Fail(TEXT("30 kg distributed unevenly must not open the door"));
                return;
            }
            Input.Press(*Controller, EKeys::R);
            Step(EPhase::SetupFirst);
        }
        break;
    case EPhase::SetupFirst:
    {
        if (!IsReset(*Chamber) || !Snap(*Cubes[0], A) || !Snap(*Cubes[1], B))
        {
            Fail(TEXT("could not place the two large cubes after reset"));
            return;
        }
        const FVector Center = C.GetComponentLocation();
        Cubes[2]->ResetPosition(FVector(Center.X - 230.0, Center.Y, 29.0));
        PickUp(*Cubes[2]);
        Step(EPhase::AimAtPlate);
        break;
    }
    case EPhase::AimAtPlate:
    {
        if (Player->GetHeldCube() != Cubes[2].Get())
        {
            Fail(TEXT("could not pick up the cube for the third plate"));
            return;
        }
        const FVector Target = C.GetComponentLocation() + FVector(0, 0, C.GetScaledBoxExtent().Z + 67.5);
        const double Height = Target.Z - Layout->Spawn.Z - Abandoned::MovementProfile::CameraHeight;
        AimFrom(Target.X - FMath::Sqrt(FMath::Square(APuzzleCharacter::HoldDistanceCm) - FMath::Square(Height)), Target.Y, Target);
        Step(EPhase::PlaceOnPlate);
        break;
    }
    case EPhase::PlaceOnPlate:
        if (Player->GetPlacementPreview().GetPreviewSupport() == &C && !Player->GetPlacementPreview().IsPreviewBlocked() &&
            Cubes[2]->GetBody()->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(28.0))
        {
            Input.Press(*Controller, EKeys::E);
            Step(EPhase::StackOnPlate);
        }
        break;
    case EPhase::StackOnPlate:
        if (Cubes[2]->GetSupport() != &C || !Snap(*Cubes[3], *Cubes[2]->GetBody()))
        {
            Fail(TEXT("the placement interaction did not snap onto plate 3"));
            return;
        }
        Step(EPhase::FirstSolution);
        break;
    case EPhase::FirstSolution:
        if (Airlock::Requested() && !bRevealedPassage && Chamber->IsExitEnabled() && PhaseSeconds >= 0.5)
        {
            if (!Chamber->GetDoor()->GetComponentLocation().Equals(Layout->Door.Center) || !Airlock::SlotIsBalance())
            {
                Fail(TEXT("Balance opened or saved before passage readiness"));
                return;
            }
            bRevealedPassage = Chamber->RevealChamberForVerification();
            if (bRevealedPassage) { UE_LOG(LogWeightBalanceTest, Display, TEXT("Solved Balance held closed while passage hidden.")); }
        }
        if (bRevealedPassage && Chamber->HasChamberLoadFailed() && FParse::Param(FCommandLine::Get(), TEXT("VerifyInvalidPassage")))
        {
            if (!Chamber->IsExitEnabled() || !Chamber->GetDoor()->GetComponentLocation().Equals(Layout->Door.Center) || !Airlock::SlotIsBalance())
            {
                Fail(TEXT("invalid passage changed the Balance gate or checkpoint"));
                return;
            }
            UE_LOG(LogWeightBalanceTest, Display, TEXT("Invalid passage rejected: Balance gate closed, player and checkpoint retained."));
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
            return;
        }
        if (Chamber->IsDoorOpen())
        {
            if (!HasLoads(*Chamber, 10, 10, 10))
            {
                Fail(TEXT("first solution opened without balanced loads"));
                return;
            }
            UE_LOG(LogWeightBalanceTest, Display, TEXT("First solution opened the door."));
            const FVector Base = Cubes[1]->GetActorLocation();
            Cubes[4]->ResetPosition(FVector(Base.X - 200, Base.Y, 29.0));
            PickUp(*Cubes[4]);
            Step(EPhase::CarryOntoStack);
        }
        break;
    case EPhase::CarryOntoStack:
        if (Player->GetHeldCube() != Cubes[4].Get())
        {
            Fail(TEXT("could not pick up the 5 kg cube for the overload test"));
            return;
        }
        PlacePlayer(Cubes[1]->GetActorLocation() + FVector(0, 0,
            Cubes[1]->GetHalfSize() + Abandoned::MovementProfile::StandingHalfHeight + 1.0), FRotator(-14, 0, 0));
        Step(EPhase::Overload);
        break;
    case EPhase::Overload:
        if (HasLoads(*Chamber, 10, 10 + Layout->PlayerMassKg + 5, 10))
        {
            if (Chamber->IsExitEnabled())
            {
                Fail(TEXT("player weight and carried mass must break the balance immediately"));
                return;
            }
            if (Chamber->GetDoor()->GetComponentLocation().Equals(Layout->Door.Center, 1.0))
            {
                Player->ReleaseHeldObject();
                Cubes[4]->ResetPosition(Layout->CubeSpawns[4].Center);
                PlacePlayer(Layout->Spawn, FRotator::ZeroRotator);
                Step(EPhase::Reopened);
            }
        }
        break;
    case EPhase::Reopened:
        if (Chamber->IsDoorOpen())
        {
            Input.Press(*Controller, EKeys::R);
            Step(EPhase::AfterReset);
        }
        break;
    case EPhase::AfterReset:
        if (!IsReset(*Chamber))
        {
            Fail(TEXT("reset left a pressure plate, indicator or door active"));
            return;
        }
        if (Player->GetHeldCube() || !Player->GetActorLocation().Equals(Layout->Spawn, 3.0))
        {
            Fail(TEXT("reset did not return the player to the start with empty hands"));
            return;
        }
        for (int32 Index = 0; Index < Cubes.Num(); ++Index)
        {
            if (Cubes[Index]->GetSupport() || !Cubes[Index]->GetActorLocation().Equals(Layout->CubeSpawns[Index].Center, 3.0))
            {
                Fail(TEXT("reset did not restore all six cubes to their spawn positions"));
                return;
            }
        }
        if (!Snap(*Cubes[0], A) || !Snap(*Cubes[2], B) || !Snap(*Cubes[3], *Cubes[2]->GetBody()) ||
            !Snap(*Cubes[4], C) || !Snap(*Cubes[5], *Cubes[4]->GetBody()))
        {
            Fail(TEXT("could not set up the alternate solution"));
            return;
        }
        Step(EPhase::SecondSolution);
        break;
    case EPhase::SecondSolution:
        if (bRevealedPassage && Chamber->HasChamberLoadFailed() && FParse::Param(FCommandLine::Get(), TEXT("VerifyInvalidPassage")))
        {
            if (!Chamber->IsExitEnabled() || !Chamber->GetDoor()->GetComponentLocation().Equals(Layout->Door.Center) || !Airlock::SlotIsBalance())
            {
                Fail(TEXT("invalid passage changed the Balance gate or checkpoint"));
                return;
            }
            UE_LOG(LogWeightBalanceTest, Display, TEXT("Invalid passage rejected: Balance gate closed, player and checkpoint retained."));
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
            return;
        }
        if (Chamber->IsDoorOpen())
        {
            if (!HasLoads(*Chamber, 10, 10, 10))
            {
                Fail(TEXT("alternate solution opened without balanced loads"));
                return;
            }
            UE_LOG(LogWeightBalanceTest, Display, TEXT("Alternate arrangement opened the door."));
            PlacePlayer(Layout->Spawn + FVector(195.0, 0, 0), FRotator(-6, 0, 0));
            Step(EPhase::Capture);
        }
        break;
    case EPhase::Capture:
        if (PhaseSeconds >= 0.15)
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("CaptureBalanceChamber")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("WeightBalance"), false, false);
            }
            Step(EPhase::ApproachExit);
        }
        break;
    case EPhase::ApproachExit:
        if (Airlock::Requested())
        {
            if (!bRevealedPassage || !Chamber->IsDoorOpen() || !Airlock::SlotIsBalance()) { Fail(TEXT("invalid passage departure")); return; }
            Airlock::RecordDeparture(*Player);
        }
        PlacePlayer(FVector(Layout->Door.Center.X - 180.0, Layout->Door.Center.Y, Layout->Spawn.Z), FRotator::ZeroRotator);
        Step(EPhase::WalkToExit);
        break;
    case EPhase::WalkToExit:
        Player->AddMovementInput(FVector::ForwardVector);
        break;
    }
}
#endif
