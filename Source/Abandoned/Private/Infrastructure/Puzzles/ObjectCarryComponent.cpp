#include "Infrastructure/Puzzles/ObjectCarryComponent.h"

#include "Components/BoxComponent.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"

UObjectCarryComponent::UObjectCarryComponent()
{
    SetLinearStiffness(1800.0f);
    SetLinearDamping(180.0f);
    SetInterpolationSpeed(18.0f);
}

UBoxComponent* UObjectCarryComponent::HeldBody() const
{
    auto* Body = GetGrabbedComponent();
    return IsValid(Body) ? Cast<UBoxComponent>(Body) : nullptr;
}

bool UObjectCarryComponent::TryGrab(UBoxComponent& Body, float MassKg)
{
    if (HeldBody() || Abandoned::Infrastructure::Puzzles::IsDoorClamped(&Body) || !Body.IsSimulatingPhysics() ||
        !FMath::IsFinite(MassKg) || MassKg <= 0.0f || MassKg > Abandoned::Domain::Puzzles::MaximumCarryKg)
    {
        return false;
    }
    if (OriginalPawnResponse)
    {
        // The previous body may have disappeared between two character ticks.
        ReleaseComponent();
    }
    GrabComponentAtLocationWithRotation(&Body, NAME_None, Body.GetComponentLocation(), Body.GetComponentRotation());
    if (HeldBody() != &Body)
    {
        return false;
    }
    OriginalPawnResponse = Body.GetCollisionResponseToChannel(ECC_Pawn);
    if (AActor* Actor = Body.GetOwner())
    {
        HeldActor = Actor;
        Actor->OnEndPlay.AddUniqueDynamic(this, &UObjectCarryComponent::OnHeldActorEndPlay);
    }
    Body.SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    LoadChanged.ExecuteIfBound(MassKg);
    return true;
}

void UObjectCarryComponent::OnHeldActorEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
    ReleaseComponent();
}

void UObjectCarryComponent::ReleaseComponent()
{
    if (AActor* Actor = HeldActor.Get())
    {
        Actor->OnEndPlay.RemoveDynamic(this, &UObjectCarryComponent::OnHeldActorEndPlay);
    }
    HeldActor.Reset();
    if (OriginalPawnResponse)
    {
        if (auto* Body = HeldBody())
        {
            Body->SetCollisionResponseToChannel(ECC_Pawn, OriginalPawnResponse.GetValue());
        }
        OriginalPawnResponse.Reset();
        LoadChanged.ExecuteIfBound(0.0f);
    }
    Super::ReleaseComponent();
}

bool UObjectCarryComponent::Transit(const FTransform& Through)
{
    auto* Body = HeldBody();
    if (!Body)
    {
        return true;
    }
    const float Mass = Body->GetMass();
    const FTransform Original = Body->GetComponentTransform();
    const FVector Linear = Body->GetPhysicsLinearVelocity();
    const FVector Angular = Body->GetPhysicsAngularVelocityInRadians();
    ReleaseComponent();
    Body->SetWorldTransform(Original * Through, false, nullptr, ETeleportType::TeleportPhysics);
    if (TryGrab(*Body, Mass))
    {
        Body->SetPhysicsLinearVelocity(Through.TransformVectorNoScale(Linear));
        Body->SetPhysicsAngularVelocityInRadians(Through.TransformVectorNoScale(Angular));
        return true;
    }
    Body->SetWorldTransform(Original, false, nullptr, ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(Linear);
    Body->SetPhysicsAngularVelocityInRadians(Angular);
    TryGrab(*Body, Mass);
    return false;
}

void UObjectCarryComponent::Throw(const FVector& Velocity, const FVector& Impulse)
{
    auto* Body = HeldBody();
    if (Body && Body->IsSimulatingPhysics())
    {
        ReleaseComponent();
        Body->SetPhysicsLinearVelocity(Velocity);
        Body->AddImpulse(Impulse);
    }
}

void UObjectCarryComponent::UpdateHold(const FVector& Origin, const FRotator& View, double HoldDistance, double Reach)
{
    const UBoxComponent* Body = HeldBody();
    if (!Body)
    {
        if (OriginalPawnResponse)
        {
            ReleaseComponent();
        }
        return;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(TrainingHold), false, GetOwner());
    Query.AddIgnoredActor(Body->GetOwner());
    FHitResult Hit;
    const FVector Direction = View.Vector();
    const bool bWall = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * HoldDistance, ECC_Visibility, Query);
    const double Distance = bWall ? FMath::Clamp(Hit.Distance - Body->GetScaledBoxExtent().GetMax(), 0.0, HoldDistance) : HoldDistance;
    if (Distance < 75.0 || FVector::DistSquared(Origin, Body->GetComponentLocation()) > FMath::Square(2.0 * Reach))
    {
        ReleaseComponent();
        return;
    }
    SetTargetLocationAndRotation(Origin + Direction * Distance, FRotator(0.0, View.Yaw, 0.0));
}
