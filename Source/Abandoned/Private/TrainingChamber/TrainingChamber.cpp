#include "TrainingChamber.h"
#include "TrainingChamberLayout.h"
#include "Verification/DoorClampVerification.h"
#include "Verification/ChamberHintsVerification.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Expedition/Dev20Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Infrastructure/Puzzles/CubePlacementComponent.h"
#include "Infrastructure/Puzzles/PuzzleInteractionSettings.h"
#include "Puzzles/PuzzleCube.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Blockout.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrainingChamber, Log, All);

namespace
{

using Abandoned::Domain::Expedition::Segment;
using Abandoned::Domain::Expedition::Dev20MassKg;
using Dev20Step = Abandoned::Domain::Expedition::Dev20Stage;
using Abandoned::Domain::Puzzles::CircuitRules;
using Abandoned::Infrastructure::Expedition::UnrealCheckpointStore;
using Abandoned::Infrastructure::Expedition::WriteSegment;
constexpr double DoorTravel = 280.0;
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
    bVerifyChamber = FParse::Param(FCommandLine::Get(), TEXT("VerifyTrainingChamber"));
    const bool bVerifyDoorClamp = Abandoned::Verification::WantsDoorClampVerification();
    bVerifyParallelCircuit = FParse::Param(FCommandLine::Get(), TEXT("VerifyParallelCircuit"));
    bVerifyBalance = FParse::Param(FCommandLine::Get(), TEXT("VerifyBalanceChamber"));
    bVerifyEntryCheckpoint = FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCheckpoint"));
    bVerifyDev20 = Dev20Stage.has_value() && FParse::Param(FCommandLine::Get(), TEXT("VerifyDev20"));
    bVerifyEntryCourse = bVerifyDev20 || bVerifyEntryCheckpoint || FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCourse"));
    if (bVerifyDoorClamp)
    {
        Arrival.Chamber = Segment::Weight;
        bReadCheckpoint = false;
    }
    else if (bVerifyParallelCircuit)
    {
        Arrival = {Segment::Button};
        bReadCheckpoint = false;
    }
    else if (bVerifyBalance)
    {
        Arrival = {Segment::Balance};
        bVerifyEntryCourse = false;
        bVerifyChamber = false;
    }
    else if (bVerifyChamber)
    {
        Arrival.Chamber = Segment::Sandbox;
    }
    else if (bVerifyEntryCourse && !bVerifyEntryCheckpoint)
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
    if (bDev20Run)
    {
        return;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerifyChamber || (bVerifyEntryCourse && !bVerifyEntryCheckpoint) || bVerifyBalance || UChamberHintsVerification::IsRequested() ||
        Abandoned::Verification::WantsDoorClampVerification() || bVerifyParallelCircuit)
    {
        return;
    }
#endif
    if (GetWorld()->WorldType == EWorldType::Game &&
        !Abandoned::Application::Expedition::RecordArrival(Course, UnrealCheckpointStore{}))
    {
        bCheckpointWriteFailed = true;
        UE_LOG(LogTrainingChamber, Warning, TEXT("Could not save the entry checkpoint. Previous data preserved."));
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
#if WITH_DEV_AUTOMATION_TESTS
        else if (bVerifyDev20 && Dev20Stage == Dev20Step::Weight)
        {
            UE_LOG(LogTrainingChamber, Display, TEXT("DEV-20 verification passed: portal, Button, Weight and final exit."));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
#endif
        return;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerifyBalance)
    {
        UE_LOG(LogTrainingChamber, Display, TEXT("Balance chamber verification passed: three plates, two solutions, reset and exit."));
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return;
    }
    if (Course.Current == Segment::Balance && bVerifyEntryCourse)
    {
        UE_LOG(LogTrainingChamber, Display, TEXT("Entry course verification passed: balance released the passage."));
    }
#endif
    if (const auto Next = Abandoned::Application::Expedition::NextCourseDestination(Course))
    {
        Abandoned::Infrastructure::Expedition::TravelToWorld(this, *Next);
    }
}

void ATrainingChamberGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
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

    UStaticMesh* Mesh = Abandoned::Blockout::LoadCube();
    if (!Mesh)
    {
        ErrorMessage = TEXT("Training chamber requires the Engine BasicShapes cube.");
        return;
    }

    UWorld& World = *GetWorld();
    Abandoned::Blockout::ApplyGravity(World);
    const auto Finishes = Abandoned::Scene::MakeFinishes(*this, SurfaceMaterial, Layout->Finishes);
    for (const FSceneBlock& Block : Layout->Room)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *Mesh, Block.Center, Block.Size,
            EComponentMobility::Static, Block.bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
    }
    for (const FTrainingCubeSpawn& CubeSpawn : Layout->CubeSpawns)
    {
        APuzzleCube* Cube = World.SpawnActor<APuzzleCube>(CubeSpawn.Center, FRotator::ZeroRotator);
        if (!Cube)
        {
            ErrorMessage = TEXT("Could not create a training chamber cube.");
            return;
        }
        Cube->Initialize(CubeSpawn.Type, *Mesh);
        const FName CubeFinish(CubeSpawn.Type == EPuzzleCubeType::Large ? TEXT("Heavy") : TEXT("Light"));
        if (UMaterialInterface* const* Material = Finishes.Find(CubeFinish))
        {
            Cube->SetVisualMaterial(**Material);
        }
        Cubes.Add(Cube);
    }
    const auto DoorParts = Abandoned::Blockout::SpawnBox(World, *Mesh, Layout->Door.Center, Layout->Door.Size,
        EComponentMobility::Movable, TEXT("BlockAllDynamic"));
    Door = DoorParts.Collision;
    Abandoned::Scene::SetFinish(DoorParts, Layout->Door, Finishes);
    Plates.Reserve(Layout->GetPlateCount());
    PlateLoads.SetNumZeroed(Layout->GetPlateCount());
    for (int32 Index = 0; Index < Layout->GetPlateCount(); ++Index)
    {
        const FSceneBlock& Block = Layout->GetPlateAt(Index);
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *Mesh, Block.Center, Block.Size, EComponentMobility::Movable);
        if (!Parts.Collision || !Parts.Visual)
        {
            ErrorMessage = TEXT("Could not create the pressure plates.");
            return;
        }
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
        FTrainingPressurePlate& Plate = Plates.AddDefaulted_GetRef();
        Plate.Body = Parts.Collision;
        UCubePlacementComponent::MarkPressurePlate(*Plate.Body);
        Plate.Visual = Parts.Visual;
        FSceneLight Light = Layout->PlateLight;
        Light.Position += Block.Center - Layout->Plate.Center;
        Plate.Indicator = Abandoned::Scene::AddLight(World, Light);
        if (Layout->UsesPlateBalance() || Layout->UsesParallelPlates())
        {
            AActor* PlateActor = Parts.Collision->GetOwner();
            auto* Sign = NewObject<UTextRenderComponent>(PlateActor);
            PlateActor->AddInstanceComponent(Sign);
            Sign->SetupAttachment(Parts.Collision);
            Sign->SetRelativeLocation(FVector(-Block.Size.X * 0.5 - 12.0, 0, 22.0));
            Sign->SetRelativeRotation(FRotator(0, 180, 0));
            Sign->SetHorizontalAlignment(EHTA_Center);
            Sign->SetVerticalAlignment(EVRTA_TextCenter);
            Sign->SetWorldSize(20.0f);
            Sign->SetText(Layout->UsesParallelPlates() ? FText::FromString(Index == 0 ? TEXT("A") : TEXT("B"))
                : FText::AsNumber(Index + 1));
            Sign->RegisterComponent();
        }
    }
    if (Layout->UsesParallelPlates() &&
        !SpawnRelayTraces(*Mesh, Finishes.FindRef(FName(TEXT("RelayIdle"))), Finishes.FindRef(FName(TEXT("RelayPowered")))))
    {
        ErrorMessage = TEXT("Could not create the parallel plate signal traces.");
        return;
    }
    if (Layout->bRequiresButton)
    {
        const auto ButtonParts = Abandoned::Blockout::SpawnBox(World, *Mesh, Layout->Button.Center,
            Layout->Button.Size, EComponentMobility::Movable);
        Button = ButtonParts.Collision;
        ButtonVisual = ButtonParts.Visual;
        Abandoned::Scene::SetFinish(ButtonParts, Layout->Button, Finishes);
    }

    if (!Door || (Layout->bRequiresButton && (!Button || !ButtonVisual)))
    {
        ErrorMessage = TEXT("Could not create the cube, pressure plate or door.");
        return;
    }
    DoorClamp.AddSigns(*Door);

    if (!Layout->HintPages.IsEmpty())
    {
        const FSceneBlock& Terminal = Layout->HintTerminal;
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *Mesh, Terminal.Center, Terminal.Size);
        if (!IsValid(Parts.Collision))
        {
            ErrorMessage = TEXT("Could not create the chamber hint terminal.");
            return;
        }
        HintTerminal = Parts.Collision;
        Abandoned::Scene::SetFinish(Parts, Terminal, Finishes);
        AActor* TerminalActor = HintTerminal->GetOwner();
        auto* Label = NewObject<UTextRenderComponent>(TerminalActor);
        TerminalActor->AddInstanceComponent(Label);
        Label->SetupAttachment(HintTerminal);
        Label->SetRelativeLocation(FVector(Terminal.Size.X * 0.5 + 1.0, 0.0, 0.0));
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetVerticalAlignment(EVRTA_TextCenter);
        Label->SetWorldSize(10.0f);
        Label->SetText(NSLOCTEXT("ChamberHints", "TerminalLabel", "HINTS"));
        Label->RegisterComponent();
    }

    for (const FSceneLight& Light : Layout->Lights)
    {
        Abandoned::Scene::AddLight(World, Light);
    }
    SaveEntryCheckpoint();
#if WITH_DEV_AUTOMATION_TESTS
    UChamberHintsVerification::Attach(*this);
#endif
    UDoorClampVerification::Attach(*this);
    UE_LOG(LogTrainingChamber, Display, TEXT("Training chamber ready. %s, %d cubes, plate released, exit closed."),
        *WriteSegment(Course.Current), Cubes.Num());
}

void ATrainingChamberGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    if (IsValid(NewPlayer) && IsValid(Door) && IsValid(GetPlate()))
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
    if (!IsValid(Door))
    {
        DoorClamp.Release();
        return;
    }
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ATrainingChamberCharacter* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || !IsValid(GetPlate()) || bDev20Recovering)
    {
        return;
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
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        if (IsValid(Cubes[Index]) && !CubeBounds.IsInside(Cubes[Index]->GetActorLocation()))
        {
            ReturnCube(*Player, Index, Layout->bDispenseCubes);
        }
    }

    ObserveCircuit(*Player, DeltaSeconds);
    ObserveHints(*Player, DeltaSeconds);
    if (MoveDoor(*Player, DeltaSeconds))
    {
        return;
    }

    const bool bCanLeave = !bDev20Run || (IsValid(Dev20) && Player->GetHeldDev20() == Dev20);
    if (!bReachedExit && IsDoorOpen() && Layout->GetExitBounds().IsInside(Location) && bCanLeave)
    {
        bReachedExit = true;
        UE_LOG(LogTrainingChamber, Display, TEXT("Player reached the exit."));
        AdvanceCourse(*Player);
        return;
    }

#if WITH_DEV_AUTOMATION_TESTS
    if (bVerifyParallelCircuit)
    {
        VerifyParallelCircuit(*Player, DeltaSeconds);
    }
    else if (bVerifyChamber)
    {
        VerifyChamber(*Player, DeltaSeconds);
    }
    else if (bVerifyBalance || (bVerifyEntryCourse && Course.Current == Segment::Balance))
    {
        VerifyBalanceChamber(*Player, DeltaSeconds);
    }
    else if (bVerifyEntryCourse)
    {
        VerifyEntryChamber(*Player, DeltaSeconds);
    }
#endif
}

bool ATrainingChamberGameMode::MoveDoor(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    using namespace Abandoned::Domain::Puzzles;
    const auto Sweep = DoorClamp.Move(*Door, Layout->Door.Center.Z, DoorTravel, 350.0 * DeltaSeconds, IsExitEnabled());
    if (!Sweep.Closing || !Sweep.Hit.bBlockingHit)
    {
        return false;
    }
    UBoxComponent* Body = Cast<UBoxComponent>(Sweep.Hit.GetComponent());
    const APuzzleCube* Cube = Cast<APuzzleCube>(Sweep.Hit.GetActor());
    const bool bPortable = Body && ((Cube && Cube->GetBody() == Body) || (IsValid(Dev20) && Dev20->GetBody() == Body));
    const DoorContact Contact = Sweep.Hit.GetActor() == &Player ? DoorContact::Player :
        bPortable ? DoorContact::Carryable : DoorContact::None;
    switch (ResolveClosingContact(Sweep.Closing, Contact, Course.Current != Segment::Sandbox))
    {
    case ClosingContact::Clamp:
        if (Player.GetHeldBody() == Body)
        {
            Player.ReleaseHeldObject();
        }
        DoorClamp.Capture(*Body);
        break;
    case ClosingContact::ResetPlayer:
        ResetChamber(Player);
        return true;
    case ClosingContact::Block:
        break;
    }
    return false;
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

bool ATrainingChamberGameMode::HasStackAtExit() const
{
    const FBox StackBounds = Layout->GetStackBounds();
    return Cubes.ContainsByPredicate([&StackBounds](const APuzzleCube* Cube)
    {
        return IsValid(Cube) && Cube->GetStackMass() > Cube->GetMassKg() &&
            StackBounds.IsInside(Cube->GetActorLocation());
    });
}

void ATrainingChamberGameMode::PressButton()
{
    if (!IsButtonLatched() && IsValid(ButtonVisual))
    {
        Circuit.PressButton();
        ButtonVisual->SetRelativeLocation(FVector(8.0, 0.0, 0.0));
        UE_LOG(LogTrainingChamber, Display, TEXT("Door circuit armed by wall button."));
    }
}

bool ATrainingChamberGameMode::IsExitEnabled() const
{
    return Layout && Layout->UsesPlateBalance() ? Balance.IsBalanced() : Circuit.GetState().ExitEnabled;
}

void ATrainingChamberGameMode::ObserveCircuit(const ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    for (int32 Index = 0; Index < Plates.Num(); ++Index)
    {
        PlateLoads[Index] = GetPlateMass(Player, Index);
    }
    if (Layout->UsesPlateBalance())
    {
        Balance.Observe({Layout->BalanceTotalKg, 0.01f}, {PlateLoads.GetData(), static_cast<size_t>(PlateLoads.Num())}, DeltaSeconds);
        for (int32 Index = 0; Index < Plates.Num(); ++Index)
        {
            SetPlatePressed(Index, PlateLoads[Index] > 0.01f);
        }
        return;
    }
    const auto DoorGate = Layout->UsesParallelPlates() ? CircuitRules::Gate::ButtonAndParallelPlates :
        Layout->bRequiresStack ? CircuitRules::Gate::StackLatch :
        (Layout->bRequiresButton ? CircuitRules::Gate::ButtonAndPlate : CircuitRules::Gate::Plate);
    Circuit.Observe({Layout->PlateActivationMassKg, Layout->PlateMaximumMassKg, DoorGate}, GetMeasuredMass(),
        Layout->bRequiresStack && HasStackAtExit(), DeltaSeconds, GetMeasuredMass(1));
    SetPlatePressed(0, Circuit.GetState().PlatePressed);
    if (Layout->UsesParallelPlates())
    {
        SetPlatePressed(1, Circuit.GetState().AlternatePlatePressed);
        UpdateRelayTraces();
    }
}

float ATrainingChamberGameMode::GetPlateMass(const ATrainingChamberCharacter& Player, int32 Index) const
{
    const UBoxComponent* Plate = Plates[Index].Body;
    const FSceneBlock& Block = Layout->GetPlateAt(Index);
    const FVector AllowedOffset = Block.Size * 0.5 - FVector(10.0);
    const double PlateTop = Block.Center.Z + Block.Size.Z * 0.5;
    const UBoxComponent* Held = Player.GetHeldBody();
    const UPrimitiveComponent* PlayerSupport = Player.GetCharacterMovement()->IsMovingOnGround()
        ? Player.GetMovementBase() : nullptr;
    while (PlayerSupport)
    {
        const APuzzleCube* Cube = Cast<APuzzleCube>(PlayerSupport->GetOwner());
        if (!Cube || !Cube->GetSupport())
        {
            break;
        }
        PlayerSupport = Cube->GetSupport();
    }
    const float PlayerLoad = Layout->PlayerMassKg + (Held && Course.Current != Segment::Sandbox ? Held->GetMass() : 0.0f);
    const auto OnPlate = [&](UBoxComponent& Body)
    {
        const FBoxSphereBounds& Bounds = Body.Bounds;
        const FVector Offset = Bounds.Origin - Block.Center;
        const double BottomZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
        return FMath::Abs(Offset.X) < AllowedOffset.X && FMath::Abs(Offset.Y) < AllowedOffset.Y &&
            FMath::IsNearlyEqual(BottomZ, PlateTop, 4.0) &&
            FMath::Abs(Body.GetPhysicsLinearVelocity().Z) < 30.0;
    };
    float MassKg = PlayerSupport == Plate ? PlayerLoad : 0.0f;
    for (const APuzzleCube* Cube : Cubes)
    {
        if (!IsValid(Cube) || Cube->GetBody() == Held)
        {
            continue;
        }
        if (Cube->GetSupport() == Plate)
        {
            MassKg += Cube->GetStackMass();
            continue;
        }
        UBoxComponent* Body = Cube->GetBody();
        if (!Cube->GetSupport() && OnPlate(*Body))
        {
            MassKg += Cube->GetStackMass();
            if (PlayerSupport == Body)
            {
                MassKg += PlayerLoad;
            }
        }
    }
    if (IsValid(Dev20) && Dev20->GetBody() != Held && OnPlate(*Dev20->GetBody()))
    {
        MassKg += Dev20MassKg + (PlayerSupport == Dev20->GetBody() ? PlayerLoad : 0.0f);
    }
    return MassKg;
}

bool ATrainingChamberGameMode::IsDoorOpen() const
{
    return IsValid(Door) && !DoorClamp.HasObject() &&
        Door->GetComponentLocation().Z >= Layout->Door.Center.Z + DoorTravel - 2.0;
}

void ATrainingChamberGameMode::SetPlatePressed(int32 Index, bool bPressed)
{
    FTrainingPressurePlate& Plate = Plates[Index];
    if (Plate.bPressed == bPressed)
    {
        return;
    }
    Plate.bPressed = bPressed;
    if (IsValid(Plate.Visual))
    {
        Plate.Visual->SetRelativeLocation(FVector(0.0, 0.0, bPressed ? -4.0 : 0.0));
    }
    if (IsValid(Plate.Indicator))
    {
        Plate.Indicator->SetIntensity(bPressed ? Layout->PressedLightIntensity : Layout->PlateLight.Intensity);
    }
    UE_LOG(LogTrainingChamber, Display, TEXT("Pressure plate %d %s."), Index + 1, bPressed ? TEXT("pressed") : TEXT("released"));
}

void ATrainingChamberGameMode::ReturnCube(ATrainingChamberCharacter& Player, int32 Index, bool bUseDispenser)
{
    if (APuzzleCube* Cube = Cubes[Index]; IsValid(Cube))
    {
        if (DoorClamp.Holds(Cube->GetBody()))
        {
            DoorClamp.Release();
        }
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
    DoorClamp.Release();
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
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
    Player.ReleaseHeldObject();
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        ReturnCube(Player, Index);
    }
    Circuit.Reset();
    Balance.Reset();
    HintReader = {};
    for (int32 Index = 0; Index < Plates.Num(); ++Index)
    {
        PlateLoads[Index] = 0.0f;
        SetPlatePressed(Index, false);
    }
    UpdateRelayTraces();
    if (IsValid(Door))
    {
        Door->SetWorldLocation(Layout->Door.Center, false, nullptr, ETeleportType::TeleportPhysics);
    }
    if (ButtonVisual)
    {
        ButtonVisual->SetRelativeLocation(FVector::ZeroVector);
    }
    bReachedExit = false;
}

#if WITH_DEV_AUTOMATION_TESTS
void ATrainingChamberGameMode::VerifyEntryChamber(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    CourseTestTime += DeltaSeconds;
    const auto Fail = [this, &Player](const TCHAR* Reason)
    {
        UE_LOG(LogTrainingChamber, Error, TEXT("Entry test failed at %s step %d: %s. Mass %.2f, player %s."),
            *WriteSegment(Course.Current), CourseTestStep, Reason, GetMeasuredMass(), *Player.GetActorLocation().ToCompactString());
        bVerifyEntryCourse = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    if (CourseTestTime > 9.0f)
    {
        Fail(TEXT("Timed out waiting for gameplay"));
        return;
    }
    const bool bWeight = Course.Current == Segment::Weight;
    const int32 ExitPlateIndex = Layout->UsesParallelPlates() &&
        FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryAlternatePlate")) ? 1 : 0;
    const bool bAlternateWeight = bWeight && FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryAlternateWeight"));
    const int32 RequiredCubes = Course.Current == Segment::Stairs ? 3 : bAlternateWeight ? 4 : bWeight ? 2 : 1;
    if (Cubes.Num() < RequiredCubes ||
        Cubes.ContainsByPredicate([](const APuzzleCube* Cube) { return !IsValid(Cube); }))
    {
        Fail(TEXT("Missing cubes from the test layout"));
        return;
    }
    if (CourseTestStep == 0 && CourseTestTime >= 0.3f && !bVerificationScreenshotRequested &&
        FParse::Param(FCommandLine::Get(), TEXT("CaptureEntryCourse")))
    {
        FScreenshotRequest::RequestScreenshot(TEXT("Entry-") + WriteSegment(Course.Current), false, false);
        bVerificationScreenshotRequested = true;
        return;
    }
    const auto Step = [this](int32 Next)
    {
        CourseTestStep = Next;
        CourseTestTime = 0.0f;
    };
    const auto AimFrom = [&Player](const FVector& From, const FVector& Target)
    {
        const FVector Eye = From + FVector(0.0, 0.0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Target - Eye).Rotation(), From));
    };
    if (bVerifyDev20 && Dev20TestPhase < 2)
    {
        if (Dev20TestPhase == 0 && CourseTestTime >= 0.25f)
        {
            TArray<AActor*> Units;
            UGameplayStatics::GetAllActorsOfClass(this, ADev20Actor::StaticClass(), Units);
            if (Units.Num() != 1 || Player.GetHeldDev20() != Dev20 ||
                !FMath::IsNearlyEqual(Dev20->GetBody()->GetMass(), Dev20MassKg, 0.05f))
            {
                Fail(TEXT("Exactly one 34 kg DEV-20 must arrive in the grip"));
                return;
            }
            Player.Interact();
            if (Dev20Stage == Dev20Step::Button && FParse::Param(FCommandLine::Get(), TEXT("VerifyDev20Recovery")))
            {
                const FVector Below = Layout->GetCubeBounds().Min - FVector(0, 0, 100);
                Dev20->RestoreAt(Below);
                UE_LOG(LogTrainingChamber, Display, TEXT("DEV-20: exercising loss outside the Button chamber bounds."));
                return;
            }
            Dev20->RestoreAt(FVector(Layout->Plate.Center.X, Layout->Plate.Center.Y,
                Layout->GetPlateTop() + Dev20->GetBody()->GetUnscaledBoxExtent().Z + 0.2));
            const FVector ButtonStart = Layout->Button.Center - FVector(156.0, 0.0, 0.0);
            AimFrom(FVector(ButtonStart.X, ButtonStart.Y, Layout->Spawn.Z), Layout->Button.Center);
            Player.Interact();
            Dev20TestPhase = 1;
            CourseTestTime = 0.0f;
        }
        else if (Dev20TestPhase == 1 && CourseTestTime >= 0.35f)
        {
            if (!IsButtonLatched() || !FMath::IsNearlyEqual(GetMeasuredMass(), Dev20MassKg, 0.05f) || IsExitEnabled())
            {
                Fail(TEXT("The armed chamber must weigh and reject a 34 kg DEV-20"));
                return;
            }
            UE_LOG(LogTrainingChamber, Display, TEXT("DEV-20: %s measured and rejected 34 kg."),
                *WriteSegment(Course.Current));
            Dev20->RestoreAt(FVector(Layout->Door.Center.X - 360.0, Layout->Door.Center.Y, 20.0));
            Dev20TestPhase = 2;
            CourseTestTime = 0.0f;
        }
        return;
    }
    const auto Pickup = [&Player, &AimFrom, this](APuzzleCube& Cube, FVector Approach)
    {
        const FVector Body = Cube.GetActorLocation();
        const FVector Start = Body - Approach * 156.0;
        AimFrom(FVector(Start.X, Start.Y, Layout->Spawn.Z), Body);
        Player.Interact();
        return Player.GetHeldCube() == &Cube;
    };
    const auto AimHold = [&AimFrom, this](const FVector& Target, FVector Approach)
    {
        const double Height = Target.Z - Layout->Spawn.Z - Abandoned::MovementProfile::CameraHeight;
        const double Forward = FMath::Sqrt(FMath::Max(0.0, FMath::Square(APuzzleCharacter::HoldDistanceCm) - FMath::Square(Height)));
        const FVector Start = Target - Approach * Forward;
        AimFrom(FVector(Start.X, Start.Y, Layout->Spawn.Z), Target);
    };
    const auto Aligned = [&Player](const UBoxComponent* Support)
    {
        const APuzzleCube* Held = Player.GetHeldCube();
        return Held && Player.GetPlacementPreview().GetPreviewSupport() == Support && !Player.GetPlacementPreview().IsPreviewBlocked() &&
            Held->GetBody()->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(28.0);
    };

    APuzzleCube& First = *Cubes[0];
    if (Course.Current == Segment::Stairs)
    {
        APuzzleCube& Base = *Cubes[1];
        APuzzleCube& Low = *Cubes[2];
        if (CourseTestStep == 0 && CourseTestTime >= 0.35f)
        {
            if (!Pickup(First, FVector::ForwardVector))
            {
                Fail(TEXT("Could not pick up the small cube"));
                return;
            }
            AimHold(Base.GetActorLocation() + FVector(0.0, 0.0, Base.GetHalfSize() + First.GetHalfSize() + 40.0),
                FVector::RightVector);
            Step(1);
        }
        else if (CourseTestStep == 1 && Aligned(Base.GetBody()))
        {
            Player.Interact();
            if (First.GetSupport() != Base.GetBody())
            {
                Fail(TEXT("Small cube did not snap to the large one"));
                return;
            }
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Low.GetActorLocation() + FVector(-6.0, 0.0,
                Low.GetHalfSize() + Abandoned::MovementProfile::StandingHalfHeight + 1.0)));
            Step(2);
        }
        else if (CourseTestStep == 2 && Player.GetMovementBase() == Low.GetBody())
        {
            Player.AddMovementInput(FVector::ForwardVector);
            if (Player.GetActorLocation().X >= Low.GetActorLocation().X + 7.0)
            {
                Player.Jump();
                Step(3);
            }
        }
        else if (CourseTestStep == 3)
        {
            Player.AddMovementInput(FVector::ForwardVector * 0.35);
            if (Player.GetMovementBase() == First.GetBody())
            {
                Player.StopJumping();
                Player.Jump();
                Step(4);
            }
        }
        else if (CourseTestStep == 4)
        {
            Player.AddMovementInput(FVector::ForwardVector);
        }
        return;
    }

    if (CourseTestStep == 0 && CourseTestTime >= 0.35f)
    {
        const FVector Position = Layout->Button.Center - FVector(156.0, 0.0, 0.0);
        AimFrom(FVector(Position.X, Position.Y, Layout->Spawn.Z), Layout->Button.Center);
        Player.Interact();
        if (!IsButtonLatched() || IsExitEnabled() || !Pickup(First, FVector::ForwardVector))
        {
            Fail(TEXT("Wall button should arm the circuit and leave the unloaded door closed"));
            return;
        }
        const FSceneBlock& ExitPlate = Layout->GetPlateAt(ExitPlateIndex);
        const FVector HeldTarget = ExitPlate.Center + FVector(0.0, 0.0,
            ExitPlate.Size.Z * 0.5 + First.GetHalfSize() + 40.0);
        AimHold(HeldTarget, FVector::ForwardVector);
        Step(1);
    }
    else if (CourseTestStep == 1 && Aligned(GetPlate(ExitPlateIndex)))
    {
        Player.Interact();
        if (First.GetSupport() != GetPlate(ExitPlateIndex))
        {
            Fail(TEXT("Cube did not snap to the plate"));
            return;
        }
        if (ExitPlateIndex == 1)
        {
            UE_LOG(LogTrainingChamber, Display, TEXT("Entry alternate solution: cube snapped to plate B."));
        }
        Step(2);
    }
    else if (CourseTestStep == 2 && bWeight && CourseTestTime >= 0.35f)
    {
        APuzzleCube& Second = *Cubes[bAlternateWeight ? 2 : 1];
        if (GetMeasuredMass() != 10.0f || IsExitEnabled() || !Pickup(Second, FVector::ForwardVector))
        {
            Fail(TEXT("Weight lock must reject 10 kg even with its circuit armed"));
            return;
        }
        const FVector HeldTarget = First.GetActorLocation() + FVector(0.0, 0.0,
            First.GetHalfSize() + Second.GetHalfSize() + 35.0);
        AimHold(HeldTarget, FVector::ForwardVector);
        Step(3);
    }
    else if (CourseTestStep == 3 && Aligned(First.GetBody()))
    {
        Player.Interact();
        if (Cubes[bAlternateWeight ? 2 : 1]->GetSupport() != First.GetBody())
        {
            Fail(TEXT("Second cube did not snap to the stack"));
            return;
        }
        Step(bAlternateWeight ? 6 : 4);
    }
    else if (CourseTestStep == 6 && CourseTestTime >= 0.35f)
    {
        APuzzleCube& Middle = *Cubes[2];
        APuzzleCube& Third = *Cubes[3];
        if (GetMeasuredMass() != 15.0f || IsExitEnabled() || !Pickup(Third, FVector::ForwardVector))
        {
            Fail(TEXT("Weight lock must reject 15 kg"));
            return;
        }
        AimHold(Middle.GetActorLocation() + FVector(0.0, 0.0,
            Middle.GetHalfSize() + Third.GetHalfSize() + 35.0), FVector::ForwardVector);
        Step(7);
    }
    else if (CourseTestStep == 7 && Aligned(Cubes[2]->GetBody()))
    {
        Player.Interact();
        if (Cubes[3]->GetSupport() != Cubes[2]->GetBody() || Cubes[3]->CanSupportCube(*Cubes[2]))
        {
            Fail(TEXT("The third cube must snap on top, and the stack must reject a fourth cube"));
            return;
        }
        UE_LOG(LogTrainingChamber, Display, TEXT("Entry test: alternate solution assembled, 10 + 5 + 5 kg."));
        Step(4);
    }
    else if (((CourseTestStep == 2 && !bWeight) || CourseTestStep == 4) && IsDoorOpen())
    {
        if (bWeight && !bVerifyDev20 && !bVerifiedWeightReset)
        {
            Player.ResetChamber();
            if (IsButtonLatched() || GetMeasuredMass() != 0.0f || IsExitEnabled() ||
                !Door->GetComponentLocation().Equals(Layout->Door.Center))
            {
                Fail(TEXT("Reset did not restore chamber 03 after solving its weight lock"));
                return;
            }
            bVerifiedWeightReset = true;
            UE_LOG(LogTrainingChamber, Display, TEXT("Entry test: reset restored the weight lock, then the solution will be repeated."));
            Step(0);
            return;
        }
        UE_LOG(LogTrainingChamber, Display, TEXT("Entry test: %s accepted %.0f kg after the wall button."),
            *WriteSegment(Course.Current), GetMeasuredMass(ExitPlateIndex));
        Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(Layout->Door.Center.X - 175.0,
            Layout->Door.Center.Y, Layout->Spawn.Z)));
        Step(5);
    }
    else if (CourseTestStep == 5)
    {
        Player.AddMovementInput(FVector::ForwardVector);
        if (bVerifyDev20 && Layout->GetExitBounds().IsInside(Player.GetActorLocation()))
        {
            const FVector Item = Dev20->GetActorLocation();
            AimFrom(FVector(Item.X - 140.0, Item.Y, Layout->Spawn.Z), Item);
            Player.Interact();
            if (Player.GetHeldDev20() != Dev20)
            {
                Fail(TEXT("Could not retrieve DEV-20 after solving the chamber"));
                return;
            }
            if (AController* Controller = Player.GetController())
            {
                Controller->SetControlRotation(FRotator(-14, 0, 0));
            }
            UE_LOG(LogTrainingChamber, Display, TEXT("DEV-20: %s held the player at the exit until the unit was retrieved."),
                *WriteSegment(Course.Current));
            Step(8);
        }
    }
    else if (CourseTestStep == 8 && bVerifyDev20)
    {
        if (Player.GetHeldDev20() != Dev20)
        {
            Fail(TEXT("Lost DEV-20 on the final approach to the exit"));
            return;
        }
        Player.AddMovementInput(FVector::ForwardVector);
    }
}

void ATrainingChamberGameMode::VerifyChamber(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    if (Cubes.Num() != 3 || !IsValid(Cubes[0]) || !IsValid(Cubes[1]) || !IsValid(Cubes[2]))
    {
        UE_LOG(LogTrainingChamber, Error, TEXT("Verification needs the three default training cubes."));
        bVerifyChamber = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    APuzzleCube& Small = *Cubes[0];
    APuzzleCube& Large = *Cubes[1];
    APuzzleCube& Step = *Cubes[2];
    UBoxComponent* Cube = Small.GetBody();
    VerificationElapsed += DeltaSeconds;
    const auto Advance = [this](EVerificationPhase Phase, const TCHAR* Message)
    {
        UE_LOG(LogTrainingChamber, Display, TEXT("Verification: %s"), Message);
        VerificationElapsed = 0.0f;
        VerificationPhase = Phase;
    };
    const auto Fail = [this, Cube, &Player](const TCHAR* Reason)
    {
        UE_LOG(LogTrainingChamber, Error, TEXT("Verification failed (phase %d): %s. Cube %s, door Z %.1f, plate %s, player %s."),
            static_cast<int32>(VerificationPhase), Reason, *Cube->GetComponentLocation().ToCompactString(),
            Door->GetComponentLocation().Z, IsPlatePressed() ? TEXT("pressed") : TEXT("released"),
            *Player.GetActorLocation().ToCompactString());
        bVerifyChamber = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto AimFrom = [&Player](FVector Here, FVector Target)
    {
        const FVector Eye = Here + FVector(0.0, 0.0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Target - Eye).Rotation(), Here));
    };
    const auto AimToHold = [this, &AimFrom](const FVector& Target, const FVector& Approach)
    {
        const double Height = Target.Z - (Layout->Spawn.Z + Abandoned::MovementProfile::CameraHeight);
        const double Forward = FMath::Sqrt(FMath::Max(0.0, FMath::Square(APuzzleCharacter::HoldDistanceCm) - FMath::Square(Height)));
        const FVector Here = Target - Approach * Forward;
        AimFrom(FVector(Here.X, Here.Y, Layout->Spawn.Z), Target);
    };
    const FVector SmallStart = Layout->CubeSpawns[0].Center;
    APlayerController* Controller = Cast<APlayerController>(Player.GetController());
    if (!Controller)
    {
        Fail(TEXT("Missing player controller"));
        return;
    }

    if (VerificationElapsed > 8.0f)
    {
        Fail(TEXT("Timed out waiting for a physical transition"));
        return;
    }

    switch (VerificationPhase)
    {
    case EVerificationPhase::Initial:
        if (VerificationElapsed >= 0.35f)
        {
            if (IsPlatePressed() || Door->GetComponentLocation() != Layout->Door.Center)
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
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(Layout->Plate.Center.X, Layout->Plate.Center.Y,
                Layout->GetPlateTop() + Abandoned::MovementProfile::StandingHalfHeight + 2.0)));
            Advance(EVerificationPhase::StandOnPlate, TEXT("Closed door confirmed. Standing on the plate."));
        }
        break;
    case EVerificationPhase::StandOnPlate:
        if (IsPlatePressed())
        {
            if (Player.GetMovementBase() != GetPlate() || !FMath::IsNearlyEqual(GetPlateMass(Player, 0), Layout->PlayerMassKg))
            {
                Fail(TEXT("The grounded player should be the only load on the plate"));
                return;
            }
            const FVector OnPlate = Player.GetActorLocation();
            Player.ResetChamber();
            const FTrainingPressurePlate& Plate = Plates[0];
            if (IsPlatePressed() || IsExitEnabled() || !Plate.Visual->GetRelativeLocation().IsNearlyZero() ||
                (Plate.Indicator && !FMath::IsNearlyEqual(Plate.Indicator->Intensity, Layout->PlateLight.Intensity)))
            {
                Fail(TEXT("Reset left the pressure plate active"));
                return;
            }
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, OnPlate + FVector(0.0, 0.0, 2.0)));
            Advance(EVerificationPhase::StandAfterReset, TEXT("Reset released the loaded plate and restored its visual and indicator."));
        }
        break;
    case EVerificationPhase::StandAfterReset:
        if (IsPlatePressed())
        {
            Player.Jump();
            Advance(EVerificationPhase::JumpOffPlate, TEXT("The plate can be pressed again after reset."));
        }
        break;
    case EVerificationPhase::JumpOffPlate:
        if (Player.GetCharacterMovement()->IsFalling() && !IsPlatePressed())
        {
            Player.StopJumping();
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
            Controller->ConsoleCommand(TEXT("Input.+key Mouse2D X=0 Y=1"), false);
            Advance(EVerificationPhase::LookUp, TEXT("Jumping unloaded the plate. Testing upward mouse movement."));
        }
        break;
    case EVerificationPhase::LookUp:
        if (VerificationElapsed >= 0.15f)
        {
            if (Controller->GetControlRotation().GetNormalized().Pitch <= 0.0)
            {
                Controller->ConsoleCommand(TEXT("Input.-key Mouse2D"), false);
                Fail(TEXT("Upward mouse movement did not raise the view"));
                return;
            }
            Controller->SetControlRotation(FRotator::ZeroRotator);
            Controller->ConsoleCommand(TEXT("Input.+key Mouse2D X=0 Y=-1"), false);
            Advance(EVerificationPhase::LookDown, TEXT("Upward mouse movement raised the view."));
        }
        break;
    case EVerificationPhase::LookDown:
        if (VerificationElapsed >= 0.15f)
        {
            Controller->ConsoleCommand(TEXT("Input.-key Mouse2D"), false);
            if (Controller->GetControlRotation().GetNormalized().Pitch >= 0.0)
            {
                Fail(TEXT("Downward mouse movement did not lower the view"));
                return;
            }
            UE_LOG(LogTrainingChamber, Display, TEXT("Verification: Downward mouse movement lowered the view."));
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Small)
            {
                Fail(TEXT("Pickup trace or physics handle missed the cube"));
                return;
            }
            const FVector Target(Layout->Plate.Center.X, Layout->Plate.Center.Y,
                Layout->GetPlateTop() + Small.GetHalfSize() + 42.0);
            AimToHold(Target, FVector::ForwardVector);
            Advance(EVerificationPhase::Carry, TEXT("Cube picked up with the gameplay trace and physics handle."));
        }
        break;
    case EVerificationPhase::Carry:
        if (Player.GetHeldCube() != &Small || IsPlatePressed())
        {
            Fail(TEXT("The held cube must stay in the grip and leave the plate unpowered"));
            return;
        }
        if (FVector::DistSquared2D(Cube->GetComponentLocation(), Layout->Plate.Center) < FMath::Square(16.0) &&
            Cube->GetComponentLocation().Z > Layout->GetPlateTop() + Small.GetHalfSize() &&
            Cube->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(30.0))
        {
            if (!Player.GetPlacementPreview().IsVisible() || Player.GetPlacementPreview().GetPreviewSupport() != GetPlate() ||
                Player.GetPlacementPreview().GetCollisionEnabled() != ECollisionEnabled::NoCollision)
            {
                Fail(TEXT("Expected a collision-free placement ghost on the plate"));
                return;
            }
            if (!bVerificationScreenshotRequested && FParse::Param(FCommandLine::Get(), TEXT("CapturePlacementGhost")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("PlacementGhost"), false, false);
                bVerificationScreenshotRequested = true;
                break;
            }
            const FVector Expected = Player.GetPlacementPreview().GetComponentLocation();
            Player.Interact();
            if (Small.GetSupport() != GetPlate() || Cube->IsSimulatingPhysics() ||
                !Small.GetActorLocation().Equals(Expected, 0.1) || Player.GetPlacementPreview().IsVisible())
            {
                Fail(TEXT("Released cube did not snap to the plate"));
                return;
            }
            Advance(EVerificationPhase::Open, TEXT("Held cube snapped to the plate at the ghost position."));
        }
        break;
    case EVerificationPhase::Open:
        if (IsPlatePressed() && IsDoorOpen())
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator,
                FVector(Layout->Door.Center.X - 180.0, Layout->Door.Center.Y, Layout->Spawn.Z)));
            Advance(EVerificationPhase::Cross, TEXT("Settled cube pressed the plate and opened the door."));
        }
        break;
    case EVerificationPhase::Cross:
        if (bReachedExit)
        {
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Small || Small.GetSupport())
            {
                Fail(TEXT("Could not remove the cube from the plate"));
                return;
            }
            Advance(EVerificationPhase::Close, TEXT("Player walked through the door and picked the cube off the plate."));
        }
        else
        {
            Player.AddMovementInput(FVector::ForwardVector);
        }
        break;
    case EVerificationPhase::Close:
        if (!IsPlatePressed() && FMath::IsNearlyEqual(Door->GetComponentLocation().Z, Layout->Door.Center.Z, 0.1))
        {
            Controller->SetControlRotation(FRotator::ZeroRotator);
            Controller->ConsoleCommand(TEXT("Input.+key LeftMouseButton"), false);
            Advance(EVerificationPhase::Throw, TEXT("Removing the cube closed the door. Testing left mouse throw."));
        }
        break;
    case EVerificationPhase::Throw:
        if (!Player.GetHeldCube())
        {
            Controller->ConsoleCommand(TEXT("Input.-key LeftMouseButton"), false);
            if (Small.GetSupport() || Player.GetPlacementPreview().IsVisible() ||
                Cube->GetPhysicsLinearVelocity().X < GetDefault<UPuzzleInteractionSettings>()->GetThrowImpulse() / Small.GetMassKg() * 0.75)
            {
                Fail(TEXT("Left mouse did not throw the free cube forward"));
                return;
            }
            Player.ResetChamber();
            if (bReachedExit || Player.GetHeldCube() || FVector::DistSquared(SmallStart, Cube->GetComponentLocation()) > 1.0)
            {
                Fail(TEXT("Reset did not restore the initial chamber state"));
                return;
            }
            Small.SetActorLocation(FVector(SmallStart.X, SmallStart.Y, -420.0), false, nullptr, ETeleportType::TeleportPhysics);
            Advance(EVerificationPhase::Return, TEXT("Left mouse threw the cube forward. Reset restored player and cube."));
        }
        break;
    case EVerificationPhase::Return:
        if (FVector::DistSquared(SmallStart, Cube->GetComponentLocation()) < FMath::Square(10.0))
        {
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Small)
            {
                Fail(TEXT("Could not pick up the returned small cube"));
                return;
            }
            const FVector Target = Large.GetActorLocation() + FVector(0.0, 0.0,
                Large.GetHalfSize() + Small.GetHalfSize() + 40.0);
            AimToHold(Target, FVector::RightVector);
            Advance(EVerificationPhase::CarryToStack, TEXT("Lost cube returned. Carrying it to the large cube."));
        }
        break;
    case EVerificationPhase::CarryToStack:
        if (FVector::DistSquared2D(Small.GetActorLocation(), Large.GetActorLocation()) < FMath::Square(14.0) &&
            Cube->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(25.0))
        {
            if (!Player.GetPlacementPreview().IsVisible() || Player.GetPlacementPreview().GetPreviewSupport() != Large.GetBody())
            {
                Fail(TEXT("Expected a placement ghost on the stack"));
                return;
            }
            Player.Interact();
            if (Small.GetSupport() != Large.GetBody() || Large.GetStackMass() != 15.0f)
            {
                Fail(TEXT("Small cube did not snap to the large cube and add its mass"));
                return;
            }
            const FVector Start = Step.GetActorLocation() + FVector(-6.0, 0.0,
                Step.GetHalfSize() + Abandoned::MovementProfile::StandingHalfHeight + 1.0);
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Start));
            Advance(EVerificationPhase::StartAscent, TEXT("Small cube snapped to large cube, total mass 15 kg."));
        }
        break;
    case EVerificationPhase::StartAscent:
        if (Player.GetMovementBase() == Step.GetBody())
        {
            Player.AddMovementInput(FVector::ForwardVector);
            if (Player.GetActorLocation().X >= Step.GetActorLocation().X + 7.0)
            {
                Player.Jump();
                Advance(EVerificationPhase::JumpToStack, TEXT("Player jumped toward the stack after a short run-up."));
            }
        }
        break;
    case EVerificationPhase::JumpToStack:
        Player.AddMovementInput(FVector::ForwardVector * 0.35);
        if (Player.GetMovementBase() == Cube)
        {
            Player.StopJumping();
            Player.Jump();
            Advance(EVerificationPhase::JumpToLedge, TEXT("Player landed on the stack and jumped toward the ledge."));
        }
        break;
    case EVerificationPhase::JumpToLedge:
        Player.AddMovementInput(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Large.GetActorLocation().X + 140.0 &&
            Player.GetCharacterMovement()->IsMovingOnGround() &&
            Player.GetActorLocation().Z > Small.GetActorLocation().Z + 145.0)
        {
            Advance(EVerificationPhase::RemoveSupport, TEXT("Player climbed from the stack onto the practice ledge."));
        }
        break;
    case EVerificationPhase::RemoveSupport:
        {
            const FVector Current = Large.GetActorLocation();
            AimFrom(FVector(Current.X, Current.Y - 155.0, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Large || Small.GetSupport() || !Cube->IsSimulatingPhysics())
            {
                Fail(TEXT("Picking up the lower block did not release its upper block"));
                return;
            }
            ResetChamber(Player);
            for (int32 Index = 0; Index < Cubes.Num(); ++Index)
            {
                if (Cubes[Index]->GetSupport() || !Cubes[Index]->GetBody()->IsSimulatingPhysics() ||
                    !Cubes[Index]->GetActorLocation().Equals(Layout->CubeSpawns[Index].Center, 1.0))
                {
                    Fail(TEXT("Reset did not free and restore all cubes"));
                    return;
                }
            }
            UE_LOG(LogTrainingChamber, Display,
                TEXT("Verification passed: player weight, placement ghost, throw, staircase, detach and reset."));
            bVerifyChamber = false;
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif

bool ATrainingChamberGameMode::UseDevice(APuzzleCharacter&, UPrimitiveComponent& Target)
{
    if (&Target == HintTerminal)
    {
        ReadNextHint();
        return true;
    }
    if (&Target == Button)
    {
        PressButton();
        return true;
    }
    return false;
}

const TCHAR* ATrainingChamberGameMode::DevicePrompt(const UPrimitiveComponent& Target) const
{
    if (&Target == HintTerminal)
    {
        return TEXT("[E] Read hint terminal");
    }
    if (&Target == Button)
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
        ? TEXT("WASD / mouse   Space jump   E interact   LMB throw   R reset   M menu   |   HINTS at entrance")
        : TEXT("WASD / mouse   Space jump   E pick up / place   LMB throw   R reset   M test menu");
    DrawText(Controls, FLinearColor::White, 24.0f, 52.0f);
    const bool bDoorObstructed = Chamber->HasClampedObject() && !Chamber->HasReachedExit();
    const FString Status = Chamber->HasReachedExit()
        ? (Layout->UsesPlateBalance() ? TEXT("Balance chamber complete") : TEXT("Entry course complete")) :
        (bDoorObstructed ? TEXT("Door obstructed. Restore power to release.") :
        Chamber->IsDoorOpen() ? TEXT("Exit open") :
        (Chamber->IsExitEnabled() ? TEXT("Exit opening") : Layout->Objective));
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
