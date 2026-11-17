#include "Puzzles/PowerLab.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Domain/Puzzles/OpticalReflector.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PowerLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Blockout.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogPowerLab, Log, All);

namespace
{
constexpr float MirrorMassKg = 8.0f;
constexpr double BeamReachCm = 2400.0;
constexpr double DockRangeCm = 100.0;
constexpr double MirrorPlacementDropCm = 60.0;
constexpr double MirrorPlacementClearanceCm = 0.5;
constexpr double DoorTravelCm = 270.0;
constexpr double DoorSpeedCm = 320.0;
constexpr ECollisionChannel OpticalBeam = ECC_GameTraceChannel1;
const FVector MirrorSize(66.0, 52.0, 108.0);
const FVector DoorSize(54.0, 204.0, 260.0);
}

APowerLabGameMode::APowerLabGameMode()
{
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
    Cyan = Finishes.FindRef(TEXT("Cyan"));
    Amber = Finishes.FindRef(TEXT("Amber"));
    Inactive = Finishes.FindRef(TEXT("Inactive"));
    UMaterialInterface* Ballast = Finishes.FindRef(TEXT("Ballast"));
    if (!Cyan || !Amber || !Inactive || !Ballast)
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
    const auto BuildLane = [&](FOpticalLane& Lane, const FOpticalLaneLayout& Data)
    {
        Lane.Emitter = Box(Data.Emitter, FVector(48, 44, 50), TEXT("Steel"), true).Collision;
        Lane.Receiver = Box(Data.Receiver, FVector(65, 68, 68), TEXT("Panel"), true).Collision;
        Box(FVector(Data.Emitter.X, Data.Emitter.Y, 19.0), FVector(27, 29, 39), TEXT("Steel"), false);
        Box(FVector(Data.Receiver.X, Data.Receiver.Y, 15.0), FVector(42, 28, 31), TEXT("Steel"), false);
        Lane.Door = Box(Data.Door, DoorSize, TEXT("Steel"), true, true).Collision;
        Lane.EmitterLamp = Box(Data.Emitter + FVector(0, 0, 28), FVector(48, 44, 8), TEXT("Inactive"), false).Visual;
        Lane.ReceiverLamp = Box(Data.Receiver + FVector(0, 0, 37), FVector(17, 58, 8), TEXT("Inactive"), false).Visual;
        Lane.PowerLine = Box(FVector(Data.Door.X - 130, 0, 3), FVector(260, 8, 6), TEXT("Inactive"), false).Visual;
        return Lane.Emitter && Lane.Receiver && Lane.Door && Lane.EmitterLamp && Lane.ReceiverLamp && Lane.PowerLine;
    };
    if (!BuildLane(Interruption, Layout->Interruption) || !BuildLane(Reflection, Layout->Reflection))
    {
        Error = TEXT("Could not assemble optical devices.");
        return;
    }
    Interruption.Clamp.AddSigns(*Interruption.Door);
    Reflection.Clamp.AddSigns(*Reflection.Door);
    UMaterialInterface* Steel = Finishes.FindRef(TEXT("Steel"));
    UMaterialInterface* Panel = Finishes.FindRef(TEXT("Panel"));
    UMaterialInterface* PulseScreen = Finishes.FindRef(TEXT("PulseScreen"));
    if (!Steel || !Panel || !PulseScreen || !SpawnPulseBay(*BoxMesh, *Steel, *Panel, *PulseScreen))
    {
        Error = TEXT("Could not assemble the timed door, switches or clock.");
        return;
    }
    Button = Box(Layout->Button, FVector(52, 24, 42), TEXT("Panel"), true).Collision;
    Box(Layout->Button - FVector(0, 15, 0), FVector(62, 26, 48), TEXT("Steel"), false);
    ButtonLamp = Box(Layout->Button + FVector(0, 13, 0), FVector(36, 4, 10), TEXT("Inactive"), false).Visual;
    Box(Layout->MirrorDock - FVector(0, 0, MirrorSize.Z / 2.0 + 2.0),
        FVector(94, 94, 6), TEXT("Panel"), false);
    for (const double Side : {-1.0, 1.0})
    {
        Box(FVector(Layout->MirrorDock.X, Layout->MirrorDock.Y + Side * 47.0, 4),
            FVector(98, 5, 7), TEXT("White"), false);
    }

    Cube = World.SpawnActor<APuzzleCube>();
    UMaterialInterface* MirrorBase = Finishes.FindRef(TEXT("Dark"));
    if (!Button || !ButtonLamp || !Cube || !MirrorBase || !BuildMirror(*BoxMesh, *MirrorBase, *Steel))
    {
        Error = TEXT("Could not assemble the optical lab controls.");
        return;
    }
    Cube->Initialize(EPuzzleCubeType::Large, *BoxMesh);
    Cube->SetVisualMaterial(*Ballast);
    Cube->ResetPosition(Layout->Cube);
    RestoreMirror();
    for (int32 Index = 0; Index < 4; ++Index)
    {
        UStaticMeshComponent* Segment = Box(FVector::ZeroVector, FVector(1), TEXT("Cyan"), false, true).Visual;
        if (!Segment)
        {
            Error = TEXT("Could not reserve the optical beam renderers.");
            return;
        }
        Segment->SetCastShadow(false);
        Segment->SetVisibility(false);
        Beams.Add(Segment);
    }
    Interruption.Direction = (Layout->Interruption.Receiver - Layout->Interruption.Emitter).GetSafeNormal();
    Reflection.Direction = FVector::ForwardVector;
#if WITH_DEV_AUTOMATION_TESTS
    bVerify = FParse::Param(FCommandLine::Get(), TEXT("VerifyPowerLab"));
    bCapture = FParse::Param(FCommandLine::Get(), TEXT("CapturePowerLab"));
    bVerifyFreeReflector = FParse::Param(FCommandLine::Get(), TEXT("VerifyFreeReflector"));
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

bool APowerLabGameMode::BuildMirror(UStaticMesh& Mesh, UMaterialInterface& Base, UMaterialInterface& Steel)
{
    const auto Parts = Abandoned::Blockout::SpawnBox(*GetWorld(), Mesh, Layout->Mirror, MirrorSize,
        EComponentMobility::Movable, TEXT("BlockAll"));
    Mirror = Parts.Collision;
    if (!Mirror || !Parts.Visual)
    {
        return false;
    }
    // Chaos uses the carry envelope. Optics and aiming trace the visible parts.
    Mirror->SetCollisionResponseToChannel(OpticalBeam, ECR_Ignore);
    Mirror->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    Parts.Visual->SetRelativeScale3D(FVector(0.66, 0.52, 0.20));
    Parts.Visual->SetRelativeLocation(FVector(0, 0, -44));
    Parts.Visual->SetMaterial(0, &Base);
    AActor* MirrorActor = Mirror->GetOwner();
    const auto AddOpticalPart = [&](FVector Offset, FVector Size)
    {
        auto* Shape = NewObject<UBoxComponent>(MirrorActor);
        MirrorActor->AddInstanceComponent(Shape);
        Shape->SetupAttachment(Mirror);
        Shape->SetMobility(EComponentMobility::Movable);
        Shape->SetRelativeLocation(Offset);
        Shape->InitBoxExtent(Size * 0.5);
        Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Shape->SetCollisionObjectType(ECC_WorldDynamic);
        Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
        Shape->SetCollisionResponseToChannel(OpticalBeam, ECR_Block);
        Shape->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        Shape->SetGenerateOverlapEvents(false);
        Shape->SetCanEverAffectNavigation(false);
        Shape->BodyInstance.bAutoWeld = false;
        Shape->RegisterComponent();
        return Shape;
    };
    const auto AddVisual = [&](UBoxComponent& Shape)
    {
        auto* Visual = NewObject<UStaticMeshComponent>(MirrorActor);
        MirrorActor->AddInstanceComponent(Visual);
        Visual->SetupAttachment(&Shape);
        Visual->SetMobility(EComponentMobility::Movable);
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Visual->SetStaticMesh(&Mesh);
        Visual->SetRelativeScale3D(Shape.GetUnscaledBoxExtent() / 50.0);
        Visual->SetMaterial(0, &Steel);
        Visual->RegisterComponent();
    };
    AddOpticalPart(FVector(0, 0, -44), FVector(66, 52, 20));
    AddVisual(*AddOpticalPart(FVector(0, 0, -24), FVector(14, 23, 20)));
    MirrorSurface = AddOpticalPart(FVector(0, 0, 8), FVector(5, 62, 45));
    AddVisual(*MirrorSurface);
    return true;
}

void APowerLabGameMode::RestoreMirror()
{
    for (auto* Clamp : {&Interruption.Clamp, &Reflection.Clamp, &PulseClamp})
    {
        if (Clamp->Holds(Mirror))
        {
            Clamp->Release();
        }
    }
    bMirrorDocked = false;
    Mirror->SetSimulatePhysics(false);
    Mirror->SetWorldLocationAndRotation(Layout->Mirror, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
    MirrorSurface->SetRelativeRotation(FRotator(0, -45, 0));
    Mirror->SetSimulatePhysics(true);
    Mirror->SetMassOverrideInKg(NAME_None, MirrorMassKg);
    Mirror->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Mirror->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

void APowerLabGameMode::ResetLab(APowerLabCharacter& Player)
{
    Interruption.Clamp.Release();
    Reflection.Clamp.Release();
    Player.Aim = EOpticsAim::None;
    Player.ReleaseHeldObject();
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
    Course.Reset();
    ResetPulseBay();
    Cube->ResetPosition(Layout->Cube);
    RestoreMirror();
    Reflection.Direction = FVector::ForwardVector;
    Interruption.Door->SetWorldLocation(Layout->Interruption.Door, false, nullptr, ETeleportType::TeleportPhysics);
    Reflection.Door->SetWorldLocation(Layout->Reflection.Door, false, nullptr, ETeleportType::TeleportPhysics);
    bDirectSignal = bReflectedSignal = false;
    ButtonLamp->SetMaterial(0, Inactive);
    for (const FOpticalLane* Lane : { &Interruption, &Reflection })
    {
        Lane->EmitterLamp->SetMaterial(0, Inactive);
        Lane->ReceiverLamp->SetMaterial(0, Inactive);
        Lane->PowerLine->SetMaterial(0, Inactive);
    }
    for (UStaticMeshComponent* Beam : Beams)
    {
        Beam->SetVisibility(false);
    }
}

bool APowerLabGameMode::Interact(APowerLabCharacter& Player, UPrimitiveComponent& Target)
{
    if (&Target == Button)
    {
        Course.PressSupplyButton();
        return true;
    }
    if (&Target == PulseButton || &Target == PulseReturn)
    {
        Pulse.Press();
        UpdatePulseDisplay();
        return true;
    }
    if (&Target == Reflection.Emitter)
    {
        Player.Aim = EOpticsAim::Emitter;
        return true;
    }
    if (IsMirrorTarget(&Target))
    {
        if (!IsValid(Mirror) || Abandoned::Infrastructure::Puzzles::IsDoorClamped(Mirror))
        {
            return true;
        }
        if (bMirrorDocked)
        {
            Mirror->SetSimulatePhysics(true);
        }
        if (Player.GrabBody(*Mirror, MirrorMassKg))
        {
            bMirrorDocked = false;
        }
        else if (bMirrorDocked)
        {
            Mirror->SetSimulatePhysics(false);
        }
        return true;
    }
    return false;
}

void APowerLabGameMode::PlaceMirror(const APowerLabCharacter& Player)
{
    const FVector From = Mirror->GetComponentLocation();
    const FCollisionShape Shape = Mirror->GetCollisionShape();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ReflectorPlacement), false, Mirror->GetOwner());
    const FCollisionResponseParams Response(Mirror->GetCollisionResponseToChannels());
    const ECollisionChannel Channel = Mirror->GetCollisionObjectType();
    const UWorld& World = *GetWorld();
    if (FVector::DistSquared(From, Layout->MirrorDock) <= FMath::Square(DockRangeCm) &&
        !World.OverlapBlockingTestByChannel(Layout->MirrorDock, FQuat::Identity, Channel, Shape, Query, Response))
    {
        Mirror->SetSimulatePhysics(false);
        Mirror->SetWorldLocationAndRotation(Layout->MirrorDock, FRotator::ZeroRotator,
            false, nullptr, ETeleportType::TeleportPhysics);
        bMirrorDocked = true;
        return;
    }

    const FQuat Upright = FRotator(0, Mirror->GetComponentRotation().Yaw, 0).Quaternion();
    FHitResult Support;
    if (!World.SweepSingleByChannel(Support, From, From - FVector(0, 0, MirrorPlacementDropCm),
        Upright, Channel, Shape, Query, Response) || Support.bStartPenetrating ||
        Support.ImpactNormal.Z < 0.7 || Support.GetActor() == &Player)
    {
        return;
    }
    const FVector Landing = Support.Location + FVector(0, 0, MirrorPlacementClearanceCm);
    if (World.OverlapBlockingTestByChannel(Landing, Upright, Channel, Shape, Query, Response))
    {
        return;
    }
    Mirror->SetWorldLocationAndRotation(Landing, Upright, false, nullptr, ETeleportType::TeleportPhysics);
    Mirror->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Mirror->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

bool APowerLabGameMode::IsMirrorTarget(const UPrimitiveComponent* Target) const
{
    return IsValid(Target) && IsValid(Mirror) && Target->GetOwner() == Mirror->GetOwner();
}

bool APowerLabGameMode::CanAimMirror(const APowerLabCharacter& Player) const
{
    if (Player.GetHeldBody() || !IsValid(Mirror) || !IsValid(MirrorSurface) ||
        Abandoned::Infrastructure::Puzzles::IsDoorClamped(Mirror))
    {
        return false;
    }
    if (bMirrorDocked)
    {
        return true;
    }
    const double Upright = Mirror->GetUpVector().Z;
    const double SpeedSq = Mirror->GetPhysicsLinearVelocity().SizeSquared();
    const double SpinSq = Mirror->GetPhysicsAngularVelocityInDegrees().SizeSquared();
    if (!Mirror->IsSimulatingPhysics() || !Abandoned::Domain::Puzzles::IsReflectorSettled(Upright, SpeedSq, SpinSq))
    {
        return false;
    }
    const FVector Base = Mirror->GetComponentTransform().TransformPosition(
        FVector(0, 0, -Mirror->GetUnscaledBoxExtent().Z));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ReflectorSupport), false, Mirror->GetOwner());
    Query.AddIgnoredActor(&Player);
    FHitResult Floor;
    return GetWorld()->LineTraceSingleByChannel(Floor, Base + FVector(0, 0, 4), Base - FVector(0, 0, 7),
        ECC_Visibility, Query) && Floor.ImpactNormal.Z >= 0.7;
}

const AActor* APowerLabGameMode::GetAimOwner(EOpticsAim Aim) const
{
    const UBoxComponent* Device = Aim == EOpticsAim::Emitter ? Reflection.Emitter.Get() : Mirror.Get();
    return IsValid(Device) ? Device->GetOwner() : nullptr;
}

void APowerLabGameMode::AimAt(EOpticsAim Target, FVector Point)
{
    if (Target == EOpticsAim::Emitter)
    {
        const FVector Direction = (Point - Reflection.Emitter->GetComponentLocation()).GetSafeNormal();
        if (!Direction.IsNearlyZero())
        {
            Reflection.Direction = Direction;
        }
    }
    else if (Target == EOpticsAim::Mirror && IsValid(MirrorSurface))
    {
        FVector Pivot = MirrorSurface->GetComponentLocation();
        const FVector Start = Reflection.Emitter->GetComponentLocation();
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(AimMirror), false, Reflection.Emitter->GetOwner());
        FHitResult Incoming;
        GetWorld()->LineTraceSingleByChannel(Incoming, Start, Start + Reflection.Direction * BeamReachCm, OpticalBeam, Query);
        if (Incoming.GetComponent() == MirrorSurface)
        {
            Pivot = Incoming.ImpactPoint;
        }
        const FVector Incident = (Pivot - Start).GetSafeNormal();
        const FVector Desired = (Point - Pivot).GetSafeNormal();
        const FVector Normal = (Desired - Incident).GetSafeNormal();
        if (!Desired.IsNearlyZero() && !Normal.IsNearlyZero())
        {
            MirrorSurface->SetWorldRotation(Normal.Rotation());
        }
    }
}

void APowerLabGameMode::DrawBeam(FVector Start, FVector End, int32& Index)
{
    const FVector Span = End - Start;
    UStaticMeshComponent& Segment = *Beams[Index++];
    Segment.SetWorldLocationAndRotation((Start + End) * 0.5, Span.Rotation());
    Segment.SetWorldScale3D(FVector(Span.Length() / 100.0, 0.022, 0.022));
    Segment.SetVisibility(true);
}

bool APowerLabGameMode::TraceLane(FOpticalLane& Lane, bool bPowered, bool bAcceptReflection, int32& BeamIndex)
{
    if (!bPowered)
    {
        return false;
    }
    FVector Start = Lane.Emitter->GetComponentLocation();
    FVector Direction = Lane.Direction;
    double Range = BeamReachCm;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(OpticalLane), false, Lane.Emitter->GetOwner());
    for (int32 Segment = 0; Segment < 2; ++Segment)
    {
        const FVector End = Start + Direction * Range;
        FHitResult Hit;
        const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, OpticalBeam, Query);
        DrawBeam(Start, bHit ? Hit.ImpactPoint : End, BeamIndex);
        if (Hit.GetComponent() == Lane.Receiver)
        {
            return !bAcceptReflection || Segment > 0;
        }
        if (!bHit || Segment > 0 || !bAcceptReflection || !IsValid(MirrorSurface) ||
            Hit.GetComponent() != MirrorSurface || Abandoned::Infrastructure::Puzzles::IsDoorClamped(Mirror))
        {
            return false;
        }
        const FVector Normal = MirrorSurface->GetForwardVector();
        if (!Abandoned::Domain::Puzzles::CanReflectFromFace(
            FVector::DotProduct(Hit.ImpactNormal, Normal), FVector::DotProduct(Direction, Normal)))
        {
            return false;
        }
        Direction = Direction.MirrorByVector(Normal).GetSafeNormal();
        Query.AddIgnoredActor(Mirror->GetOwner());
        Range -= Hit.Distance + 2.0;
        if (Range <= 0.0)
        {
            return false;
        }
        Start = Hit.ImpactPoint + Direction * 2.0;
    }
    return false;
}

bool APowerLabGameMode::MoveDoor(UBoxComponent* Door, Abandoned::Infrastructure::Puzzles::SlidingDoorClamp& Clamp,
    const FVector& Closed, bool bOpen, float Delta, APowerLabCharacter& Player, const FVector* LocalRetry)
{
    using namespace Abandoned::Domain::Puzzles;
    if (!IsValid(Door))
    {
        Clamp.Release();
        return false;
    }
    const auto Sweep = Clamp.Move(*Door, Closed.Z, DoorTravelCm, DoorSpeedCm * Delta, bOpen);
    if (!Sweep.Closing || !Sweep.Hit.bBlockingHit)
    {
        return false;
    }
    UBoxComponent* Body = Cast<UBoxComponent>(Sweep.Hit.GetComponent());
    const bool bPortable = Body && (Body == Cube->GetBody() || (Body == Mirror && !bMirrorDocked));
    const DoorContact Contact = Sweep.Hit.GetActor() == &Player ? DoorContact::Player :
        bPortable ? DoorContact::Carryable : DoorContact::None;
    switch (ResolveClosingContact(Sweep.Closing, Contact, true))
    {
    case ClosingContact::Clamp:
        if (Player.GetHeldBody() == Body)
        {
            Player.ReleaseHeldObject();
        }
        Clamp.Capture(*Body);
        break;
    case ClosingContact::ResetPlayer:
        if (LocalRetry)
        {
            Player.ReleaseHeldObject();
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, *LocalRetry));
        }
        else
        {
            ResetLab(Player);
        }
        return true;
    case ClosingContact::Block:
        break;
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
        for (auto* Clamp : {&Interruption.Clamp, &Reflection.Clamp, &PulseClamp})
        {
            if (Clamp->Holds(Cube->GetBody()))
            {
                Clamp->Release();
            }
        }
        if (Player->GetHeldCube() == Cube)
        {
            Player->ReleaseHeldObject();
        }
        Cube->ResetPosition(Layout->Cube);
    }
    if (Mirror->GetComponentLocation().Z < -240.0)
    {
        if (Player->GetHeldBody() == Mirror)
        {
            Player->ReleaseHeldObject();
        }
        RestoreMirror();
        Player->Aim = EOpticsAim::None;
    }
    const auto Power = Course.GetPower();
    int32 BeamIndex = 0;
    bDirectSignal = TraceLane(Interruption, Power.DirectEmitter, false, BeamIndex);
    bReflectedSignal = TraceLane(Reflection, Power.ReflectionEmitter, true, BeamIndex);
    for (; BeamIndex < Beams.Num(); ++BeamIndex)
    {
        Beams[BeamIndex]->SetVisibility(false);
    }
    const auto Doors = Course.ObserveReceivers(bDirectSignal, bReflectedSignal, Delta);
    ButtonLamp->SetMaterial(0, Power.DirectEmitter ? Amber : Inactive);
    Interruption.EmitterLamp->SetMaterial(0, Power.DirectEmitter ? Cyan : Inactive);
    Reflection.EmitterLamp->SetMaterial(0, Power.ReflectionEmitter ? Cyan : Inactive);
    Interruption.ReceiverLamp->SetMaterial(0, bDirectSignal ? Cyan : Inactive);
    Reflection.ReceiverLamp->SetMaterial(0, bReflectedSignal ? Cyan : Inactive);
    Interruption.PowerLine->SetMaterial(0, Doors.Interruption ? Amber : Inactive);
    Reflection.PowerLine->SetMaterial(0, Doors.Reflection ? Amber : Inactive);
    ObservePulse(Delta, Player->GetActorLocation());
    if (MoveDoor(Interruption.Door, Interruption.Clamp, Layout->Interruption.Door, Doors.Interruption, Delta, *Player) ||
        MoveDoor(Reflection.Door, Reflection.Clamp, Layout->Reflection.Door, Doors.Reflection, Delta, *Player) ||
        MoveDoor(PulseDoor, PulseClamp, Layout->PulseDoor.Center, Pulse.IsPowered(), Delta, *Player, &Layout->PulseRetry))
    {
        return;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerify)
    {
        Verify(*Player, Delta);
    }
    else if (bVerifyPulse)
    {
        VerifyPulse(*Player, Delta);
    }
#endif
}

const TCHAR* APowerLabGameMode::GetObjective(const FVector& Player) const
{
    if (IsInPulseBay(Player))
    {
        if (PulseClamp.HasObject())
        {
            return TEXT("Timed door obstructed. Start a pulse to release the item.");
        }
        if (Player.X > Layout->PulseDoor.Center.X)
        {
            return Pulse.IsComplete() ? TEXT("Timed passage complete. Use the return switch [E].")
                : TEXT("Clear the doorway.");
        }
        return Pulse.IsPowered() ? TEXT("Pulse active. Cross the marked door.")
            : TEXT("Use the pulse switch [E], then cross before the timer expires.");
    }
    if (Interruption.Clamp.HasObject() || Reflection.Clamp.HasObject())
    {
        return TEXT("Door obstructed. Restore its signal to release the item.");
    }
    if (Player.X > Layout->Interruption.Door.X + 60.0)
    {
        if (bReflectedSignal)
        {
            return TEXT("Signal received. Go through the open door.");
        }
        return TEXT("Place mirror [E]. Aim emitter at mirror [E] and mirror at receiver [Q].");
    }
    return Course.GetPower().DirectEmitter ? TEXT("Interrupt the beam with the cube or your suit.")
        : TEXT("Turn on the signal at the wall switch [E].");
}

#if WITH_DEV_AUTOMATION_TESTS
void APowerLabGameMode::Verify(APowerLabCharacter& Player, float Delta)
{
    VerificationTime += Delta;
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogPowerLab, Error, TEXT("Power lab verification failed at step %d: %s"), VerificationStep, Reason);
        bVerify = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto Next = [this](const TCHAR* Message)
    {
        UE_LOG(LogPowerLab, Display, TEXT("Power lab: %s"), Message);
        ++VerificationStep;
        VerificationTime = 0.0f;
    };
    const auto LookFrom = [&Player](FVector Feet, FVector At)
    {
        FVector Center(Feet.X, Feet.Y, 92.0);
        const FVector Eye = Center + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((At - Eye).Rotation(), Center));
    };
    const FVector MirrorPlacement = Layout->MirrorDock +
        (bVerifyFreeReflector ? FVector(-140, 125, 0) : FVector::ZeroVector);
    if (VerificationTime > 6.0f)
    {
        if (bVerifyFreeReflector && VerificationStep >= 6 && VerificationStep <= 8)
        {
            UE_LOG(LogPowerLab, Error, TEXT("Free mirror pose %s, up %.3f, speed %.2f, spin %.2f, aimable %d."),
                *Mirror->GetComponentLocation().ToCompactString(), Mirror->GetUpVector().Z,
                Mirror->GetPhysicsLinearVelocity().Size(), Mirror->GetPhysicsAngularVelocityInDegrees().Size(),
                CanAimMirror(Player));
        }
        Fail(TEXT("Timed out waiting for a physical transition."));
        return;
    }
    switch (VerificationStep)
    {
    case 0:
        if (VerificationTime < 0.2f)
        {
            return;
        }
        if (Course.GetPower().DirectEmitter || Course.GetPower().ReflectionEmitter)
        {
            Fail(TEXT("The unpowered lab emitted a signal."));
            return;
        }
        LookFrom(Layout->Button + FVector(0, 135, 0), Layout->Button);
        Player.Interact();
        Next(TEXT("Used the supply button."));
        break;
    case 1:
        if (bDirectSignal)
        {
            LookFrom(Layout->Interruption.Emitter + FVector(0, 320, 0), Layout->Interruption.Receiver);
            Next(TEXT("Receiver acquired the direct beam."));
        }
        break;
    case 2:
        if (!bDirectSignal && VerificationTime >= 0.12f)
        {
            Player.ResetChamber();
            if (Course.GetPower().DirectEmitter || !Interruption.Door->GetComponentLocation().Equals(Layout->Interruption.Door))
            {
                Fail(TEXT("Reset left the circuit or first door active."));
                return;
            }
            LookFrom(Layout->Button + FVector(0, 135, 0), Layout->Button);
            Player.Interact();
            Next(TEXT("Suit blocked the beam and reset cleared the circuit."));
        }
        break;
    case 3:
        if (bDirectSignal)
        {
            Cube->ResetPosition(FVector(Layout->Interruption.Emitter.X, 20.0, 42.0));
            Next(TEXT("Put a physical ballast cube across the beam."));
        }
        break;
    case 4:
        if (!bDirectSignal && Interruption.Door->GetComponentLocation().Z > Layout->Interruption.Door.Z + 250)
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Interruption.Door - FVector(135, 0, 38)));
            Next(TEXT("Loss of light enabled the second bay and opened the first door."));
        }
        break;
    case 5:
        Player.AddMovementInput(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Layout->Interruption.Door.X + 115)
        {
            LookFrom(Layout->Mirror + FVector(-125, 0, 0), Layout->Mirror);
            Player.Interact();
            if (Player.GetHeldBody() != Mirror)
            {
                Fail(TEXT("Could not pick up the portable mirror."));
                return;
            }
            Player.AimMirror();
            if (Player.IsAiming())
            {
                Fail(TEXT("held reflector entered adjustment mode"));
                return;
            }
            LookFrom(MirrorPlacement + FVector(0, 127, 0), MirrorPlacement);
            Next(TEXT("Crossed the door and picked up the mirror."));
        }
        break;
    case 6:
        if (FVector::DistSquared(Mirror->GetComponentLocation(), MirrorPlacement) < FMath::Square(45.0))
        {
            Player.Interact();
            if (bMirrorDocked == bVerifyFreeReflector || Player.GetHeldBody())
            {
                Fail(TEXT("Mirror placement did not enter the expected support state."));
                return;
            }
            LookFrom(Layout->Reflection.Emitter + FVector(-105, 100, 0), Layout->Reflection.Emitter);
            Player.Interact();
            if (Player.Aim != EOpticsAim::Emitter)
            {
                Fail(TEXT("Emitter interaction did not enter aiming."));
                return;
            }
            LookFrom(Player.GetActorLocation(), MirrorSurface->GetComponentLocation());
            Next(bVerifyFreeReflector ? TEXT("Placed the carried mirror off the dock and aimed the emitter.")
                : TEXT("Docked the carried mirror and entered live emitter aiming."));
        }
        break;
    case 7:
        if (VerificationTime >= 0.12f && CanAimMirror(Player))
        {
            Player.Interact();
            LookFrom(Mirror->GetComponentLocation() + FVector(-119, 72, 0), MirrorSurface->GetComponentLocation());
            Player.AimMirror();
            if (Player.Aim != EOpticsAim::Mirror)
            {
                Fail(TEXT("Mirror interaction did not enter aiming."));
                return;
            }
            LookFrom(Player.GetActorLocation(), Layout->Reflection.Receiver);
            Next(TEXT("Aimed the reflector with the same trace as mouse input."));
        }
        break;
    case 8:
        if (bReflectedSignal && Reflection.Door->GetComponentLocation().Z > Layout->Reflection.Door.Z + 250)
        {
            Player.Interact();
            if (bCapture)
            {
                LookFrom(MirrorPlacement + FVector(-380, -120, 0),
                    (MirrorSurface->GetComponentLocation() + Layout->Reflection.Receiver) * 0.5);
            }
            Next(TEXT("Real reflected ray reached its receiver and opened the exit."));
            if (bCapture)
            {
                VerificationStep = 16;
            }
            else if (bVerifyFreeReflector)
            {
                VerificationStep = 13;
            }
        }
        break;
    case 9:
        if (VerificationTime >= 0.15f)
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Reflection.Door - FVector(130, 0, 38)));
            Next(TEXT("Approaching the powered exit."));
        }
        break;
    case 10:
        Player.AddMovementInput(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Layout->Reflection.Door.X + 95)
        {
            bMirrorDocked = false;
            FVector UnderDoor = Layout->Reflection.Door;
            UnderDoor.Z += (MirrorSize.Z - DoorSize.Z) * 0.5 + 1.0;
            Mirror->SetWorldLocationAndRotation(UnderDoor, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
            Mirror->SetSimulatePhysics(true);
            Mirror->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Mirror->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
            Reflection.Direction = FVector::UpVector;
            Next(TEXT("Optical exit passed. Closing the unpowered door over the mirror."));
        }
        break;
    case 11:
        if (Reflection.Clamp.Holds(Mirror))
        {
            if (Mirror->IsSimulatingPhysics())
            {
                Fail(TEXT("the door did not retain the mirror"));
                return;
            }
            Next(TEXT("Optical door clamped the portable mirror."));
        }
        break;
    case 12:
        MoveDoor(Reflection.Door, Reflection.Clamp, Layout->Reflection.Door, true, Delta, Player);
        if (!Reflection.Clamp.HasObject())
        {
            if (!Mirror->IsSimulatingPhysics() || !FMath::IsNearlyEqual(Mirror->GetMass(), MirrorMassKg, 0.05f))
            {
                Fail(TEXT("opening did not restore mirror physics and mass"));
                return;
            }
            UE_LOG(LogPowerLab, Display, TEXT("Power lab verification passed: switch, blockers, reflection, exit and mirror clamp."));
            bVerify = false;
            bVerifyPulse = true;
        }
        break;
    case 13:
        if (VerificationTime >= 0.15f)
        {
            if (bMirrorDocked || !Mirror->IsSimulatingPhysics() ||
                FVector::Dist2D(Mirror->GetComponentLocation(), Layout->MirrorDock) <= DockRangeCm)
            {
                Fail(TEXT("the free reflector route used the dock"));
                return;
            }
            LookFrom(Mirror->GetComponentLocation() + FVector(-119, 72, 0), MirrorSurface->GetComponentLocation());
            Player.AimMirror();
            if (Player.Aim != EOpticsAim::Mirror)
            {
                Fail(TEXT("a settled reflector outside the dock could not be aimed again"));
                return;
            }
            LookFrom(Player.GetActorLocation(), Layout->Reflection.Receiver + FVector(0, 240, 75));
            Next(TEXT("Turned the free reflector away from its receiver."));
        }
        break;
    case 14:
        if (!bReflectedSignal && Reflection.Door->GetComponentLocation().Equals(Layout->Reflection.Door, 1.0))
        {
            LookFrom(Player.GetActorLocation(), Layout->Reflection.Receiver);
            Next(TEXT("Turning off target closed the real door. Aimed at the receiver again."));
        }
        break;
    case 15:
        if (bReflectedSignal && Reflection.Door->GetComponentLocation().Z > Layout->Reflection.Door.Z + 250)
        {
            Player.Interact();
            UE_LOG(LogPowerLab, Display, TEXT("Free reflector verification passed: placed off-dock, lost signal and reopened."));
            Next(TEXT("Free reflector solution restored the reflected signal."));
            VerificationStep = 9;
        }
        break;
    case 16:
        if (VerificationTime >= 0.18f)
        {
            FScreenshotRequest::RequestScreenshot(bVerifyFreeReflector ? TEXT("PowerLab-Free-Reflector")
                : TEXT("PowerLab-Reflection"), false, false);
            VerificationStep = bVerifyFreeReflector ? 13 : 9;
            VerificationTime = 0.0f;
        }
        break;
    }
}
#endif

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

bool APowerLabCharacter::InteractWithDevice(UPrimitiveComponent& Target)
{
    APowerLabGameMode* Lab = GetWorld()->GetAuthGameMode<APowerLabGameMode>();
    return Lab && Lab->Interact(*this, Target);
}

void APowerLabCharacter::PlaceHeldDevice(UBoxComponent& Body)
{
    if (APowerLabGameMode* Lab = GetWorld()->GetAuthGameMode<APowerLabGameMode>(); Lab && &Body == Lab->Mirror)
    {
        Lab->PlaceMirror(*this);
    }
}

void APowerLabCharacter::ResetChamber()
{
    if (APowerLabGameMode* Lab = GetWorld()->GetAuthGameMode<APowerLabGameMode>())
    {
        Lab->ResetLab(*this);
    }
}

void APowerLabCharacter::AimMirror()
{
    const APowerLabGameMode* Lab = GetWorld()->GetAuthGameMode<APowerLabGameMode>();
    if (Lab && Lab->IsMirrorTarget(TraceInteractable()) && Lab->CanAimMirror(*this))
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
    const FVector End = Start + GetViewRotation().Vector() * BeamReachCm;
    const AActor* Device = Lab->GetAimOwner(Aim);
    if (!IsValid(Device) || FVector::DistSquared(Device->GetActorLocation(), Start) > FMath::Square(380.0) ||
        (Aim == EOpticsAim::Mirror && !Lab->CanAimMirror(*this)))
    {
        Aim = EOpticsAim::None;
        return;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(AimOptics), false, this);
    Query.AddIgnoredActor(Device);
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);
    Lab->AimAt(Aim, Hit.bBlockingHit ? Hit.ImpactPoint : End);
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
