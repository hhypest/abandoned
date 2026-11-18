#include "Infrastructure/Puzzles/PortalTransitComponent.h"

#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Infrastructure/Puzzles/UnrealPortalShape.h"
#include "Infrastructure/Puzzles/UnrealPortalTransform.h"
#include "Player/PuzzleCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPortalTransit, Log, All);

using Abandoned::Domain::Puzzles::PortalEvent;

namespace
{
constexpr double Clearance = 6.0;
}

void UPortalTransitComponent::Configure(const FTransform& Arrival, const FTransform& Departure, FVector2D ApertureHalfSize)
{
    Poses[0] = Arrival;
    Poses[1] = Departure;
    HalfSize = ApertureHalfSize;
}

bool UPortalTransitComponent::Fits(const FVector& Local, const FVector& Extent) const
{
    return Abandoned::Domain::Puzzles::FitsPortal(Local.Y, Local.Z, HalfSize.X, HalfSize.Y, Extent.Y, Extent.Z);
}

bool UPortalTransitComponent::IntersectsAperture(const FVector& Local, const FVector& Extent) const
{
    return FMath::Abs(Local.Y) < HalfSize.X + Extent.Y && FMath::Abs(Local.Z) < HalfSize.Y + Extent.Z;
}

bool UPortalTransitComponent::ExitClear(const UPrimitiveComponent& Body, const FTransform& Destination,
    const UPrimitiveComponent* Other) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PortalClearance), false, Body.GetOwner());
    if (Other)
    {
        Query.AddIgnoredActor(Other->GetOwner());
    }
    return !GetWorld()->OverlapBlockingTestByChannel(Destination.GetLocation(), Destination.GetRotation(),
        Body.GetCollisionObjectType(), Body.GetCollisionShape(-0.7f), Query,
        FCollisionResponseParams(Body.GetCollisionResponseToChannels()));
}

PortalEvent UPortalTransitComponent::Observe(UPrimitiveComponent& Body, FPortalTravelerState& State, bool bPowered)
{
    AActor* Owner = Body.GetOwner();
    if (!IsValid(Owner))
    {
        State = {};
        return PortalEvent::None;
    }
    APuzzleCharacter* Player = Cast<APuzzleCharacter>(Owner);
    const FVector Here = Body.GetComponentLocation();
    const FVector Previous = State.bTracked ? State.Previous : Here;
    const int32 PriorApproach = State.Approach;
    State.Approach = INDEX_NONE;
    State.Previous = Here;
    State.bTracked = true;
    UBoxComponent* Held = Player ? Player->GetHeldBody() : nullptr;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Poses); ++Index)
    {
        const FTransform& Source = Poses[Index];
        const FTransform& Destination = Poses[1 - Index];
        const FVector Local = Source.InverseTransformPositionNoScale(Here);
        const FVector Last = Source.InverseTransformPositionNoScale(Previous);
        const auto Measured = Abandoned::Infrastructure::Puzzles::TryPortalExtent(Body, Source);
        if (!Measured)
        {
            if (!State.bUnsupportedShape)
            {
                UE_LOG(LogPortalTransit, Warning, TEXT("Portal transit rejected unsupported collision component %s (%s)."),
                    *Body.GetName(), *Body.GetClass()->GetName());
            }
            State = {};
            State.bUnsupportedShape = true;
            return PortalEvent::None;
        }
        State.bUnsupportedShape = false;
        const FVector Extent = Measured.GetValue();
        const bool bCrossedPlane = Last.X > 0.0 && Local.X <= 0.0;
        const FVector Threshold = bCrossedPlane ? FMath::Lerp(Last, Local, Last.X / (Last.X - Local.X)) : Local;
        const bool bCrossed = bCrossedPlane && FMath::Abs(Threshold.Y) <= HalfSize.X &&
            FMath::Abs(Threshold.Z) <= HalfSize.Y;
        bool bFits = Fits(Threshold, Extent);
        const bool bMovingIn = Last.X >= Local.X - 0.1;
        bool bTouching = Local.X >= 0.0 && Local.X <= Extent.X + 1.0 && bMovingIn && IntersectsAperture(Local, Extent);
        FVector HeldExtent = FVector::ZeroVector;
        if (Held && (bTouching || bCrossed || (Local.X > 0.0 && Local.X <= 220.0)))
        {
            const FVector HeldLocal = Source.InverseTransformPositionNoScale(Held->GetComponentLocation());
            HeldExtent = Abandoned::Infrastructure::Puzzles::PortalBoxExtent(*Held, Source);
            bFits = bFits && Fits(HeldLocal, HeldExtent);
            bTouching = bTouching || (Local.X > 0.0 && bMovingIn && HeldLocal.X >= -180.0 &&
                HeldLocal.X <= HeldExtent.X && IntersectsAperture(HeldLocal, HeldExtent));
        }

        FTransform Through;
        FTransform Target;
        bool bClear = false;
        if (bPowered && bCrossed && bFits)
        {
            Through = Abandoned::Infrastructure::Puzzles::PortalLinkTransform(Source, Destination);
            const double Out = Destination.InverseTransformPositionNoScale(Through.TransformPosition(Here)).X;
            double ForwardOffset = Extent.X + Clearance - Out;
            if (Held)
            {
                const double HeldOut = Destination.InverseTransformPositionNoScale(
                    Through.TransformPosition(Held->GetComponentLocation())).X;
                ForwardOffset = FMath::Max(ForwardOffset, HeldExtent.X + Clearance - HeldOut);
            }
            Through.AddToTranslation(Destination.GetUnitAxis(EAxis::X) * FMath::Max(0.0, ForwardOffset));
            Target = Owner->GetActorTransform() * Through;
            bClear = ExitClear(Body, Target, Held) &&
                (!Held || ExitClear(*Held, Held->GetComponentTransform() * Through, &Body));
        }
        const PortalEvent Event = Abandoned::Domain::Puzzles::ObservePortal(
            bPowered, PriorApproach == Index, bTouching, bCrossed, bFits, bClear);
        if (Event == PortalEvent::Approach)
        {
            State.Approach = Index;
        }
        if (Event == PortalEvent::Interrupted)
        {
            return Event;
        }
        if (Event != PortalEvent::Blocked && Event != PortalEvent::Transfer)
        {
            continue;
        }

        const bool bTransferred = Event == PortalEvent::Transfer && (!Player || Player->TransitHeldBody(Through));
        const FVector Before = Player ? Player->GetCharacterMovement()->Velocity : Body.GetPhysicsLinearVelocity();
        const FVector Angular = Player ? FVector::ZeroVector : Body.GetPhysicsAngularVelocityInRadians();
        FVector After;
        if (bTransferred)
        {
            if (Player)
            {
                Player->SetBase(nullptr);
                if (AController* Controller = Player->GetController())
                {
                    Controller->SetControlRotation((Through.GetRotation() * Controller->GetControlRotation().Quaternion()).Rotator());
                }
            }
            Owner->SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
            After = Through.TransformVectorNoScale(Before);
            bExitOccupied = false;
#if WITH_DEV_AUTOMATION_TESTS
            InspectTransfer(Before, After);
#endif
        }
        else
        {
            const FVector Normal = Source.GetUnitAxis(EAxis::X);
            const FVector Correction = Normal * (Extent.X + Clearance - Local.X);
            if (Player && Held)
            {
                Player->TransitHeldBody(FTransform(Correction));
            }
            Owner->SetActorLocation(Owner->GetActorLocation() + Correction, false, nullptr, ETeleportType::TeleportPhysics);
            After = Before - FMath::Min(0.0, FVector::DotProduct(Before, Normal)) * Normal;
            bExitOccupied = true;
#if WITH_DEV_AUTOMATION_TESTS
            ++Stats.Blocked;
#endif
        }
        if (Player)
        {
            UCharacterMovementComponent& Movement = *Player->GetCharacterMovement();
            Movement.Velocity = After;
            Movement.UpdateComponentVelocity();
        }
        else
        {
            Body.SetPhysicsLinearVelocity(After);
            Body.SetPhysicsAngularVelocityInRadians(bTransferred ? Through.TransformVectorNoScale(Angular) : Angular);
        }
        State = {Body.GetComponentLocation(), INDEX_NONE, true};
        return bTransferred ? PortalEvent::Transfer : PortalEvent::Blocked;
    }
    return State.Approach != INDEX_NONE ? PortalEvent::Approach : PortalEvent::None;
}

#if WITH_DEV_AUTOMATION_TESTS
void UPortalTransitComponent::InspectTransfer(const FVector& Before, const FVector& After)
{
    ++Stats.Transfers;
    Stats.LastBefore = Before;
    Stats.LastAfter = After;
    UE_LOG(LogPortalTransit, Display, TEXT("Transfer %d: velocity %s -> %s, speed %.2f -> %.2f cm/s."),
        Stats.Transfers, *Before.ToCompactString(), *After.ToCompactString(), Before.Size(), After.Size());
}

#endif
