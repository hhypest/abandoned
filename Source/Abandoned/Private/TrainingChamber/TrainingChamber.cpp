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
#include "Verification/EntryChamberVerification.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"
#include "Expedition/Dev20Actor.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Misc/CommandLine.h"
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
using Abandoned::Infrastructure::Expedition::UnrealCheckpointStore;
using Abandoned::Infrastructure::Expedition::WriteSegment;

using FDecorGroups = TMap<UMaterialInterface*, TArray<FTransform>>;

UStaticMeshComponent* SpawnWeightShell(UWorld& World, UStaticMesh* Mesh, TConstArrayView<FSceneBlock> Room)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Training_WeightShellBody);
    const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
    if (!Body)
    {
        return nullptr;
    }
    const auto& Boxes = Body->AggGeom.BoxElems;
    int32 Index = 0;
    for (const FSceneBlock& Block : Room)
    {
        if (Block.bCollision)
        {
            if (!Boxes.IsValidIndex(Index))
            {
                return nullptr;
            }
            const FKBoxElem& Box = Boxes[Index++];
            if (!Box.Center.Equals(Block.Center) || !FVector(Box.X, Box.Y, Box.Z).Equals(Block.Size)
                || !Box.Rotation.IsNearlyZero())
            {
                return nullptr;
            }
        }
    }
    if (Index != Boxes.Num() || Index != Body->AggGeom.GetElementCount())
    {
        return nullptr;
    }
    AActor* Owner = World.SpawnActor<AActor>();
    if (!Owner)
    {
        return nullptr;
    }
    auto* Shell = NewObject<UStaticMeshComponent>(Owner);
    Owner->SetRootComponent(Shell);
    Owner->AddInstanceComponent(Shell);
    Shell->SetMobility(EComponentMobility::Static);
    Shell->SetStaticMesh(Mesh);
    Shell->SetVisibility(false);
    Shell->SetCastShadow(false);
    Shell->SetCollisionProfileName(TEXT("BlockAll"));
    Shell->RegisterComponent();
    return Shell;
}

bool SpawnRoomDecor(UWorld& World, UStaticMesh& Mesh, const FDecorGroups& Groups)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Training_RegisterDecorInstances);
    AActor* Owner = World.SpawnActor<AActor>();
    if (!Owner)
    {
        return false;
    }
    for (const auto& Group : Groups)
    {
        auto* Instances = NewObject<UInstancedStaticMeshComponent>(Owner);
        Owner->AddInstanceComponent(Instances);
        if (USceneComponent* Root = Owner->GetRootComponent())
        {
            Instances->SetupAttachment(Root);
        }
        else
        {
            Owner->SetRootComponent(Instances);
        }
        Instances->SetMobility(EComponentMobility::Static);
        Instances->SetStaticMesh(&Mesh);
        if (Group.Key)
        {
            Instances->SetMaterial(0, Group.Key);
        }
        Instances->SetCollisionProfileName(TEXT("NoCollision"));
        Instances->SetCanEverAffectNavigation(false);
        Instances->AddInstances(Group.Value, false, false, false);
        Instances->RegisterComponent();
    }
    return true;
}
}

ATrainingChamberGameMode::ATrainingChamberGameMode()
{
    PlateCircuit = CreateDefaultSubobject<UTrainingPlateCircuitComponent>(TEXT("PlateCircuit"));
    DoorDrive = CreateDefaultSubobject<UTrainingDoorDriveComponent>(TEXT("DoorDrive"));
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

    UStaticMesh* Mesh = Abandoned::Blockout::LoadCube();
    if (!Mesh)
    {
        ErrorMessage = TEXT("Training chamber requires the Engine BasicShapes cube.");
        return;
    }

    auto* MeshLibrary = GetGameInstance()->GetSubsystem<UChamberMeshLibrary>();
    UStaticMesh* SmallCube = MeshLibrary->LoadForChamber(*GetWorld(), TEXT("/Game/Abandoned/FosTech/SM_Cube55.SM_Cube55"));
    UStaticMesh* LargeCube = MeshLibrary->LoadForChamber(*GetWorld(), TEXT("/Game/Abandoned/FosTech/SM_Cube80.SM_Cube80"));
    UStaticMesh* PlateBase = MeshLibrary->LoadForChamber(*GetWorld(), TEXT("/Game/Abandoned/FosTech/SM_PlateBase.SM_PlateBase"));
    UStaticMesh* PlateTop = MeshLibrary->LoadForChamber(*GetWorld(), TEXT("/Game/Abandoned/FosTech/SM_PlateTop.SM_PlateTop"));
    if (!SmallCube || !LargeCube || !PlateBase || !PlateTop)
    {
        ErrorMessage = TEXT("Training chamber requires the FosTech cube and plate meshes.");
        return;
    }

    UWorld& World = *GetWorld();
    Abandoned::Blockout::ApplyGravity(World);
    const auto Finishes = Abandoned::Scene::MakeFinishes(*this, SurfaceMaterial, Layout->Finishes);
    const bool bDecorLayout = Layout->IsA<UEntryStairsLayout>() || Layout->IsA<UEntryWeightLayout>();
    const bool bInstanceDecor = bDecorLayout && !FParse::Param(FCommandLine::Get(), TEXT("NoInstancedChamberDecor"));
    FDecorGroups DecorGroups;
    UStaticMeshComponent* Shell = nullptr;
    if (Layout->IsA<UEntryWeightLayout>())
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("CompoundWeightShell")))
        {
            UStaticMesh* BodyMesh = MeshLibrary->LoadForChamber(World, UChamberMeshLibrary::WeightShellPath);
            Shell = SpawnWeightShell(World, BodyMesh, Layout->Room);
            if (!Shell)
            {
                ErrorMessage = TEXT("Cooked Weight shell does not match the chamber layout.");
                return;
            }
        }
        UE_LOG(LogTrainingChamber, Log, TEXT("Weight shell mode=%s"), Shell ? TEXT("Compound") : TEXT("Separate"));
    }
    for (const FSceneBlock& Block : Layout->Room)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(Training_SolidRoomBlock, Block.bCollision);
        TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(Training_DecorativeRoomBlock, !Block.bCollision);
        if (Shell && Block.bCollision)
        {
            auto* Visual = NewObject<UStaticMeshComponent>(Shell->GetOwner());
            Shell->GetOwner()->AddInstanceComponent(Visual);
            Visual->SetupAttachment(Shell);
            Visual->SetMobility(EComponentMobility::Static);
            Visual->SetStaticMesh(Mesh);
            Visual->SetCollisionProfileName(TEXT("NoCollision"));
            Visual->SetRelativeLocation(Block.Center);
            Visual->SetRelativeScale3D(Block.Size / 100.0);
            Visual->RegisterComponent();
            Abandoned::Scene::SetFinish({nullptr, Visual}, Block, Finishes);
            continue;
        }
        if (bInstanceDecor && !Block.bCollision)
        {
            DecorGroups.FindOrAdd(Finishes.FindRef(Block.Finish)).Emplace(
                FQuat::Identity, Block.Center, Block.Size / 100.0);
            continue;
        }
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *Mesh, Block.Center, Block.Size,
            EComponentMobility::Static, Block.bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
    }
    if (!DecorGroups.IsEmpty() && !SpawnRoomDecor(World, *Mesh, DecorGroups))
    {
        ErrorMessage = TEXT("Could not create the training chamber decor.");
        return;
    }
    if (bDecorLayout)
    {
        UE_LOG(LogTrainingChamber, Log, TEXT("Room decor mode=%s groups=%d"),
            bInstanceDecor ? TEXT("instanced") : TEXT("separate"), DecorGroups.Num());
    }
    for (const FTrainingCubeSpawn& CubeSpawn : Layout->CubeSpawns)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Training_SpawnPuzzleCube);
        APuzzleCube* Cube = World.SpawnActor<APuzzleCube>(CubeSpawn.Center, FRotator::ZeroRotator);
        if (!Cube)
        {
            ErrorMessage = TEXT("Could not create a training chamber cube.");
            return;
        }
        Cube->Initialize(CubeSpawn.Type, CubeSpawn.Type == EPuzzleCubeType::Small ? *SmallCube : *LargeCube);
        Cubes.Add(Cube);
    }
    const auto DoorParts = Abandoned::Blockout::SpawnBox(World, *Mesh, Layout->Door.Center, Layout->Door.Size,
        EComponentMobility::Movable, TEXT("BlockAllDynamic"));
    Abandoned::Scene::SetFinish(DoorParts, Layout->Door, Finishes);
    PlateCircuit->Configure(*Layout, Course.Current != Segment::Sandbox);
    for (int32 Index = 0; Index < Layout->GetPlateCount(); ++Index)
    {
        const FSceneBlock& Block = Layout->GetPlateAt(Index);
        TRACE_CPUPROFILER_EVENT_SCOPE(Training_BuildPressurePlate);
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *PlateTop, Block.Center, Block.Size, EComponentMobility::Movable);
        if (!Parts.Collision || !Parts.Visual)
        {
            ErrorMessage = TEXT("Could not create the pressure plates.");
            return;
        }
        // Both mesh pivots share the centre of the unpressed 110 x 110 x 20 cm assembly.
        const FVector PlateScale = Block.Size / FVector(110.0, 110.0, 20.0);
        Parts.Visual->SetRelativeScale3D(PlateScale);
        AActor* PlateActor = Parts.Collision->GetOwner();
        auto* Base = NewObject<UStaticMeshComponent>(PlateActor);
        PlateActor->AddInstanceComponent(Base);
        Base->SetupAttachment(Parts.Collision);
        Base->SetMobility(EComponentMobility::Movable);
        Base->SetCollisionProfileName(TEXT("NoCollision"));
        Base->SetStaticMesh(PlateBase);
        Base->SetRelativeScale3D(PlateScale);
        Base->RegisterComponent();
        FSceneLight Light = Layout->PlateLight;
        Light.Position += Block.Center - Layout->Plate.Center;
        PlateCircuit->AddPlate(*Parts.Collision, *Parts.Visual, Abandoned::Scene::AddLight(World, Light));
        if (Layout->UsesPlateBalance() || Layout->UsesParallelPlates())
        {
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

    if (!IsValid(DoorParts.Collision) || (Layout->bRequiresButton && (!Button || !ButtonVisual)))
    {
        ErrorMessage = TEXT("Could not create the cube, pressure plate or door.");
        return;
    }
    DoorDrive->Bind(*DoorParts.Collision, Layout->Door.Center);

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
        TRACE_CPUPROFILER_EVENT_SCOPE(Training_AddLight);
        Abandoned::Scene::AddLight(World, Light);
    }
    SaveEntryCheckpoint();
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
    TRACE_CPUPROFILER_EVENT_SCOPE(Training_ArrivalReady);
#if WITH_DEV_AUTOMATION_TESTS
    const bool bVerifyCourse = UEntryChamberVerification::IsRequested(bDev20Run);
    if (Course.Current == Segment::Sandbox && UTrainingVerification::IsRequested())
    {
        UTrainingVerification::Attach(*this, Cubes, PlateCircuit->GetPlates());
    }
    else if (UChamberHintsVerification::IsRequested())
    {
        UChamberHintsVerification::Attach(*this);
    }
    else if (Course.Current == Segment::Button && UParallelCircuitVerification::IsRequested())
    {
        UParallelCircuitVerification::Attach(*this, Cubes, RelayTraces, RelayIdleMaterial, RelayPoweredMaterial);
    }
    else if (Course.Current == Segment::Balance && (UPlateBalanceVerification::IsStandaloneRequested() || bVerifyCourse))
    {
        UPlateBalanceVerification::Attach(*this, Cubes, PlateCircuit->GetPlates());
    }
    else if (bVerifyCourse && (Course.Current == Segment::Stairs || Course.Current == Segment::Button || Course.Current == Segment::Weight))
    {
        UEntryChamberVerification::Attach(*this, Course.Current, Cubes);
    }
#endif
    UDoorClampVerification::Attach(*this);
    UE_LOG(LogTrainingChamber, Display, TEXT("Training chamber ready. %s, %d cubes, plate released, exit closed."),
        *WriteSegment(Course.Current), Cubes.Num());
}

void ATrainingChamberGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
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
    if (!IsValid(GetDoor()))
    {
        DoorDrive->ReleaseClamp();
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

    PlateCircuit->Observe(*Player, Cubes, Dev20.Get(), DeltaSeconds);
    if (Layout->UsesParallelPlates())
    {
        UpdateRelayTraces();
    }
    ObserveHints(*Player, DeltaSeconds);
    if (DoorDrive->Move(*Player, Dev20.Get(), IsExitEnabled(), Course.Current != Segment::Sandbox, DeltaSeconds) ==
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
        AdvanceCourse(*Player);
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
    if (!IsButtonLatched() && IsValid(ButtonVisual))
    {
        PlateCircuit->PressButton();
        ButtonVisual->SetRelativeLocation(FVector(8.0, 0.0, 0.0));
        UE_LOG(LogTrainingChamber, Display, TEXT("Door circuit armed by wall button."));
    }
}

void ATrainingChamberGameMode::ReturnCube(ATrainingChamberCharacter& Player, int32 Index, bool bUseDispenser)
{
    if (APuzzleCube* Cube = Cubes[Index]; IsValid(Cube))
    {
        DoorDrive->ReleaseIfHolding(Cube->GetBody());
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
    DoorDrive->ReleaseClamp();
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
    PlateCircuit->Reset();
    HintReader = {};
    UpdateRelayTraces();
    DoorDrive->ReturnToClosedPosition();
    if (ButtonVisual)
    {
        ButtonVisual->SetRelativeLocation(FVector::ZeroVector);
    }
    bReachedExit = false;
}

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
