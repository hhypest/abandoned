#include "TrainingChamber.h"
#include "TrainingChamber/ChamberMeshLibrary.h"
#include "Engine/GameInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "TrainingChamberLayout.h"
#include "Verification/DoorClampVerification.h"
#include "Verification/ChamberHintsVerification.h"
#include "Verification/ParallelCircuitVerification.h"
#include "Verification/PlateBalanceVerification.h"
#include "Verification/TrainingVerification.h"
#include "Verification/WeightStreamingVerification.h"
#include "Verification/EntryChamberVerification.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Expedition/Dev20Actor.h"
#include "Expedition/VanguardRoute.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Misc/CommandLine.h"
#include "Puzzles/PuzzleCube.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrainingChamber, Log, All);

namespace
{

using Abandoned::Domain::Expedition::Segment;
using Abandoned::Domain::Expedition::Dev20MassKg;
using Dev20Step = Abandoned::Domain::Expedition::Dev20Stage;
using Abandoned::Infrastructure::Expedition::UnrealCheckpointStore;
using Abandoned::Infrastructure::Expedition::WriteSegment;

}

ATrainingChamberGameMode::ATrainingChamberGameMode()
{
    DefaultPawnClass = ATrainingChamberCharacter::StaticClass();
    HUDClass = ATrainingChamberHUD::StaticClass();
    PlayerControllerClass = AAbandonedTestController::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface"));
    SurfaceMaterial = Material.Object;
}

void ATrainingChamberGameMode::SelectLayout(const Abandoned::Application::Expedition::WorldDestination& Destination)
{
    using namespace Abandoned::Application::Expedition;
    const auto* Dev20Route = std::get_if<Dev20Destination>(&Destination);
    bDev20Run = Dev20Route != nullptr;
    ChamberDestination Arrival = Dev20Route ? ChamberDestination{Dev20Chamber(Dev20Route->Stage)}
                                           : std::get<ChamberDestination>(Destination);
    if (Dev20Route)
    {
        if (const auto* Session = Abandoned::Infrastructure::Expedition::Dev20Session(this);
            Session && Session->Transfer.IsExpected(Dev20Route->Stage))
        {
            Dev20Stage = Dev20Route->Stage;
        }
    }
    bool bReadCheckpoint = !bDev20Run && GetWorld()->WorldType == EWorldType::Game;
#if WITH_DEV_AUTOMATION_TESTS
    if (UChamberHintsVerification::IsRequested())
    {
        Arrival.Chamber = Segment::Weight;
        Course = StartCourse(Arrival, nullptr);
        Layout = UChamberHintsVerification::CreateFixtureLayout(*this);
        return;
    }
    const bool bVerifyDoorClamp = Abandoned::Verification::WantsDoorClampVerification();
    if (!bVerifyDoorClamp && UParallelCircuitVerification::IsRequested())
    {
        Course = StartCourse({Segment::Button}, nullptr);
        Layout = GetDefault<UEntryButtonLayout>();
        return;
    }
    if (!bVerifyDoorClamp && UPlateBalanceVerification::IsStandaloneRequested())
    {
        Course = StartCourse({Segment::Balance}, nullptr);
        Layout = GetDefault<UWeightBalanceLayout>();
        return;
    }
    if (!bVerifyDoorClamp && UTrainingVerification::IsRequested())
    {
        Course = StartCourse({Segment::Sandbox}, nullptr);
        Layout = GetDefault<UTrainingChamberLayout>();
        return;
    }
    if (bVerifyDoorClamp)
    {
        Arrival.Chamber = Segment::Weight;
        bReadCheckpoint = false;
    }
    else if (UEntryChamberVerification::IsRequested(bDev20Run) && !UEntryChamberVerification::UsesCheckpoint())
    {
        bReadCheckpoint = false;
    }
#endif
    const UnrealCheckpointStore Store;
    Course = StartCourse(Arrival, bReadCheckpoint ? &Store : nullptr);
    if (Course.Issue != Abandoned::Application::Expedition::CheckpointIssue::None)
    {
        UE_LOG(LogTrainingChamber, Warning, TEXT("%s Starting at the first chamber."),
            Abandoned::Infrastructure::Expedition::CheckpointIssueText(Course.Issue));
    }
    if (Course.Current == Segment::Button)
    {
        Layout = GetDefault<UEntryButtonLayout>();
    }
    else if (Course.Current == Segment::Weight)
    {
        Layout = GetDefault<UEntryWeightLayout>();
    }
    else if (Course.Current == Segment::Balance)
    {
        Layout = GetDefault<UWeightBalanceLayout>();
    }
    else if (Course.Current == Segment::Sandbox)
    {
        Layout = GetDefault<UTrainingChamberLayout>();
    }
    else
    {
        Layout = GetDefault<UEntryStairsLayout>();
    }
}

void ATrainingChamberGameMode::SaveEntryCheckpoint()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Training_SaveEntryCheckpoint);
    if (bDev20Run)
    {
        return;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (UTrainingVerification::IsRequested() || (UEntryChamberVerification::IsRequested(bDev20Run) && !UEntryChamberVerification::UsesCheckpoint()) ||
        UPlateBalanceVerification::IsStandaloneRequested() ||
        UChamberHintsVerification::IsRequested() ||
        Abandoned::Verification::WantsDoorClampVerification() || UParallelCircuitVerification::IsRequested())
    {
        return;
    }
#endif
    if (GetWorld()->WorldType == EWorldType::Game &&
        Abandoned::Application::Expedition::RequiresArrivalCheckpoint(Course))
    {
        ArrivalWrite = UnrealCheckpointStore{}.BeginArrival(Course.Current);
    }
}

void ATrainingChamberGameMode::AdvanceCourse(const ATrainingChamberCharacter& Player)
{
    if (bDev20Run)
    {
        if (Dev20Stage == Dev20Step::Button)
        {
            bReachedExit = Abandoned::Infrastructure::Expedition::TravelWithDev20(
                this, Dev20Step::Button, IsValid(Dev20) && Player.GetHeldDev20() == Dev20);
        }
        return;
    }
    if (const auto Next = Abandoned::Application::Expedition::NextCourseDestination(Course))
    {
        const bool bNextIsChamber = std::holds_alternative<Abandoned::Application::Expedition::ChamberDestination>(*Next);
        Abandoned::Infrastructure::Expedition::TravelToWorld(this, *Next, bNextIsChamber);
    }
}

void ATrainingChamberGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Training_InitGame);
    Super::InitGame(MapName, Options, ErrorMessage);
    if (!ErrorMessage.IsEmpty())
    {
        return;
    }

    using Abandoned::Infrastructure::Expedition::EntryWorld;
    using Abandoned::Infrastructure::Expedition::ReadWorldDestination;
    const auto Destination = ReadWorldDestination(Options, EntryWorld::Chamber, ErrorMessage);
    if (!Destination)
    {
        return;
    }
    SelectLayout(*Destination);
    if (!Layout->IsValid())
    {
        ErrorMessage = TEXT("Invalid training chamber data in DefaultGame.ini.");
        return;
    }

    Devices = GetWorld()->SpawnActor<AChamberAssembly>();
    if (!Devices) { ErrorMessage = TEXT("Could not create chamber devices."); return; }
    Devices->Build(*Layout, SurfaceMaterial, Course.Current != Segment::Sandbox, ErrorMessage);
    if (!ErrorMessage.IsEmpty()) { return; }
    UWorld& World = *GetWorld();
    SaveEntryCheckpoint();
    if (NeedsAirlock()) { RequestNextChamber(); }
    if ((Layout->IsA<UEntryStairsLayout>() || Layout->IsA<UEntryButtonLayout>())
        && FParse::Param(FCommandLine::Get(), TEXT("CompoundWeightShell")))
    {
        FTimerHandle PrepareTimer;
        World.GetTimerManager().SetTimer(PrepareTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (GetWorld() && GetGameInstance()->GetWorld() == GetWorld())
            {
                GetGameInstance()->GetSubsystem<UChamberMeshLibrary>()->PrepareWeightShell();
            }
        }), 0.5f, false);
    }
    ActivateChamber();
}

void ATrainingChamberGameMode::ActivateChamber()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Training_ArrivalReady);
#if WITH_DEV_AUTOMATION_TESTS
    if (Course.Current == Segment::Stairs)
    {
        UWeightStreamingVerification::AttachIfRequested(*this);
    }
    const bool bVerifyCourse = UEntryChamberVerification::IsRequested(bDev20Run);
    if (Course.Current == Segment::Sandbox && UTrainingVerification::IsRequested())
    {
        UTrainingVerification::Attach(*this, Devices->Cubes, Devices->PlateCircuit->GetPlates());
    }
    else if (UChamberHintsVerification::IsRequested())
    {
        UChamberHintsVerification::Attach(*this);
    }
    else if (Course.Current == Segment::Button && UParallelCircuitVerification::IsRequested())
    {
        UParallelCircuitVerification::Attach(*this, Devices->Cubes, Devices->RelayTraces, Devices->RelayIdleMaterial, Devices->RelayPoweredMaterial);
    }
    else if (Course.Current == Segment::Balance && FParse::Param(FCommandLine::Get(), TEXT("VerifyBalanceAirlock")))
    {
        UEntryChamberVerification::Attach(*this, Course.Current, Devices->Cubes);
    }
    else if (Course.Current == Segment::Balance && (UPlateBalanceVerification::IsStandaloneRequested() || bVerifyCourse))
    {
        UPlateBalanceVerification::Attach(*this, Devices->Cubes, Devices->PlateCircuit->GetPlates());
    }
    else if (bVerifyCourse && (Course.Current == Segment::Stairs || Course.Current == Segment::Button || Course.Current == Segment::Weight))
    {
        UEntryChamberVerification::Attach(*this, Course.Current, Devices->Cubes);
    }
#endif
    UDoorClampVerification::Attach(*this);
    UE_LOG(LogTrainingChamber, Display, TEXT("Training chamber ready. %s, %d cubes, plate released, exit closed."),
        *WriteSegment(Course.Current), Devices->Cubes.Num());
}

void ATrainingChamberGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    if (IsValid(NewPlayer) && Course.Current == Segment::Passage)
    {
        if (auto* Active = AVanguardSegment::ActiveIn(*GetWorld()))
        {
            RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, Active->PlayerStart()));
            if (auto* Suit = Cast<AVanguardCharacter>(NewPlayer->GetPawn())) { Active->Equip(*Suit); }
        }
        return;
    }
    if (IsValid(NewPlayer) && IsValid(GetDoor()) && IsValid(GetPlate()))
    {
        RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, Layout->Spawn));
        NewPlayer->SetInputMode(FInputModeGameOnly());
        if (Dev20Stage && !Dev20)
        {
            auto* Player = Cast<ATrainingChamberCharacter>(NewPlayer->GetPawn());
            auto* Session = Abandoned::Infrastructure::Expedition::Dev20Session(this);
            if (Player && Session && Session->Transfer.IsExpected(*Dev20Stage))
            {
                Dev20 = GetWorld()->SpawnActor<ADev20Actor>(
                    Layout->Spawn + FVector(150.0, 0.0, Abandoned::MovementProfile::CameraHeight),
                    FRotator::ZeroRotator);
                if (IsValid(Dev20) && Player->GrabBody(*Dev20->GetBody(), Dev20MassKg) &&
                    Session->Transfer.Arrive(*Dev20Stage))
                {
                    UE_LOG(LogTrainingChamber, Display, TEXT("DEV-20 arrived in %s, %.0f kg, grip restored."),
                        *WriteSegment(Course.Current), Dev20->GetBody()->GetMass());
                    return;
                }
            }
            if (IsValid(Dev20))
            {
                if (Player)
                {
                    Player->ReleaseHeldObject();
                }
                Dev20->Destroy();
                Dev20 = nullptr;
            }
            Dev20Stage.reset();
            UE_LOG(LogTrainingChamber, Error, TEXT("Could not restore DEV-20 hand-off."));
        }
    }
}

void ATrainingChamberGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Devices || !IsValid(GetDoor()))
    {
        if (Devices) { Devices->DoorDrive->ReleaseClamp(); }
        return;
    }
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ATrainingChamberCharacter* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || !IsValid(GetPlate()) || bDev20Recovering)
    {
        return;
    }

    if (NeedsAirlock())
    {
        PrepareNextChamber();
        if (bReachedExit)
        {
            if (!EnterNextChamber(*Player)) { bReachedExit = false; }
            return;
        }
    }
    const FVector Location = Player->GetActorLocation();
    if (Location.Z < -600.0)
    {
        ResetChamber(*Player);
        return;
    }
    const FBox CubeBounds = Layout->GetCubeBounds();
    if (Dev20Stage && (!IsValid(Dev20) || !CubeBounds.IsInside(Dev20->GetActorLocation())))
    {
        ResetChamber(*Player);
        return;
    }
    for (int32 Index = 0; Index < Devices->Cubes.Num(); ++Index)
    {
        if (IsValid(Devices->Cubes[Index]) && !CubeBounds.IsInside(Devices->Cubes[Index]->GetActorLocation()))
        {
            ReturnCube(*Player, Index, Layout->bDispenseCubes);
        }
    }

    Devices->PlateCircuit->Observe(*Player, Devices->Cubes, Dev20.Get(), DeltaSeconds);
    if (Layout->UsesParallelPlates())
    {
        Devices->UpdateRelayTraces();
    }
    ObserveHints(*Player, DeltaSeconds);
    if (Devices->DoorDrive->Move(*Player, Dev20.Get(), IsExitEnabled() && !IsWaitingForChamber(), Course.Current != Segment::Sandbox, DeltaSeconds) ==
        ETrainingDoorResult::ResetPlayer)
    {
        ResetChamber(*Player);
        return;
    }

    const bool bCanLeave = !bDev20Run || (IsValid(Dev20) && Player->GetHeldDev20() == Dev20);
    if (!bReachedExit && IsDoorOpen() && Layout->GetExitBounds().IsInside(Location) && bCanLeave)
    {
        bReachedExit = true;
        UE_LOG(LogTrainingChamber, Display, TEXT("Player reached the exit."));
        if (!NeedsAirlock()) { AdvanceCourse(*Player); }
    }
}

const TCHAR* ATrainingChamberGameMode::GetDev20Status(const ATrainingChamberCharacter& Player) const
{
    if (!IsValid(Dev20))
    {
        return TEXT("DEV-20 transfer expired. Restart Load DEV-20 Route from the test menu.");
    }
    if (bReachedExit && Dev20Stage == Dev20Step::Weight)
    {
        return TEXT("DEV-20 delivered. Route complete.");
    }
    return Player.GetHeldDev20() == Dev20 ? TEXT("DEV-20 in hand (34 kg). Carry through the exit.")
        : TEXT("Solve the chamber, then bring DEV-20 through the exit.");
}

void ATrainingChamberGameMode::PressButton()
{
    if (!IsButtonLatched() && IsValid(Devices->ButtonVisual))
    {
        Devices->PlateCircuit->PressButton();
        Devices->ButtonVisual->SetRelativeLocation(FVector(8.0, 0.0, 0.0));
        UE_LOG(LogTrainingChamber, Display, TEXT("Door circuit armed by wall button."));
    }
}

void ATrainingChamberGameMode::ReturnCube(ATrainingChamberCharacter& Player, int32 Index, bool bUseDispenser)
{
    if (APuzzleCube* Cube = Devices->Cubes[Index]; IsValid(Cube))
    {
        Devices->DoorDrive->ReleaseIfHolding(Cube->GetBody());
        if (Player.GetHeldCube() == Cube)
        {
            Player.ReleaseHeldObject();
        }
        const FTrainingCubeSpawn& Spawn = Layout->CubeSpawns[Index];
        Cube->ResetPosition(bUseDispenser ? Spawn.Outlet : Spawn.Center);
    }
    UE_LOG(LogTrainingChamber, Display, TEXT("Cube returned by %s."), bUseDispenser ? TEXT("dispenser") : TEXT("reset"));
}

void ATrainingChamberGameMode::ResetChamber(ATrainingChamberCharacter& Player)
{
    Devices->DoorDrive->ReleaseClamp();
    if (bDev20Run)
    {
        if (!bDev20Recovering)
        {
            bDev20Recovering = true;
            Abandoned::Infrastructure::Expedition::ReturnToDev20Station(
                this, Abandoned::Application::Expedition::Dev20Entry::Recovery);
        }
        return;
    }
    if (NeedsAirlock() && bChamberLoadFailed) { RequestNextChamber(); }
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
    Player.ReleaseHeldObject();
    for (int32 Index = 0; Index < Devices->Cubes.Num(); ++Index)
    {
        ReturnCube(Player, Index);
    }
    Devices->PlateCircuit->Reset();
    HintReader = {};
    Devices->UpdateRelayTraces();
    Devices->DoorDrive->ReturnToClosedPosition();
    if (Devices->ButtonVisual)
    {
        Devices->ButtonVisual->SetRelativeLocation(FVector::ZeroVector);
    }
    bReachedExit = false;
}

bool ATrainingChamberGameMode::UseDevice(APuzzleCharacter&, UPrimitiveComponent& Target)
{
    if (&Target == Devices->HintTerminal)
    {
        ReadNextHint();
        return true;
    }
    if (&Target == Devices->Button)
    {
        PressButton();
        return true;
    }
    return false;
}

const TCHAR* ATrainingChamberGameMode::DevicePrompt(const UPrimitiveComponent& Target) const
{
    if (&Target == Devices->HintTerminal)
    {
        return TEXT("[E] Read hint terminal");
    }
    if (&Target == Devices->Button)
    {
        return IsButtonLatched() ? TEXT("Circuit armed") : TEXT("[E] Arm the door circuit");
    }
    return nullptr;
}

void ATrainingChamberGameMode::ResetPuzzle(APuzzleCharacter& Player)
{
    if (auto* Trainee = Cast<ATrainingChamberCharacter>(&Player))
    {
        ResetChamber(*Trainee);
    }
}

bool ATrainingChamberCharacter::CanReadHintTerminal() const
{
    const ATrainingChamberGameMode* Chamber = GetWorld()->GetAuthGameMode<ATrainingChamberGameMode>();
    return Chamber && IsValid(Chamber->GetHintTerminal()) && !GetHeldBody() &&
        TraceInteractable() == Chamber->GetHintTerminal();
}

void ATrainingChamberHUD::DrawHUD()
{
    Super::DrawHUD();
    const ATrainingChamberCharacter* Player = Cast<ATrainingChamberCharacter>(GetOwningPawn());
    const ATrainingChamberGameMode* Chamber = GetWorld()->GetAuthGameMode<ATrainingChamberGameMode>();
    if (!Player || !Chamber)
    {
        return;
    }

    const UTrainingChamberLayout* Layout = Chamber->GetLayout();
    DrawText(Layout->Title, FLinearColor::White, 24.0f, 24.0f, nullptr, 1.2f);
    const TCHAR* Controls = IsValid(Chamber->GetHintTerminal())
        ? TEXT("WASD / mouse   Space jump   E interact   LMB throw   R reset   Esc pause   |   HINTS at entrance")
        : TEXT("WASD / mouse   Space jump   E pick up / place   LMB throw   R reset   Esc pause");
    DrawText(Controls, FLinearColor::White, 24.0f, 52.0f);
    const bool bDoorObstructed = Chamber->HasClampedObject() && !Chamber->HasReachedExit();
    const FString Status = Chamber->HasChamberLoadFailed() ? TEXT("Next chamber unavailable. Press R to retry.") : Chamber->HasReachedExit()
        ? (Layout->UsesPlateBalance() ? TEXT("Balance chamber complete") : TEXT("Entry course complete")) :
        (bDoorObstructed ? TEXT("Door obstructed. Restore power to release.") :
        Chamber->IsDoorOpen() ? TEXT("Exit open") :
        (Chamber->IsExitEnabled() ? (Chamber->IsWaitingForChamber() ? TEXT("Preparing next chamber") : TEXT("Exit opening")) : Layout->Objective));
    if (bDoorObstructed)
    {
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.78f), 16.0f, 71.0f, 300.0f, 24.0f);
    }
    DrawText(Status, bDoorObstructed ? FLinearColor::White : FLinearColor(1.0, 0.68, 0.23), 24.0f, 76.0f);
    if (Layout->UsesParallelPlates())
    {
        const bool bA = Chamber->IsPlatePressed(0);
        const bool bB = Chamber->IsPlatePressed(1);
        DrawText(FString::Printf(TEXT("Switch: %s   |   A: %.0f kg %s   |   B: %.0f kg %s   |   A OR B: %s"),
            Chamber->IsButtonLatched() ? TEXT("armed") : TEXT("off"),
            Chamber->GetMeasuredMass(0), bA ? TEXT("live") : TEXT("idle"),
            Chamber->GetMeasuredMass(1), bB ? TEXT("live") : TEXT("idle"),
            Chamber->IsParallelRelayPowered() ? TEXT("live") : TEXT("idle")), FLinearColor::White, 24.0f, 101.0f);
    }
    else if (Layout->bRequiresButton)
    {
        DrawText(FString::Printf(TEXT("Circuit: %s   |   Plate: %.0f kg   |   Target: %.0f%s kg"),
            Chamber->IsButtonLatched() ? TEXT("armed") : TEXT("off"), Chamber->GetMeasuredMass(),
            Layout->PlateActivationMassKg, Layout->PlateActivationMassKg == Layout->PlateMaximumMassKg ? TEXT("") : TEXT("+")),
            FLinearColor::White, 24.0f, 101.0f);
    }
    if (Layout->UsesPlateBalance())
    {
        const TArray<float>& Loads = Chamber->GetPlateLoads();
        const float Target = Layout->BalanceTotalKg / Layout->GetPlateCount();
        DrawText(FString::Printf(TEXT("Target: %.0f kg on each plate"), Target), FLinearColor::White, 24.0f, 103.0f);
        for (int32 Index = 0; Index < Loads.Num(); ++Index)
        {
            const TCHAR* State = FMath::IsNearlyEqual(Loads[Index], Target, 0.01f) ? TEXT("ready")
                : Loads[Index] < Target ? TEXT("too light") : TEXT("too heavy");
            DrawText(FString::Printf(TEXT("Plate %d: %.0f kg  |  %s"), Index + 1, Loads[Index], State),
                FLinearColor::White, 24.0f, 126.0f + 21.0f * Index);
        }
    }
    if (const auto Issue = Chamber->GetCheckpointIssue(); Issue != Abandoned::Application::Expedition::CheckpointIssue::None)
    {
        const FString Notice = FString(Abandoned::Infrastructure::Expedition::CheckpointIssueText(Issue)) +
            TEXT(" Starting from chamber 01.");
        DrawText(Notice, FLinearColor::White, 24.0f, 125.0f);
    }
    else if (Chamber->HasCheckpointWriteFailed())
    {
        DrawText(TEXT("Could not save checkpoint. Previous data preserved."), FLinearColor::White, 24.0f, 125.0f);
    }
    if (Chamber->IsDev20Run())
    {
        DrawText(Chamber->GetDev20Status(*Player), FLinearColor::White, 24.0f, 147.0f);
    }

    const float CenterX = Canvas->ClipX * 0.5f;
    const float CenterY = Canvas->ClipY * 0.5f;
    DrawRect(FLinearColor::White, CenterX - 1.0f, CenterY - 1.0f, 3.0f, 3.0f);
    const auto& Hints = Chamber->GetHints();
    const bool bShowHintPage = Hints.Page() > 0 && Player->CanReadHintTerminal();
    const TCHAR* Prompt = bShowHintPage ? nullptr : Player->GetPlacementPrompt();
    if (Prompt)
    {
        float PromptWidth = 0.0f;
        float PromptHeight = 0.0f;
        GetTextSize(Prompt, PromptWidth, PromptHeight);
        DrawText(Prompt, FLinearColor::White, CenterX - PromptWidth * 0.5f, CenterY + 22.0f);
    }
    if (bShowHintPage)
    {
        DrawHintPage(*Layout, Hints.Page(), Hints.AvailablePages());
    }
    if (IsValid(Chamber->GetHintTerminal()) && Hints.HasNewEntry())
    {
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 192.0f, 168.0f, 25.0f);
        DrawText(TEXT("New terminal entry"), FLinearColor::White, 24.0f, 197.0f);
    }
}
