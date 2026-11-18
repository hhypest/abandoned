#pragma once

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"

namespace Abandoned::Infrastructure::Puzzles
{
inline FVector PortalBoxExtent(const UBoxComponent& Box, const FTransform& Mouth)
{
    const FQuat Relative = Mouth.GetRotation().Inverse() * Box.GetComponentQuat();
    const FVector Size = Box.GetScaledBoxExtent();
    return Relative.GetAxisX().GetAbs() * Size.X + Relative.GetAxisY().GetAbs() * Size.Y +
        Relative.GetAxisZ().GetAbs() * Size.Z;
}

inline TOptional<FVector> TryPortalExtent(const UPrimitiveComponent& Body, const FTransform& Mouth)
{
    if (const auto* Box = Cast<UBoxComponent>(&Body))
    {
        return PortalBoxExtent(*Box, Mouth);
    }
    if (const auto* Capsule = Cast<UCapsuleComponent>(&Body))
    {
        const double Radius = Capsule->GetScaledCapsuleRadius();
        const double Segment = Capsule->GetScaledCapsuleHalfHeight() - Radius;
        const FVector Axis = Mouth.InverseTransformVectorNoScale(Capsule->GetUpVector());
        return FVector(Radius) + Axis.GetAbs() * Segment;
    }
    return {};
}
}
