#include "TrainingChamber/ChamberAssembly.h"
#include "TrainingChamber/ChamberMeshLibrary.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "World/FosTech.h"
#include "World/SceneAssembly.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogChamberAssembly, Log, All);
namespace
{
using FDecorGroups = TMap<UMaterialInterface*, TArray<FTransform>>;

UStaticMeshComponent* SpawnWeightShell(UWorld& World, UStaticMesh* Mesh, TConstArrayView<FSceneBlock> Room, AActor& Assembly)
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
    FActorSpawnParameters Params;
    Params.Owner = &Assembly;
    AActor* Owner = World.SpawnActor<AActor>(Params);
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

bool SpawnRoomDecor(UWorld& World, UStaticMesh& Mesh, const FDecorGroups& Groups, AActor& Assembly)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Training_RegisterDecorInstances);
    FActorSpawnParameters Params;
    Params.Owner = &Assembly;
    AActor* Owner = World.SpawnActor<AActor>(Params);
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

AChamberAssembly::AChamberAssembly()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Origin")));
    PlateCircuit = CreateDefaultSubobject<UTrainingPlateCircuitComponent>(TEXT("PlateCircuit"));
    DoorDrive = CreateDefaultSubobject<UTrainingDoorDriveComponent>(TEXT("DoorDrive"));
}

void AChamberAssembly::Build(const UTrainingChamberLayout& Settings, UMaterialInterface* SurfaceMaterial, bool bCountHeldMass, FString& ErrorMessage)
{
    Layout = &Settings;
    UStaticMesh* Mesh = Abandoned::Blockout::LoadCube();
    if (!Mesh)
    {
        ErrorMessage = TEXT("Training chamber requires the Engine BasicShapes cube.");
        return;
    }

    const bool bStreamed = GetLevel() != GetWorld()->PersistentLevel;
    auto* MeshLibrary = GetGameInstance()->GetSubsystem<UChamberMeshLibrary>();
    UStaticMesh* SmallCube = bStreamed ? nullptr : MeshLibrary->LoadForChamber(*GetWorld(), Abandoned::FosTech::SmallCube);
    UStaticMesh* LargeCube = bStreamed ? nullptr : MeshLibrary->LoadForChamber(*GetWorld(), Abandoned::FosTech::LargeCube);
    UStaticMesh* PlateBase = bStreamed ? PlateBaseMesh.Get() : MeshLibrary->LoadForChamber(*GetWorld(), Abandoned::FosTech::PlateBase);
    UStaticMesh* PlateTop = bStreamed ? PlateTopMesh.Get() : MeshLibrary->LoadForChamber(*GetWorld(), Abandoned::FosTech::PlateTop);
    if ((!bStreamed && (!SmallCube || !LargeCube)) || !PlateBase || !PlateTop)
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
    if (bStreamed && Layout->IsA<UEntryWeightLayout>())
    {
        for (AActor* Actor : GetLevel()->Actors)
        {
            auto* Floor = Cast<AStaticMeshActor>(Actor);
            if (Floor && Floor->GetStaticMeshComponent()->GetStaticMesh() &&
                Floor->GetStaticMeshComponent()->GetStaticMesh()->GetPathName() == UChamberMeshLibrary::WeightShellPath)
            {
                Shell = Floor->GetStaticMeshComponent();
                Shell->SetVisibility(false);
                break;
            }
        }
        if (!Shell || !Shell->IsRegistered() || !Shell->IsQueryCollisionEnabled())
        {
            ErrorMessage = TEXT("Streamed Weight floor is not ready.");
            return;
        }
    }
    else if (Layout->IsA<UEntryWeightLayout>())
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("CompoundWeightShell")))
        {
            UStaticMesh* BodyMesh = MeshLibrary->LoadForChamber(World, UChamberMeshLibrary::WeightShellPath);
            Shell = SpawnWeightShell(World, BodyMesh, Layout->Room, *this);
            if (!Shell)
            {
                ErrorMessage = TEXT("Cooked Weight shell does not match the chamber layout.");
                return;
            }
        }
        UE_LOG(LogChamberAssembly, Log, TEXT("Weight shell mode=%s"), Shell ? TEXT("Compound") : TEXT("Separate"));
    }
    const bool bInstanceShellVisuals = Shell && bInstanceDecor &&
        FParse::Param(FCommandLine::Get(), TEXT("InstanceWeightShellVisuals"));
    int32 GroupedSolidCount = 0;
    for (const FSceneBlock& Block : Layout->Room)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(Training_SolidRoomBlock, Block.bCollision);
        TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(Training_DecorativeRoomBlock, !Block.bCollision);
        if ((bStreamed && Shell) || (bInstanceDecor && (!Block.bCollision || bInstanceShellVisuals)))
        {
            DecorGroups.FindOrAdd(Finishes.FindRef(Block.Finish)).Emplace(
                FQuat::Identity, Block.Center, Block.Size / 100.0);
            GroupedSolidCount += Block.bCollision ? 1 : 0;
            continue;
        }
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
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *Mesh, Block.Center, Block.Size,
            EComponentMobility::Static, Block.bCollision ? TEXT("BlockAll") : TEXT("NoCollision"), this);
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
    }
    if (!DecorGroups.IsEmpty() && !SpawnRoomDecor(World, *Mesh, DecorGroups, *this))
    {
        ErrorMessage = TEXT("Could not create the training chamber decor.");
        return;
    }
    if (Shell)
    {
        UE_LOG(LogChamberAssembly, Log, TEXT("Weight shell grouped visuals=%d"), GroupedSolidCount);
    }
    if (bDecorLayout)
    {
        UE_LOG(LogChamberAssembly, Log, TEXT("Room decor mode=%s groups=%d"),
            bInstanceDecor ? TEXT("instanced") : TEXT("separate"), DecorGroups.Num());
    }
    for (const FTrainingCubeSpawn& CubeSpawn : Layout->CubeSpawns)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Training_SpawnPuzzleCube);
        if (bStreamed)
        {
            APuzzleCube* Found = nullptr;
            for (AActor* Actor : GetLevel()->Actors)
            {
                APuzzleCube* Cube = Cast<APuzzleCube>(Actor);
                if (Cube && FVector::Dist2D(Cube->GetActorLocation(), CubeSpawn.Center) < 1.0 &&
                    Cube->GetMassKg() == (CubeSpawn.Type == EPuzzleCubeType::Small ? 5.0f : 10.0f))
                {
                    Found = Cube;
                    break;
                }
            }
            if (!Found || Cubes.Contains(Found)) { ErrorMessage = TEXT("Weight ballast placement mismatch."); return; }
            Cubes.Add(Found);
            continue;
        }
        FActorSpawnParameters Params;
        Params.Owner = this;
        APuzzleCube* Cube = World.SpawnActor<APuzzleCube>(CubeSpawn.Center, FRotator::ZeroRotator, Params);
        if (!Cube)
        {
            ErrorMessage = TEXT("Could not create a training chamber cube.");
            return;
        }
        Cube->Initialize(CubeSpawn.Type, CubeSpawn.Type == EPuzzleCubeType::Small ? *SmallCube : *LargeCube);
        Cubes.Add(Cube);
    }
    const auto DoorParts = Abandoned::Blockout::SpawnBox(World, *Mesh, Layout->Door.Center, Layout->Door.Size,
        EComponentMobility::Movable, TEXT("BlockAllDynamic"), this);
    // The leaf is 114.30 cm wide at its original height of 190.25 cm.
    const double LeafHeight = FMath::Max(Layout->Door.Size.Z, Layout->Door.Size.Y * 190.25 / 114.30);
    // Mount the jambs on the chamber side of the wall, around the playable opening.
    const double FrameWidth = Layout->Door.Size.Y + 32.0;
    const double FrameHeight = FMath::Max(Layout->Door.Size.Z, FrameWidth * 190.18 / 163.48);
    const FVector FrameSize(Layout->Door.Size.X, FrameWidth, FrameHeight);
    const FVector FrameOffset(-Layout->Door.Size.X * 0.5 - 22.0, 0, (FrameHeight - Layout->Door.Size.Z) * 0.5);
    const auto Frame = Abandoned::Blockout::SpawnBox(World, *Mesh, Layout->Door.Center + FrameOffset,
        FrameSize, EComponentMobility::Static, TEXT("NoCollision"), this);
    auto* LeafMesh = bStreamed ? DoorLeafMesh.Get() : MeshLibrary->LoadForChamber(World, Abandoned::FosTech::Leaf);
    auto* FrameMesh = bStreamed ? DoorFrameMesh.Get() : MeshLibrary->LoadForChamber(World, Abandoned::FosTech::Frame);
    if (!Abandoned::FosTech::FitVisual(DoorParts.Visual, LeafMesh, LeafHeight, -90.0) ||
        !Abandoned::FosTech::FitVisual(Frame.Visual, FrameMesh, FrameSize.Z, -90.0))
    {
        ErrorMessage = TEXT("Missing FosTech door assembly.");
        return;
    }
    DoorParts.Visual->AddLocalOffset(FVector(0, 0, (LeafHeight - Layout->Door.Size.Z) * 0.5));
    PlateCircuit->Configure(*Layout, bCountHeldMass);
    for (int32 Index = 0; Index < Layout->GetPlateCount(); ++Index)
    {
        const FSceneBlock& Block = Layout->GetPlateAt(Index);
        TRACE_CPUPROFILER_EVENT_SCOPE(Training_BuildPressurePlate);
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *PlateTop, Block.Center, Block.Size, EComponentMobility::Movable, TEXT("BlockAll"), this);
        if (!Parts.Collision || !Parts.Visual)
        {
            ErrorMessage = TEXT("Could not create the pressure plates.");
            return;
        }
        // Both parts retain the common GLB origin; align the tread surface with the gameplay box.
        const auto Bounds = PlateBase->GetBounds();
        const double PlateScale = FMath::Min(Block.Size.X / (2.0 * Bounds.BoxExtent.X), Block.Size.Y / (2.0 * Bounds.BoxExtent.Y));
        const FVector PlateOffset(-Bounds.Origin.X * PlateScale, -Bounds.Origin.Y * PlateScale,
            Block.Size.Z * 0.5 - PlateTop->GetBoundingBox().Max.Z * PlateScale);
        Parts.Visual->SetRelativeLocation(PlateOffset);
        Parts.Visual->SetRelativeScale3D(FVector(PlateScale));
        AActor* PlateActor = Parts.Collision->GetOwner();
        auto* Base = NewObject<UStaticMeshComponent>(PlateActor);
        PlateActor->AddInstanceComponent(Base);
        Base->SetupAttachment(Parts.Collision);
        Base->SetMobility(EComponentMobility::Movable);
        Base->SetCollisionProfileName(TEXT("NoCollision"));
        Base->SetStaticMesh(PlateBase);
        Base->SetRelativeLocation(PlateOffset);
        Base->SetRelativeScale3D(FVector(PlateScale));
        Base->RegisterComponent();
        FSceneLight Light = Layout->PlateLight;
        Light.Position += Block.Center - Layout->Plate.Center;
        PlateCircuit->AddPlate(*Parts.Collision, *Parts.Visual, Abandoned::Scene::AddLight(World, Light, this));
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
    if (Layout->UsesParallelPlates())
    {
        const auto RelayBox = Abandoned::Blockout::SpawnBox(World, *Mesh,
            Layout->Door.Center + FVector(-40, Layout->Door.Size.Y * 0.5 + 44, 0), FVector(20, 46, 52),
            EComponentMobility::Static, TEXT("NoCollision"), this);
        RelayHousing = RelayBox.Visual;
        auto* HousingMesh = bStreamed ? RelayMesh.Get() : MeshLibrary->LoadForChamber(World, Abandoned::FosTech::Relay);
        if (!Abandoned::FosTech::FitVisual(RelayHousing, HousingMesh, 52.0, 90.0))
        {
            ErrorMessage = TEXT("Missing FosTech relay box.");
            return;
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
            Layout->Button.Size, EComponentMobility::Movable, TEXT("BlockAll"), this);
        Button = ButtonParts.Collision;
        ButtonVisual = ButtonParts.Visual;
        auto* MeshForSwitch = bStreamed ? SwitchMesh.Get() : MeshLibrary->LoadForChamber(World, Abandoned::FosTech::Button);
        if (!Abandoned::FosTech::FitVisual(ButtonVisual, MeshForSwitch, Layout->Button.Size.Z, 90.0))
        {
            ErrorMessage = TEXT("Missing FosTech wall button.");
            return;
        }
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
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *Mesh, Terminal.Center, Terminal.Size, EComponentMobility::Static, TEXT("BlockAll"), this);
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
        Abandoned::Scene::AddLight(World, Light, this);
    }
}

bool AChamberAssembly::IsStreamedRoomReady() const
{
    const auto* Door = DoorDrive->GetDoor();
    const auto ReadyBody = [this](const UBoxComponent* Body)
    {
        return IsValid(Body) && Body->GetOwner()->GetLevel() == GetLevel() && Body->IsRegistered() &&
            Body->IsQueryCollisionEnabled();
    };
    if (!Layout || !ReadyBody(Door) || (!Layout->IsA<UWeightBalanceLayout>() && !ReadyBody(Button)) || PlateCircuit->GetPlates().Num() != Layout->GetPlateCount())
    {
        return false;
    }
    const auto ReadyVisual = [this](const UStaticMeshComponent* Visual)
    {
        return IsValid(Visual) && Visual->GetOwner()->GetLevel() == GetLevel() &&
            Visual->IsRegistered() && Visual->GetStaticMesh();
    };
    for (const auto& Plate : PlateCircuit->GetPlates())
    {
        if (!ReadyBody(Plate.Body) || !ReadyVisual(Plate.Visual) || !IsValid(Plate.Indicator) || !Plate.Indicator->IsRegistered())
        {
            return false;
        }
    }
    if (Layout->UsesParallelPlates() && (!ReadyVisual(RelayHousing) || !RelayIdleMaterial || !RelayPoweredMaterial ||
        RelayTraces.Num() != Layout->RelayTraces.Num() ||
        RelayTraces.ContainsByPredicate([&](const auto& Trace) { return !ReadyVisual(Trace); })))
    {
        return false;
    }
    return
        Cubes.Num() == Layout->CubeSpawns.Num() &&
        !Cubes.ContainsByPredicate([this](const APuzzleCube* Cube)
        {
            return !IsValid(Cube) || Cube->GetLevel() != GetLevel() || !Cube->GetBody()->IsSimulatingPhysics();
        });
}

void AChamberAssembly::Retire()
{
    DoorDrive->ReleaseClamp();
    const auto Owned = Children;
    for (AActor* Actor : Owned)
    {
        if (IsValid(Actor)) { Actor->Destroy(); }
    }
    Destroy();
}

void AChamberAssembly::BuildStreamed(const UTrainingChamberLayout& Template, UMaterialInterface* SurfaceMaterial, FString& ErrorMessage)
{
    auto* Settings = DuplicateObject<UTrainingChamberLayout>(&Template, this);
    if (!Settings->IsValid() || (Settings->IsA<UEntryButtonLayout>() && !Settings->UsesParallelPlates()) ||
        (Settings->IsA<UWeightBalanceLayout>() && (!Settings->UsesPlateBalance() || Settings->GetPlateCount() != 3)))
    {
        ErrorMessage = TEXT("Invalid streamed chamber layout.");
        return;
    }
    const FVector Origin = GetActorLocation();
    Settings->Spawn += Origin;
    Settings->HintTerminal.Center += Origin;
    Settings->Button.Center += Origin;
    Settings->StackZone.Center += Origin;
    Settings->PlateLight.Position += Origin;
    Settings->Door.Center += Origin;
    Settings->Plate.Center += Origin;
    Settings->ExitZone.Center += Origin;
    Settings->CubeBounds.Center += Origin;
    for (auto& Block : Settings->Room) { Block.Center += Origin; }
    for (auto& Light : Settings->Lights) { Light.Position += Origin; }
    for (auto& Spawn : Settings->CubeSpawns) { Spawn.Center += Origin; Spawn.Outlet += Origin; }
    for (auto& Plate : Settings->AdditionalPlates) { Plate.Center += Origin; }
    for (auto& Trace : Settings->RelayTraces) { Trace.Block.Center += Origin; }
    Build(*Settings, SurfaceMaterial, true, ErrorMessage);
}
