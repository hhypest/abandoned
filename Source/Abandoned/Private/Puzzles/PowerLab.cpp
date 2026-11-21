#include "Puzzles/PowerLab.h"
#include "World/FosTech.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Verification/OpticsVerification.h"
#endif

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Infrastructure/Puzzles/OpticalCircuitComponent.h"
#include "Infrastructure/Puzzles/OpticalReflectorComponent.h"
#include "Infrastructure/Puzzles/PowerLabDoorsComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Player/AbandonedTestController.h"
#include "Puzzles/PowerLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Blockout.h"
#include "World/SceneAssembly.h"

namespace
{
const FVector DoorSize(54.0, 204.0, 260.0);
}

APowerLabGameMode::APowerLabGameMode()
{
    Optics = CreateDefaultSubobject<UOpticalCircuitComponent>(TEXT("OpticalCircuit"));
    Reflector = CreateDefaultSubobject<UOpticalReflectorComponent>(TEXT("Reflector"));
    Doors = CreateDefaultSubobject<UPowerLabDoorsComponent>(TEXT("Doors"));
    DefaultPawnClass = APowerLabCharacter::StaticClass();
    PlayerControllerClass = AAbandonedTestController::StaticClass();
    HUDClass = APowerLabHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;

    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface"));
    BaseMaterial = Material.Object;
}

void APowerLabGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
    Super::InitGame(Map, Options, Error);
    using namespace Abandoned::Infrastructure::Expedition;
    if (!Error.IsEmpty() || !ReadWorldDestination(Options, EntryWorld::Optics, Error))
    {
        return;
    }
    SetActorTickEnabled(false);
    UWorld& World = *GetWorld();
    UStaticMesh* BoxMesh = Abandoned::Blockout::LoadCube();
    Layout = GetDefault<UPowerLabLayout>();
    const auto LaneValid = [](const FOpticalLaneLayout& Lane)
    {
        return !Lane.Emitter.ContainsNaN() && !Lane.Receiver.ContainsNaN() && !Lane.Door.ContainsNaN() &&
            !Lane.Emitter.Equals(Lane.Receiver);
    };
    if (!BoxMesh || !BaseMaterial || !LaneValid(Layout->Interruption) || !LaneValid(Layout->Reflection) ||
        Layout->Room.IsEmpty() || Layout->Spawn.ContainsNaN() || Layout->Cube.ContainsNaN() ||
        Layout->Button.ContainsNaN() || Layout->Mirror.ContainsNaN() || Layout->MirrorDock.ContainsNaN() ||
        !Layout->HasValidPulse())
    {
        Error = TEXT("Invalid optical lab layout or missing base assets.");
        return;
    }
    Abandoned::Blockout::ApplyGravity(World);
    const auto Finishes = Abandoned::Scene::MakeFinishes(*this, BaseMaterial, Layout->Finishes);
    UMaterialInterface* Cyan = Finishes.FindRef(TEXT("Cyan"));
    UMaterialInterface* Amber = Finishes.FindRef(TEXT("Amber"));
    UMaterialInterface* Inactive = Finishes.FindRef(TEXT("Inactive"));
    if (!Cyan || !Amber || !Inactive)
    {
        Error = TEXT("Missing optical lab light materials.");
        return;
    }
    const auto Box = [&](FVector Center, FVector Size, FName Finish, bool bSolid, bool bMoves = false)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *BoxMesh, Center, Size,
            bMoves ? EComponentMobility::Movable : EComponentMobility::Static,
            bSolid ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
        if (Parts.Visual)
        {
            Parts.Visual->SetMaterial(0, Finishes.FindRef(Finish));
        }
        return Parts;
    };
    for (const FSceneBlock& Block : Layout->Room)
    {
        if (!Block.IsValid() || !Finishes.Contains(Block.Finish) ||
            !Box(Block.Center, Block.Size, Block.Finish, Block.bCollision).Collision)
        {
            Error = TEXT("Invalid optical lab room block.");
            return;
        }
    }
    for (const FSceneLight& Light : Layout->Lights)
    {
        if (!Light.IsValid() || !Abandoned::Scene::AddLight(World, Light))
        {
            Error = TEXT("Invalid optical lab light.");
            return;
        }
    }
    const auto BuildLane = [&](FOpticalLane& Lane, const FOpticalLaneLayout& Data, bool bAimable) -> UBoxComponent*
    {
        const auto Emitter = Box(Data.Emitter, FVector(48, 44, 50), TEXT("Steel"), true);
        const auto Receiver = Box(Data.Receiver, FVector(65, 68, 68), TEXT("Panel"), true);
        Lane.EmitterHousing = Emitter.Visual;
        if (Lane.EmitterHousing && bAimable)
        {
            Lane.EmitterHousing->SetMobility(EComponentMobility::Movable);
        }
        Lane.Emitter = Emitter.Collision;
        Lane.Receiver = Receiver.Collision;
        const double Heading = (Data.Receiver - Data.Emitter).Rotation().Yaw;
        if (!Abandoned::FosTech::SetVisual(Emitter.Visual, Abandoned::FosTech::Emitter, 25.0, Heading - 90.0) ||
            !Abandoned::FosTech::SetVisual(Receiver.Visual, Abandoned::FosTech::Receiver, 60.0, Heading + 90.0))
        {
            return nullptr;
        }
        Box(FVector(Data.Emitter.X, Data.Emitter.Y, 19.0), FVector(27, 29, 39), TEXT("Steel"), false);
        Box(FVector(Data.Receiver.X, Data.Receiver.Y, 15.0), FVector(42, 28, 31), TEXT("Steel"), false);
        UBoxComponent* Door = Box(Data.Door, DoorSize, TEXT("Steel"), true, true).Collision;
        Lane.EmitterLamp = Box(Data.Emitter + FVector(0, 0, 28), FVector(48, 44, 8), TEXT("Inactive"), false).Visual;
        Lane.ReceiverLamp = Box(Data.Receiver + FVector(0, 0, 37), FVector(17, 58, 8), TEXT("Inactive"), false).Visual;
        Lane.PowerLine = Box(FVector(Data.Door.X - 130, 0, 3), FVector(260, 8, 6), TEXT("Inactive"), false).Visual;
        return Lane.Emitter && Lane.Receiver && Lane.EmitterLamp && Lane.ReceiverLamp && Lane.PowerLine ? Door : nullptr;
    };
    FOpticalLane Interruption;
    FOpticalLane Reflection;
    UBoxComponent* DirectDoor = BuildLane(Interruption, Layout->Interruption, false);
    UBoxComponent* ReflectedDoor = BuildLane(Reflection, Layout->Reflection, true);
    if (!DirectDoor || !ReflectedDoor)
    {
        Error = TEXT("Could not assemble optical devices.");
        return;
    }
    UMaterialInterface* Steel = Finishes.FindRef(TEXT("Steel"));
    UMaterialInterface* Panel = Finishes.FindRef(TEXT("Panel"));
    UMaterialInterface* PulseScreen = Finishes.FindRef(TEXT("PulseScreen"));
    if (!Steel || !Panel || !PulseScreen || !Doors->Build(*Layout, *DirectDoor, *ReflectedDoor, *BoxMesh,
        *Steel, *Panel, *PulseScreen, *Amber, *Inactive))
    {
        Error = TEXT("Could not assemble the timed door, switches or clock.");
        return;
    }
    Button = Box(Layout->Button, FVector(52, 24, 42), TEXT("Panel"), true).Collision;
    Box(Layout->Button - FVector(0, 15, 0), FVector(62, 26, 48), TEXT("Steel"), false);
    UStaticMeshComponent* ButtonLamp = Box(Layout->Button + FVector(0, 13, 0), FVector(36, 4, 10), TEXT("Inactive"), false).Visual;
    Box(Layout->MirrorDock - FVector(0, 0, UOpticalReflectorComponent::HeightCm / 2.0 + 2.0),
        FVector(94, 94, 6), TEXT("Panel"), false);
    for (const double Side : {-1.0, 1.0})
    {
        Box(FVector(Layout->MirrorDock.X, Layout->MirrorDock.Y + Side * 47.0, 4),
            FVector(98, 5, 7), TEXT("White"), false);
    }

    Cube = World.SpawnActor<APuzzleCube>();
    UMaterialInterface* MirrorBase = Finishes.FindRef(TEXT("Dark"));
    if (!Button || !ButtonLamp || !Cube || !Cube->Initialize(EPuzzleCubeType::Large) || !MirrorBase || !Reflector->Build(Layout->Mirror, Layout->MirrorDock, *BoxMesh, *MirrorBase, *Steel))
    {
        Error = TEXT("Could not assemble the optical lab controls.");
        return;
    }
    Cube->ResetPosition(Layout->Cube);
    Reflector->RestoreMirror();
    if (!Optics->Build(Interruption, Reflection, *BoxMesh, *ButtonLamp, *Cyan, *Amber, *Inactive))
    {
        Error = TEXT("Could not reserve the optical beam renderers.");
        return;
    }
#if WITH_DEV_AUTOMATION_TESTS
    UOpticsVerification::AttachIfRequested(*this);
#endif
    SetActorTickEnabled(true);
}

void APowerLabGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
    if (Player && Layout && IsActorTickEnabled())
    {
        RestartPlayerAtTransform(Player, FTransform(FRotator::ZeroRotator, Layout->Spawn));
    }
}

void APowerLabGameMode::ResetLab(APowerLabCharacter& Player)
{
    Player.Aim = EOpticsAim::None;
    Player.ReleaseHeldObject();
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
    Optics->Reset();
    Doors->Reset();
    Cube->ResetPosition(Layout->Cube);
    Reflector->RestoreMirror();
}

bool APowerLabGameMode::Interact(APowerLabCharacter& Player, UPrimitiveComponent& Target)
{
    if (&Target == Button)
    {
        Optics->PressSupplyButton();
        return true;
    }
    if (Doors->UseSwitch(Target))
    {
        return true;
    }
    if (&Target == Optics->AimableEmitter())
    {
        Player.Aim = EOpticsAim::Emitter;
        return true;
    }
    if (Reflector->IsMirrorTarget(&Target))
    {
        Reflector->PickUp(Player);
        return true;
    }
    return false;
}

void APowerLabGameMode::Tick(float Delta)
{
    Super::Tick(Delta);
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    APowerLabCharacter* Player = Controller ? Cast<APowerLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player)
    {
        return;
    }
    if (Cube->GetActorLocation().Z < -240.0)
    {
        Doors->ReleaseIfHolding(Cube->GetBody());
        if (Player->GetHeldCube() == Cube)
        {
            Player->ReleaseHeldObject();
        }
        Cube->ResetPosition(Layout->Cube);
    }
    UBoxComponent* Mirror = Reflector->GetBody();
    if (Mirror->GetComponentLocation().Z < -240.0)
    {
        if (Player->GetHeldBody() == Mirror)
        {
            Player->ReleaseHeldObject();
        }
        Doors->ReleaseIfHolding(Mirror);
        Reflector->RestoreMirror();
        Player->Aim = EOpticsAim::None;
    }
    const auto Signals = Optics->Update(Delta, *Reflector);
    switch (Doors->Update(Delta, Signals, *Player, Cube->GetBody(), Reflector->PortableBody()))
    {
    case EPowerLabRecovery::PulseRetry:
        Player->ReleaseHeldObject();
        Player->PlaceAt(FTransform(FRotator::ZeroRotator, Layout->PulseRetry));
        break;
    case EPowerLabRecovery::ResetLab:
        ResetLab(*Player);
        break;
    case EPowerLabRecovery::None:
        break;
    }
}

bool APowerLabGameMode::IsInPulseBay(const FVector& Player) const
{
    return Doors->IsInPulseBay(Player);
}

float APowerLabGameMode::GetPulseSeconds() const
{
    return Doors->GetPulseSeconds();
}

float APowerLabGameMode::GetPulseFraction() const
{
    return Doors->GetPulseFraction();
}

const TCHAR* APowerLabGameMode::GetObjective(const FVector& Player) const
{
    if (IsInPulseBay(Player))
    {
        return Doors->GetPulseObjective(Player);
    }
    if (Doors->OpticalDoorClamped())
    {
        return TEXT("Door obstructed. Restore its signal to release the item.");
    }
    if (Player.X > Layout->Interruption.Door.X + 60.0)
    {
        if (Optics->HasReflection())
        {
            return TEXT("Signal received. Go through the open door.");
        }
        return TEXT("Place mirror [E]. Aim emitter at mirror [E] and mirror at receiver [Q].");
    }
    return Optics->HasSupply() ? TEXT("Interrupt the beam with the cube or your suit.")
        : TEXT("Turn on the signal at the wall switch [E].");
}

void APowerLabCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Super::BindAdditionalInput(Input);
    Input.BindAction(MapAction(EKeys::Q), ETriggerEvent::Started, this, &APowerLabCharacter::AimMirror);
}

void APowerLabCharacter::Interact()
{
    if (IsAiming())
    {
        Aim = EOpticsAim::None;
        return;
    }
    Super::Interact();
}

bool APowerLabGameMode::UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target)
{
    auto* LabPlayer = Cast<APowerLabCharacter>(&Player);
    return LabPlayer && Interact(*LabPlayer, Target);
}

void APowerLabGameMode::PlaceDevice(APuzzleCharacter& Player, UBoxComponent& Body)
{
    if (auto* LabPlayer = Cast<APowerLabCharacter>(&Player); LabPlayer && &Body == Reflector->GetBody())
    {
        Reflector->PlaceMirror(*LabPlayer);
    }
}

void APowerLabGameMode::ResetPuzzle(APuzzleCharacter& Player)
{
    if (auto* LabPlayer = Cast<APowerLabCharacter>(&Player))
    {
        ResetLab(*LabPlayer);
    }
}

void APowerLabCharacter::AimMirror()
{
    const APowerLabGameMode* Lab = GetWorld()->GetAuthGameMode<APowerLabGameMode>();
    if (Lab && Lab->Reflector->IsMirrorTarget(TraceInteractable()) && Lab->Reflector->CanAimMirror(*this))
    {
        Aim = EOpticsAim::Mirror;
    }
}

void APowerLabCharacter::Tick(float Delta)
{
    Super::Tick(Delta);
    APowerLabGameMode* Lab = GetWorld()->GetAuthGameMode<APowerLabGameMode>();
    if (!IsAiming() || !Lab)
    {
        return;
    }
    const FVector Start = Camera->GetComponentLocation();
    const FVector End = Start + GetViewRotation().Vector() * UOpticalCircuitComponent::BeamReachCm;
    const UBoxComponent* Body = Aim == EOpticsAim::Emitter ? Lab->Optics->AimableEmitter() : Lab->Reflector->GetBody();
    const AActor* Device = IsValid(Body) ? Body->GetOwner() : nullptr;
    if (!IsValid(Device) || FVector::DistSquared(Device->GetActorLocation(), Start) > FMath::Square(380.0) ||
        (Aim == EOpticsAim::Mirror && !Lab->Reflector->CanAimMirror(*this)))
    {
        Aim = EOpticsAim::None;
        return;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(AimOptics), false, this);
    Query.AddIgnoredActor(Device);
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);
    const FVector Point = Hit.bBlockingHit ? Hit.ImpactPoint : End;
    if (Aim == EOpticsAim::Emitter)
    {
        Lab->Optics->AimEmitter(Point);
    }
    else
    {
        Lab->Optics->AimReflector(*Lab->Reflector, Point);
    }
}

void APowerLabHUD::DrawHUD()
{
    Super::DrawHUD();
    const APowerLabGameMode* Lab = GetWorld()->GetAuthGameMode<APowerLabGameMode>();
    const APowerLabCharacter* Player = Cast<APowerLabCharacter>(GetOwningPawn());
    if (!Canvas || !Lab || !Player)
    {
        return;
    }
    DrawText(TEXT("ENERGY TIER / OPTICS"), FLinearColor::White, 24, 24, nullptr, 1.15f);
    DrawText(Player->IsAiming() ? TEXT("Align with mouse. Confirm [E].") : Lab->GetObjective(Player->GetActorLocation()),
        FLinearColor::White, 24, 53, nullptr, 1.0f);
    DrawText(TEXT("Reset [R]"), FLinearColor::White, 24, 76, nullptr, 0.9f);
    if (Lab->IsInPulseBay(Player->GetActorLocation()))
    {
        const float Seconds = Lab->GetPulseSeconds();
        DrawRect(FLinearColor(0, 0, 0, 0.8f), 16, 97, 214, 41);
        DrawText(Seconds > 0.0f ? FString::Printf(TEXT("Pulse %.1f s"), FMath::CeilToFloat(Seconds * 10.0f) * 0.1f)
            : TEXT("Pulse ready"), FLinearColor::White, 24, 102, nullptr, 1.0f);
        DrawRect(FLinearColor(0.22f, 0.24f, 0.25f), 24, 124, 196, 5);
        DrawRect(FLinearColor(1.0f, 0.66f, 0.22f), 24, 124, 196 * Lab->GetPulseFraction(), 5);
    }
    const float X = Canvas->ClipX * 0.5f;
    const float Y = Canvas->ClipY * 0.5f;
    DrawLine(X - 6, Y, X + 6, Y, FLinearColor::White, 1);
    DrawLine(X, Y - 6, X, Y + 6, FLinearColor::White, 1);
}
