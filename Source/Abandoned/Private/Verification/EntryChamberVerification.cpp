#include "Verification/EntryChamberVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Domain/Expedition/Dev20.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Expedition/Dev20Actor.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Infrastructure/Puzzles/CubePlacementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PuzzleCube.h"
#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "UnrealClient.h"
#include "Verification/VerificationPlayerPose.h"
#include "Verification/AirlockVerificationSession.h"

DEFINE_LOG_CATEGORY_STATIC(LogEntryChamberTest, Log, All);

namespace
{
bool SavedWithArchive(Abandoned::Domain::Expedition::Segment Expected)
{
    const auto Saved = Abandoned::Infrastructure::Expedition::UnrealCheckpointStore{}.Read();
    return Saved.Chamber == Expected && Saved.Archive.Size() == 1 &&
        Saved.Archive.At(0) == Abandoned::Domain::Expedition::SuitRecord::OxygenRestored;
}

bool EntryFlag(const TCHAR* Name)
{
    return FParse::Param(FCommandLine::Get(), Name);
}
}
#endif

UEntryChamberVerification::UEntryChamberVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
bool UEntryChamberVerification::UsesCheckpoint()
{
    return EntryFlag(TEXT("VerifyPassageAirlock")) || EntryFlag(TEXT("VerifyBalanceAirlock")) || EntryFlag(TEXT("VerifyEntryCheckpoint")) || EntryFlag(TEXT("VerifyButtonAirlock")) || EntryFlag(TEXT("VerifyWeightAirlock"));
}

bool UEntryChamberVerification::IsRequested(bool bDev20Route)
{
    return UsesCheckpoint() || EntryFlag(TEXT("VerifyEntryCourse")) || (bDev20Route && EntryFlag(TEXT("VerifyDev20")));
}

void UEntryChamberVerification::Attach(ATrainingChamberGameMode& Chamber, Abandoned::Domain::Expedition::Segment Current,
    const TArray<TObjectPtr<APuzzleCube>>& Ballast)
{
    using ESegment = Abandoned::Domain::Expedition::Segment;
    auto* Test = NewObject<UEntryChamberVerification>(&Chamber);
    Test->Segment = Current;
    Test->bDev20 = Chamber.IsDev20Run() && EntryFlag(TEXT("VerifyDev20"));
    Test->ExitPlateIndex = Chamber.GetLayout()->UsesParallelPlates() && EntryFlag(TEXT("VerifyEntryAlternatePlate")) ? 1 : 0;
    Test->bAlternateWeight = Current == ESegment::Weight && EntryFlag(TEXT("VerifyEntryAlternateWeight"));
    Test->UpperIndex = Test->bAlternateWeight ? 2 : 1;
    const int32 Required = Current == ESegment::Stairs ? 3 : Test->bAlternateWeight ? 4 : Current == ESegment::Weight ? 2 : 1;
    if (Ballast.Num() < Required)
    {
        Test->Fail(TEXT("missing cubes from the test layout"));
        return;
    }
    for (APuzzleCube* Cube : Ballast)
    {
        Test->Cubes.Add(Cube);
    }
    Chamber.AddInstanceComponent(Test);
    Test->RegisterComponent();
    Test->AddTickPrerequisiteActor(&Chamber);
    Test->SetComponentTickEnabled(true);
}

void UEntryChamberVerification::Advance(EPhase Next)
{
    Phase = Next;
    Elapsed = 0.0f;
}

void UEntryChamberVerification::Fail(const TCHAR* Reason)
{
    if (Phase == EPhase::PlaceFirst || Phase == EPhase::PlaceUpper)
    {
        const auto* Player = Cast<ATrainingChamberCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
        const int32 Index = Phase == EPhase::PlaceFirst ? 0 : UpperIndex;
        if (Player && Cubes.IsValidIndex(Index) && Cubes[Index].IsValid())
        {
            const APuzzleCube* Expected = Cubes[Index].Get();
            const auto& Preview = Player->GetPlacementPreview();
            UE_LOG(LogEntryChamberTest, Display,
                TEXT("Placement at failure: player %s, aim %s, expected cube %s at %s, held %s, speed %.1f cm/s, support %s, prompt %s."),
                *Player->GetActorLocation().ToCompactString(), *Player->GetViewRotation().ToCompactString(),
                *GetNameSafe(Expected), *Expected->GetActorLocation().ToCompactString(), *GetNameSafe(Player->GetHeldCube()),
                Expected->GetBody()->GetPhysicsLinearVelocity().Size(), *GetPathNameSafe(Preview.GetPreviewSupport()), Preview.PlacementPrompt());
        }
    }
    UE_LOG(LogEntryChamberTest, Error, TEXT("Entry test failed: %s (%s, phase %d)."),
        Reason, *Abandoned::Infrastructure::Expedition::WriteSegment(Segment), static_cast<int32>(Phase));
    SetComponentTickEnabled(false);
    Input.Release();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UEntryChamberVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    Input.Release();
    Super::EndPlay(Reason);
}

void UEntryChamberVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    Elapsed += DeltaSeconds;
    if (Elapsed > 9.0f)
    {
        Fail(TEXT("timed out waiting for gameplay or world travel"));
        return;
    }
    if (Input.Release())
    {
        return;
    }
    auto* Chamber = Cast<ATrainingChamberGameMode>(GetOwner());
    if (!Chamber || !Chamber->GetLayout() || !IsValid(Chamber->GetPlate(ExitPlateIndex)) || !IsValid(Chamber->GetDoor()) ||
        Cubes.ContainsByPredicate([](const auto& Cube) { return !Cube.IsValid(); }))
    {
        Fail(TEXT("chamber fixture disappeared"));
        return;
    }
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (Player)
    {
        Check(*Chamber, *Player, *Controller);
    }
}

void UEntryChamberVerification::Check(ATrainingChamberGameMode& Chamber, ATrainingChamberCharacter& Player, APlayerController& Controller)
{
    using ESegment = Abandoned::Domain::Expedition::Segment;
    using Abandoned::Domain::Expedition::Dev20MassKg;
    using Abandoned::Infrastructure::Expedition::WriteSegment;
    using Abandoned::Verification::PlacePlayer;
    const auto& Layout = *Chamber.GetLayout();
    const bool bStairs = Segment == ESegment::Stairs;
    const bool bWeight = Segment == ESegment::Weight;
    const bool bTestButton = EntryFlag(TEXT("VerifyButtonAirlock"));
    const bool bTestWeight = EntryFlag(TEXT("VerifyWeightAirlock"));
    const bool bTestBalance = EntryFlag(TEXT("VerifyBalanceAirlock"));
    auto* Session = UAirlockVerificationSession::Find(Chamber);
    if ((bTestButton || bTestWeight || bTestBalance) && !Session)
    {
        Fail(TEXT("airlock verification session unavailable"));
        return;
    }
    const bool bMissingPlate = bTestBalance && EntryFlag(TEXT("VerifyMissingBalancePlate"));
    const bool bTestDeparture = (bWeight && bTestBalance) || (bStairs && bTestButton) || (Segment == ESegment::Button && bTestWeight);
    if (bDev20 && Phase != EPhase::Start && !Unit.IsValid())
    {
        Fail(TEXT("DEV-20 disappeared from the chamber"));
        return;
    }
    if (Chamber.HasReachedExit())
    {
        const auto Expected = bDev20 ? EPhase::CarryToExit : bStairs ? EPhase::CrossLedge : EPhase::WalkToExit;
        if (Phase != Expected || (bDev20 && Player.GetHeldDev20() != Unit.Get()))
        {
            Fail(TEXT("exit reached before completing the chamber or retrieving DEV-20"));
            return;
        }
        SetComponentTickEnabled(false);
        UE_LOG(LogEntryChamberTest, Display, TEXT("Entry test: crossed %s exit."), *WriteSegment(Segment));
        if (bDev20 && bWeight)
        {
            UE_LOG(LogEntryChamberTest, Display, TEXT("DEV-20 verification passed: portal, Button, Weight and final exit."));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        return;
    }

    const auto AimFrom = [&Player](const FVector& From, const FVector& Target)
    {
        const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        PlacePlayer(Player, FTransform((Target - Eye).Rotation(), From));
    };
    const auto UseAt = [&](const FVector& Target, EPhase Next)
    {
        AimFrom(FVector(Target.X - 156.0, Target.Y, Layout.Spawn.Z), Target);
        Input.Press(Controller, EKeys::E);
        Advance(Next);
    };
    const auto AimHold = [&](const FVector& Target, const FVector& Approach)
    {
        const double Height = Target.Z - Layout.Spawn.Z - Abandoned::MovementProfile::CameraHeight;
        const double Forward = FMath::Sqrt(FMath::Max(0.0, FMath::Square(APuzzleCharacter::HoldDistanceCm) - FMath::Square(Height)));
        const FVector Start = Target - Approach * Forward;
        AimFrom(FVector(Start.X, Start.Y, Layout.Spawn.Z), Target);
    };
    const auto PlaceWhenAligned = [&](const APuzzleCube& Expected, const UBoxComponent* Support, EPhase Next)
    {
        if (Player.GetHeldCube() != &Expected)
        {
            Fail(TEXT("grip changed while waiting to place the cube"));
            return;
        }
        if (Player.GetPlacementPreview().GetPreviewSupport() == Support && !Player.GetPlacementPreview().IsPreviewBlocked() &&
            Expected.GetBody()->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(28.0))
        {
            Input.Press(Controller, EKeys::E);
            Advance(Next);
        }
    };

    if (bTestDeparture && ((bStairs && Phase == EPhase::RunUp) || Phase == EPhase::WaitDoor))
    {
        if (!Chamber.IsExitEnabled()) { return; }
        if (!bRevealedRoom)
        {
            if (!Chamber.GetDoor()->GetComponentLocation().Equals(Layout.Door.Center) || !SavedWithArchive(Segment))
            {
                Fail(TEXT("gate moved or checkpoint changed before streamed devices were ready"));
                return;
            }
            if (Elapsed >= 0.5f && Chamber.RevealChamberForVerification())
            {
                bRevealedRoom = true;
                UE_LOG(LogEntryChamberTest, Display, TEXT("Airlock visit %d: solved circuit held closed during load."), Session->Chamber.Visits + 1);
            }
            return;
        }
        if ((bMissingPlate || (bTestButton && EntryFlag(TEXT("VerifyIncompleteButton")))) && Chamber.HasChamberLoadFailed())
        {
            if (!Chamber.GetDoor()->GetComponentLocation().Equals(Layout.Door.Center) || !SavedWithArchive(Segment))
            {
                Fail(TEXT("incomplete chamber advanced the gate or checkpoint"));
                return;
            }
            if (bMissingPlate)
            {
                UE_LOG(LogEntryChamberTest, Display, TEXT("Missing Balance plate rejected: gate closed and Weight checkpoint preserved."));
            }
            else
            {
                UE_LOG(LogEntryChamberTest, Display, TEXT("Incomplete Button circuit rejected: gate closed and Stairs checkpoint preserved."));
            }
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
            return;
        }
        if (!Chamber.IsDoorOpen()) { return; }
        if (Session->Chamber.Pawn.Get() != &Player)
        {
            if (!SavedWithArchive(Segment)) { Fail(TEXT("checkpoint advanced before crossing")); return; }
            Session->Chamber.Pawn = &Player;
            UE_LOG(LogEntryChamberTest, Display, TEXT("Airlock visit %d: open gate, checkpoint still %s."), Session->Chamber.Visits + 1, *WriteSegment(Segment));
        }
    }

    APuzzleCube& First = *Cubes[0];
    switch (Phase)
    {
    case EPhase::Start:
        if (Elapsed < 0.35f)
        {
            break;
        }
        if (bWeight && bMissingPlate)
        {
            if (!bRevealedRoom)
            {
                bRevealedRoom = Chamber.RevealChamberForVerification();
                return;
            }
            if (Chamber.IsWaitingForChamber()) { return; }
            // Remove an actual registered body while Weight is still unsolved.
            for (TActorIterator<AChamberAssembly> It(GetWorld()); It; ++It)
            {
                if (It->Layout && It->Layout->IsA<UWeightBalanceLayout>() && It->IsStreamedRoomReady())
                {
                    It->PlateCircuit->GetPlate(2)->UnregisterComponent();
                    UE_LOG(LogEntryChamberTest, Display, TEXT("Removed the third Balance plate before solving Weight."));
                    break;
                }
            }
        }
        if ((Segment == ESegment::Balance && bTestBalance) || (bWeight && bTestWeight) || (Segment == ESegment::Button && bTestButton))
        {
            const auto* Level = Chamber.GetPlate()->GetOwner()->GetLevel();
            const int32 ExpectedPlates = bTestBalance ? 3 : Layout.GetPlateCount();
            for (int32 Index = 0; Index < ExpectedPlates; ++Index)
            {
                const auto* Plate = Chamber.GetPlate(Index);
                if (!IsValid(Plate) || !Plate->IsRegistered() || !Plate->IsQueryCollisionEnabled() ||
                    Plate->GetOwner()->GetLevel() != Level || Chamber.IsPlatePressed(Index))
                {
                    Fail(TEXT("streamed plate unavailable or already pressed on arrival"));
                    return;
                }
            }
            if (Session->Chamber.Pawn.Get() != &Player || Level == GetWorld()->PersistentLevel ||
                Chamber.GetDoor()->GetOwner()->GetLevel() != Level || Cubes.Num() != (bTestBalance ? 6 : bTestButton ? 2 : 4) ||
                (bTestButton && Chamber.IsParallelRelayPowered()) ||
                Cubes.ContainsByPredicate([Level](const auto& Cube) { return Cube->GetLevel() != Level; }) ||
                !Player.GetMovementBase() || Player.GetMovementBase()->GetOwner()->GetLevel() != Level ||
                !SavedWithArchive(Segment))
            {
                Fail(TEXT("streamed chamber ownership, pawn, floor or saved arrival mismatch"));
                return;
            }
            UE_LOG(LogEntryChamberTest, Display, TEXT("Airlock visit %d: reached streamed %s, checkpoint and archive confirmed."), ++Session->Chamber.Visits, *WriteSegment(Segment));
            SetComponentTickEnabled(false);
            if (Session->Chamber.Visits == 2) { FPlatformMisc::RequestExitWithStatus(false, 0); }
            else
            {
                Abandoned::Infrastructure::Expedition::TravelToWorld(this,
                    Abandoned::Application::Expedition::ChamberDestination{bTestBalance ? ESegment::Weight : bTestButton ? ESegment::Stairs : ESegment::Button});
            }
            return;
        }
        if (!bCaptured && EntryFlag(TEXT("CaptureEntryCourse")))
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Entry-") + WriteSegment(Segment), false, false);
            bCaptured = true;
            break;
        }
        if (bDev20)
        {
            TArray<AActor*> Units;
            UGameplayStatics::GetAllActorsOfClass(this, ADev20Actor::StaticClass(), Units);
            Unit = Player.GetHeldDev20();
            if (Units.Num() != 1 || !Unit.IsValid() || Units[0] != Unit.Get() ||
                !FMath::IsNearlyEqual(Unit->GetBody()->GetMass(), Dev20MassKg, 0.05f))
            {
                Fail(TEXT("exactly one 34 kg DEV-20 must arrive in the grip"));
                return;
            }
            Input.Press(Controller, EKeys::E);
            Advance(EPhase::DropUnit);
        }
        else if (bStairs)
        {
            UseAt(First.GetActorLocation(), EPhase::AimFirst);
        }
        else
        {
            Advance(EPhase::Arm);
        }
        break;
    case EPhase::DropUnit:
        if (Player.GetHeldDev20())
        {
            Fail(TEXT("could not release DEV-20"));
            return;
        }
        if (Segment == ESegment::Button && EntryFlag(TEXT("VerifyDev20Recovery")))
        {
            Unit->RestoreAt(Layout.GetCubeBounds().Min - FVector(0, 0, 100));
            UE_LOG(LogEntryChamberTest, Display, TEXT("DEV-20: exercising loss outside the Button chamber bounds."));
            Advance(EPhase::AwaitRecovery);
            break;
        }
        Unit->RestoreAt(FVector(Layout.Plate.Center.X, Layout.Plate.Center.Y,
            Layout.GetPlateTop() + Unit->GetBody()->GetUnscaledBoxExtent().Z + 0.2));
        UseAt(Layout.Button.Center, EPhase::MeasureUnit);
        break;
    case EPhase::AwaitRecovery:
        break; // The regular loss handler travels back to the station.
    case EPhase::MeasureUnit:
        if (Elapsed < 0.35f)
        {
            break;
        }
        if (!Chamber.IsButtonLatched() || !FMath::IsNearlyEqual(Chamber.GetMeasuredMass(), Dev20MassKg, 0.05f) || Chamber.IsExitEnabled())
        {
            Fail(TEXT("the armed chamber must weigh and reject a 34 kg DEV-20"));
            return;
        }
        UE_LOG(LogEntryChamberTest, Display, TEXT("DEV-20: %s measured and rejected 34 kg."), *WriteSegment(Segment));
        Unit->RestoreAt(FVector(Layout.Door.Center.X - 360.0, Layout.Door.Center.Y, 20.0));
        Advance(EPhase::Arm);
        break;
    case EPhase::Arm:
        UseAt(Layout.Button.Center, EPhase::TakeFirst);
        break;
    case EPhase::TakeFirst:
        if (!Chamber.IsButtonLatched() || Chamber.IsExitEnabled())
        {
            Fail(TEXT("wall button must arm the circuit and leave the unloaded door closed"));
            return;
        }
        UseAt(First.GetActorLocation(), EPhase::AimFirst);
        break;
    case EPhase::AimFirst:
        if (Player.GetHeldCube() != &First)
        {
            Fail(TEXT("could not pick up the first cube"));
            return;
        }
        if (bStairs)
        {
            const APuzzleCube& Base = *Cubes[1];
            AimHold(Base.GetActorLocation() + FVector(0, 0, Base.GetHalfSize() + First.GetHalfSize() + 40), FVector::RightVector);
        }
        else
        {
            const FSceneBlock& Plate = Layout.GetPlateAt(ExitPlateIndex);
            AimHold(Plate.Center + FVector(0, 0, Plate.Size.Z * 0.5 + First.GetHalfSize() + 40), FVector::ForwardVector);
        }
        Advance(EPhase::PlaceFirst);
        break;
    case EPhase::PlaceFirst:
        PlaceWhenAligned(First, bStairs ? Cubes[1]->GetBody() : Chamber.GetPlate(ExitPlateIndex), EPhase::CheckFirst);
        break;
    case EPhase::CheckFirst:
        if (First.GetSupport() != (bStairs ? Cubes[1]->GetBody() : Chamber.GetPlate(ExitPlateIndex)))
        {
            Fail(TEXT("first cube did not snap to the intended support"));
            return;
        }
        if (bStairs)
        {
            UE_LOG(LogEntryChamberTest, Display, TEXT("Entry test: first cube snapped to its Stairs support."));
            const APuzzleCube& Low = *Cubes[2];
            PlacePlayer(Player, FTransform(FRotator::ZeroRotator, Low.GetActorLocation() + FVector(-6, 0,
                Low.GetHalfSize() + Abandoned::MovementProfile::StandingHalfHeight + 1)));
            Advance(EPhase::RunUp);
        }
        else
        {
            if (ExitPlateIndex == 1)
            {
                UE_LOG(LogEntryChamberTest, Display, TEXT("Entry alternate solution: cube snapped to plate B."));
            }
            Advance(bWeight ? EPhase::TakeUpper : EPhase::WaitDoor);
        }
        break;
    case EPhase::RunUp:
        if (Player.GetMovementBase() == Cubes[2]->GetBody())
        {
            Player.AddMovementInput(FVector::ForwardVector);
            if (Player.GetActorLocation().X >= Cubes[2]->GetActorLocation().X + 7)
            {
                Player.Jump();
                Advance(EPhase::JumpToStack);
            }
        }
        break;
    case EPhase::JumpToStack:
        Player.AddMovementInput(FVector::ForwardVector * 0.35);
        if (Player.GetMovementBase() == First.GetBody())
        {
            Player.StopJumping();
            Player.Jump();
            Advance(EPhase::CrossLedge);
        }
        break;
    case EPhase::CrossLedge:
        Player.AddMovementInput(FVector::ForwardVector);
        break;
    case EPhase::TakeUpper:
        if (Elapsed < 0.35f)
        {
            break;
        }
        if (Chamber.GetMeasuredMass() != (UpperIndex == 3 ? 15.0f : 10.0f) || Chamber.IsExitEnabled())
        {
            Fail(TEXT("weight lock must reject the partial stack"));
            return;
        }
        UseAt(Cubes[UpperIndex]->GetActorLocation(), EPhase::AimUpper);
        break;
    case EPhase::AimUpper:
    {
        const APuzzleCube& Upper = *Cubes[UpperIndex];
        const APuzzleCube& Below = UpperIndex == 3 ? *Cubes[2] : First;
        if (Player.GetHeldCube() != &Upper)
        {
            Fail(TEXT("could not pick up the next cube for the stack"));
            return;
        }
        AimHold(Below.GetActorLocation() + FVector(0, 0, Below.GetHalfSize() + Upper.GetHalfSize() + 35), FVector::ForwardVector);
        Advance(EPhase::PlaceUpper);
        break;
    }
    case EPhase::PlaceUpper:
        PlaceWhenAligned(*Cubes[UpperIndex], UpperIndex == 3 ? Cubes[2]->GetBody() : First.GetBody(), EPhase::CheckUpper);
        break;
    case EPhase::CheckUpper:
        if (Cubes[UpperIndex]->GetSupport() != (UpperIndex == 3 ? Cubes[2]->GetBody() : First.GetBody()))
        {
            Fail(TEXT("upper cube did not snap to the stack"));
            return;
        }
        if (UpperIndex == 2)
        {
            UpperIndex = 3;
            Advance(EPhase::TakeUpper);
            break;
        }
        if (UpperIndex == 3)
        {
            if (Cubes[3]->CanSupportCube(*Cubes[2]))
            {
                Fail(TEXT("the stack must reject a fourth cube"));
                return;
            }
            UE_LOG(LogEntryChamberTest, Display, TEXT("Entry test: alternate solution assembled, 10 + 5 + 5 kg."));
        }
        Advance(EPhase::WaitDoor);
        break;
    case EPhase::WaitDoor:
        if (Chamber.IsDoorOpen())
        {
            if (bWeight && !bDev20 && !bVerifiedWeightReset)
            {
                Input.Press(Controller, EKeys::R);
                Advance(EPhase::CheckReset);
                break;
            }
            UE_LOG(LogEntryChamberTest, Display, TEXT("Entry test: %s accepted %.0f kg after the wall button."),
                *WriteSegment(Segment), Chamber.GetMeasuredMass(ExitPlateIndex));
            PlacePlayer(Player, FTransform(FRotator::ZeroRotator, FVector(Layout.Door.Center.X - 175, Layout.Door.Center.Y, Layout.Spawn.Z)));
            Advance(EPhase::WalkToExit);
        }
        break;
    case EPhase::CheckReset:
        if (Chamber.IsButtonLatched() || Chamber.GetMeasuredMass() != 0 || Chamber.IsExitEnabled() ||
            !Chamber.GetDoor()->GetComponentLocation().Equals(Layout.Door.Center))
        {
            Fail(TEXT("reset did not restore chamber 03 after solving its weight lock"));
            return;
        }
        UE_LOG(LogEntryChamberTest, Display, TEXT("Entry test: reset restored the weight lock, then the solution will be repeated."));
        bVerifiedWeightReset = true;
        UpperIndex = bAlternateWeight ? 2 : 1;
        Advance(EPhase::Arm);
        break;
    case EPhase::WalkToExit:
        Player.AddMovementInput(FVector::ForwardVector);
        if (bDev20 && Layout.GetExitBounds().IsInside(Player.GetActorLocation()))
        {
            UseAt(Unit->GetActorLocation(), EPhase::RetrieveUnit);
        }
        break;
    case EPhase::RetrieveUnit:
        if (Player.GetHeldDev20() != Unit.Get())
        {
            Fail(TEXT("could not retrieve DEV-20 after solving the chamber"));
            return;
        }
        Controller.SetControlRotation(FRotator(-14, 0, 0));
        UE_LOG(LogEntryChamberTest, Display, TEXT("DEV-20: %s held the player at the exit until the unit was retrieved."), *WriteSegment(Segment));
        Advance(EPhase::CarryToExit);
        break;
    case EPhase::CarryToExit:
        if (Player.GetHeldDev20() != Unit.Get())
        {
            Fail(TEXT("lost DEV-20 on the final approach to the exit"));
            return;
        }
        Player.AddMovementInput(FVector::ForwardVector);
        break;
    }
}
#endif
