#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Domain/Puzzles/BallastRules.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Infrastructure/Puzzles/ObjectCarryComponent.h"

namespace Abandoned::Verification
{
inline bool VerifyRejectedCarryTransit(UObjectCarryComponent& Grip, UBoxComponent& Body,
    const UCharacterMovementComponent& Movement)
{
    const float UnloadedSpeed = Movement.MaxWalkSpeed;
    const float Mass = Body.GetMass();
    const ECollisionResponse Response = Body.GetCollisionResponseToChannel(ECC_Pawn);
    Body.SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    if (!Grip.TryGrab(Body, Mass))
    {
        Body.SetCollisionResponseToChannel(ECC_Pawn, Response);
        return false;
    }
    const bool Slowed = Movement.MaxWalkSpeed < UnloadedSpeed;
    // Make both destination grab and source re-grab fail with a real Chaos body.
    Body.SetMassOverrideInKg(NAME_None, Domain::Puzzles::MaximumCarryKg + 1.0f);
    const FTransform Original = Body.GetComponentTransform();
    Body.SetPhysicsLinearVelocity(FVector(21, 32, 43));
    Body.SetPhysicsAngularVelocityInRadians(FVector(1, 2, 3));
    const FVector Linear = Body.GetPhysicsLinearVelocity();
    const FVector Angular = Body.GetPhysicsAngularVelocityInRadians();
    const bool Moved = Grip.Transit(FTransform(FRotator(0, 90, 0), FVector(300, 400, 0)));
    const bool Restored = !Moved && !Grip.HeldBody() && Body.GetComponentTransform().Equals(Original) &&
        Body.GetPhysicsLinearVelocity().Equals(Linear, 0.01) &&
        Body.GetPhysicsAngularVelocityInRadians().Equals(Angular, 0.01) &&
        Body.GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap &&
        FMath::IsNearlyEqual(Movement.MaxWalkSpeed, UnloadedSpeed);
    Grip.ReleaseComponent();
    Body.SetMassOverrideInKg(NAME_None, Mass);
    Body.SetCollisionResponseToChannel(ECC_Pawn, Response);
    return Slowed && Restored;
}
}
#endif
