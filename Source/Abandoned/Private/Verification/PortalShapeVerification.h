#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Domain/Puzzles/PortalTransit.h"
#include "Infrastructure/Puzzles/UnrealPortalShape.h"

namespace Abandoned::Verification
{
inline bool VerifyPortalShapeProjection()
{
    using Infrastructure::Puzzles::TryPortalExtent;
    const FTransform Mouth(FRotator(0, 30, 0));
    auto* Capsule = NewObject<UCapsuleComponent>();
    Capsule->SetCapsuleSize(20, 60);
    const auto Upright = TryPortalExtent(*Capsule, Mouth);
    if (!Upright || !Upright->Equals(FVector(20, 20, 60))) { return false; }

    auto* Box = NewObject<UBoxComponent>();
    Box->SetBoxExtent(FVector(30, 10, 15));
    Box->SetWorldTransform(FTransform(Mouth.GetRotation(), FVector::ZeroVector, FVector(2, 1, 1)));
    const auto Aligned = TryPortalExtent(*Box, Mouth);
    Box->SetWorldRotation(FRotator(0, 75, 0));
    const auto Rotated = TryPortalExtent(*Box, Mouth);
    const double Diagonal = 70.0 / FMath::Sqrt(2.0);
    return Aligned && Rotated && Aligned->Equals(FVector(60, 10, 15)) &&
        Rotated->Equals(FVector(Diagonal, Diagonal, 15)) &&
        Domain::Puzzles::FitsPortal(80, 0, 118, 145, Aligned->Y, Aligned->Z) &&
        !Domain::Puzzles::FitsPortal(80, 0, 118, 145, Rotated->Y, Rotated->Z);
}
}
#endif
