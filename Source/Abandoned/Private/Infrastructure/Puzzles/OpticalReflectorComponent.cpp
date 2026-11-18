#include "Infrastructure/Puzzles/OpticalReflectorComponent.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Domain/Puzzles/OpticalReflector.h"
#include "Engine/World.h"
#include "Infrastructure/Puzzles/OpticalCircuitComponent.h"
#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"
#include "Player/PuzzleCharacter.h"
#include "World/Blockout.h"

namespace
{
constexpr float MirrorMassKg = 8.0f;
constexpr double DockRangeCm = 100.0;
constexpr double MirrorPlacementDropCm = 60.0;
constexpr double MirrorPlacementClearanceCm = 0.5;
const FVector MirrorSize(66.0, 52.0, UOpticalReflectorComponent::HeightCm);
}

bool UOpticalReflectorComponent::Build(FVector Home, FVector Dock, UStaticMesh& Mesh, UMaterialInterface& Base, UMaterialInterface& Steel)
{
    HomeLocation = Home;
    DockLocation = Dock;
    const auto Parts = Abandoned::Blockout::SpawnBox(*GetWorld(), Mesh, HomeLocation, MirrorSize,
        EComponentMobility::Movable, TEXT("BlockAll"));
    Mirror = Parts.Collision;
    if (!Mirror || !Parts.Visual)
    {
        return false;
    }
    // Chaos uses the carry envelope. Optics and aiming trace the visible parts.
    Mirror->SetCollisionResponseToChannel(UOpticalCircuitComponent::BeamChannel, ECR_Ignore);
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
        Shape->SetCollisionResponseToChannel(UOpticalCircuitComponent::BeamChannel, ECR_Block);
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

void UOpticalReflectorComponent::RestoreMirror()
{
    bMirrorDocked = false;
    Mirror->SetSimulatePhysics(false);
    Mirror->SetWorldLocationAndRotation(HomeLocation, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
    MirrorSurface->SetRelativeRotation(FRotator(0, -45, 0));
    Mirror->SetSimulatePhysics(true);
    Mirror->SetMassOverrideInKg(NAME_None, MirrorMassKg);
    Mirror->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Mirror->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

void UOpticalReflectorComponent::PickUp(APuzzleCharacter& Player)
{
    if (!IsValid(Mirror) || Abandoned::Infrastructure::Puzzles::IsDoorClamped(Mirror))
    {
        return;
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
}

void UOpticalReflectorComponent::PlaceMirror(const APuzzleCharacter& Player)
{
    const FVector From = Mirror->GetComponentLocation();
    const FCollisionShape Shape = Mirror->GetCollisionShape();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ReflectorPlacement), false, Mirror->GetOwner());
    const FCollisionResponseParams Response(Mirror->GetCollisionResponseToChannels());
    const ECollisionChannel Channel = Mirror->GetCollisionObjectType();
    const UWorld& World = *GetWorld();
    if (FVector::DistSquared(From, DockLocation) <= FMath::Square(DockRangeCm) &&
        !World.OverlapBlockingTestByChannel(DockLocation, FQuat::Identity, Channel, Shape, Query, Response))
    {
        Mirror->SetSimulatePhysics(false);
        Mirror->SetWorldLocationAndRotation(DockLocation, FRotator::ZeroRotator,
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

bool UOpticalReflectorComponent::IsMirrorTarget(const UPrimitiveComponent* Target) const
{
    return IsValid(Target) && IsValid(Mirror) && Target->GetOwner() == Mirror->GetOwner();
}

bool UOpticalReflectorComponent::CanAimMirror(const APuzzleCharacter& Player) const
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

void UOpticalReflectorComponent::Align(FVector Start, FVector Pivot, FVector Point)
{
    const FVector Incident = (Pivot - Start).GetSafeNormal();
    const FVector Desired = (Point - Pivot).GetSafeNormal();
    const FVector Normal = (Desired - Incident).GetSafeNormal();
    if (!Desired.IsNearlyZero() && !Normal.IsNearlyZero())
    {
        MirrorSurface->SetWorldRotation(Normal.Rotation());
    }
}
