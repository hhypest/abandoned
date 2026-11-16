#include "Infrastructure/Expedition/UnrealMantle.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Domain/Expedition/MantleRules.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace Abandoned::Infrastructure::Expedition
{
namespace
{
const FName MantleLedgeTag(TEXT("PassageMantleLedge"));
}

void MarkMantleLedge(UBoxComponent& Ledge)
{
    Ledge.ComponentTags.AddUnique(MantleLedgeTag);
}

std::optional<MantlePath> FindMantlePath(const ACharacter& Player, double SupportHeight)
{
    const UCharacterMovementComponent& Movement = *Player.GetCharacterMovement();
    const UCapsuleComponent& Capsule = *Player.GetCapsuleComponent();
    if (Player.bIsCrouched || (!Movement.IsMovingOnGround() && !Movement.IsFalling()))
    {
        return std::nullopt;
    }
    const FVector Forward = Player.GetViewRotation().Vector().GetSafeNormal2D();
    if (Forward.IsNearlyZero())
    {
        return std::nullopt;
    }
    const FVector Start = Player.GetActorLocation();
    const double HalfHeight = Capsule.GetScaledCapsuleHalfHeight();
    const double Radius = Capsule.GetScaledCapsuleRadius();
    const double Feet = Start.Z - HalfHeight;
    const UWorld& World = *Player.GetWorld();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PassageMantle), false, &Player);
    const FCollisionResponseParams Response(Capsule.GetCollisionResponseToChannels());
    const ECollisionChannel Channel = Capsule.GetCollisionObjectType();

    const FVector Probe(Start.X, Start.Y, Feet + 22.0);
    FHitResult Face;
    if (!World.LineTraceSingleByChannel(Face, Probe, Probe + Forward * (Radius + 65.0), Channel, Query, Response))
    {
        return std::nullopt;
    }
    const UPrimitiveComponent* Ledge = Face.GetComponent();
    if (!Ledge || Ledge->Mobility != EComponentMobility::Static || !Ledge->ComponentHasTag(MantleLedgeTag) ||
        FMath::Abs(Face.ImpactNormal.Z) > 0.15)
    {
        return std::nullopt;
    }
    const FVector IntoLedge = -Face.ImpactNormal.GetSafeNormal2D();
    const FVector Footprint = Face.ImpactPoint + IntoLedge * (Radius + 16.0);
    FHitResult Top;
    const FVector TopStart(Footprint.X, Footprint.Y, Feet + Domain::Expedition::MaximumMantleRiseCm + 5.0);
    if (!World.LineTraceSingleByChannel(Top, TopStart, Footprint, Channel, Query, Response) ||
        Top.GetComponent() != Ledge || !Movement.IsWalkable(Top) ||
        !Domain::Expedition::CanReachMantleLedge(Top.ImpactPoint.Z - SupportHeight,
            Top.ImpactPoint.Z - Feet, FMath::Min(1.0, FVector::DotProduct(IntoLedge, Forward))))
    {
        return std::nullopt;
    }

    const FVector Landing(Footprint.X, Footprint.Y, Top.ImpactPoint.Z + HalfHeight + 3.0);
    const FVector Raised(Start.X, Start.Y, Landing.Z);
    const FCollisionShape Shape = Capsule.GetCollisionShape();
    if (World.SweepTestByChannel(Start, Raised, Capsule.GetComponentQuat(), Channel, Shape, Query, Response) ||
        World.SweepTestByChannel(Raised, Landing, Capsule.GetComponentQuat(), Channel, Shape, Query, Response) ||
        World.OverlapBlockingTestByChannel(Landing, Capsule.GetComponentQuat(), Channel, Shape, Query, Response))
    {
        return std::nullopt;
    }
    return MantlePath{Start, Raised, Landing};
}
}
