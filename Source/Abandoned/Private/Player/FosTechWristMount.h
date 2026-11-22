#pragma once

#include "CoreMinimal.h"

namespace Abandoned::WristMount
{
// The measured glass in the strapless EQP-01 mesh. Local X points out of the
// glass, -Y reads left to right and Z is up (the UWidgetComponent convention).
inline FTransform GlassInCasing()
{
    const FQuat Rotation = FRotationMatrix::MakeFromXZ(FVector(0, 48, 11), FVector(0, -11, 48)).ToQuat();
    constexpr double Width = 70.144, Height = 46.032;
    const FVector TopLeft(-46, 84, 51);
    return FTransform(Rotation, TopLeft + Rotation.RotateVector(FVector(0, -Width / 2, -Height / 2)));
}

inline FTransform CasingOnForearm()
{
    const FTransform Glass = GlassInCasing();
    constexpr double Scale = 0.16;
    const FQuat Rotation = Glass.GetRotation().Inverse();
    // terminal_l supplies the optical axes. Seat the back of the rugged casing
    // into the sleeve with the window just above the old mounting surface.
    return FTransform(Rotation, FVector(0.4, 0, 0) - Rotation.RotateVector(Scale * Glass.GetLocation()), FVector(Scale));
}
}
