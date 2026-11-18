#pragma once

#include "Math/Transform.h"

namespace Abandoned::Infrastructure::Puzzles
{
inline FTransform PortalLinkTransform(const FTransform& Source, const FTransform& Destination)
{
    const FQuat Turn(FVector::UpVector, UE_PI);
    const FQuat Rotation = Destination.GetRotation() * Turn * Source.GetRotation().Inverse();
    return FTransform(Rotation, Destination.GetLocation() - Rotation.RotateVector(Source.GetLocation()));
}
}
