#include "Infrastructure/Puzzles/TrainingDoorDriveComponent.h"

#include "Components/BoxComponent.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "Expedition/Dev20Actor.h"
#include "Player/PuzzleCharacter.h"
#include "Puzzles/PuzzleCube.h"

namespace
{
constexpr double DoorTravel = 280.0;
}

void UTrainingDoorDriveComponent::Bind(UBoxComponent& Body, FVector ClosedLocation)
{
    Door = &Body;
    Closed = ClosedLocation;
    Clamp.AddSigns(Body);
}

bool UTrainingDoorDriveComponent::IsOpen() const
{
    const UBoxComponent* Leaf = Door.Get();
    return Leaf && !Clamp.HasObject() && Leaf->GetComponentLocation().Z >= Closed.Z + DoorTravel - 2.0;
}

ETrainingDoorResult UTrainingDoorDriveComponent::Move(APuzzleCharacter& Player, const ADev20Actor* Dev20,
    bool bOpening, bool bPlayerAtRisk, float DeltaSeconds)
{
    using namespace Abandoned::Domain::Puzzles;
    UBoxComponent* Leaf = Door.Get();
    if (!Leaf)
    {
        ReleaseClamp();
        return ETrainingDoorResult::Continue;
    }
    const auto Sweep = Clamp.Move(*Leaf, Closed.Z, DoorTravel, 350.0 * DeltaSeconds, bOpening);
    if (!Sweep.Closing || !Sweep.Hit.bBlockingHit)
    {
        return ETrainingDoorResult::Continue;
    }
    UBoxComponent* Body = Cast<UBoxComponent>(Sweep.Hit.GetComponent());
    const APuzzleCube* Cube = Cast<APuzzleCube>(Sweep.Hit.GetActor());
    const bool bPortable = Body && ((Cube && Cube->GetBody() == Body) || (IsValid(Dev20) && Dev20->GetBody() == Body));
    const DoorContact Contact = Sweep.Hit.GetActor() == &Player ? DoorContact::Player :
        bPortable ? DoorContact::Carryable : DoorContact::None;
    switch (ResolveClosingContact(Sweep.Closing, Contact, bPlayerAtRisk))
    {
    case ClosingContact::Clamp:
        if (Player.GetHeldBody() == Body)
        {
            Player.ReleaseHeldObject();
        }
        Clamp.Capture(*Body);
        break;
    case ClosingContact::ResetPlayer:
        return ETrainingDoorResult::ResetPlayer;
    case ClosingContact::Block:
        break;
    }
    return ETrainingDoorResult::Continue;
}

void UTrainingDoorDriveComponent::ReleaseIfHolding(const UBoxComponent* Body)
{
    if (Clamp.Holds(Body))
    {
        Clamp.Release();
    }
}

void UTrainingDoorDriveComponent::ReturnToClosedPosition()
{
    if (UBoxComponent* Leaf = Door.Get())
    {
        Leaf->SetWorldLocation(Closed, false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void UTrainingDoorDriveComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Clamp.Release();
    Super::EndPlay(Reason);
}
