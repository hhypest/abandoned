#pragma once

#include <cmath>

namespace Abandoned::Domain::Puzzles
{
inline bool IsReflectorSettled(double Upright, double LinearSpeedSq, double AngularSpeedSq)
{
    constexpr double MinimumUpright = 0.9;
    constexpr double MaximumSpeedCmPerSecond = 18.0;
    constexpr double MaximumDegreesPerSecond = 18.0;
    return std::isfinite(Upright) && Upright >= MinimumUpright &&
        LinearSpeedSq >= 0.0 && LinearSpeedSq <= MaximumSpeedCmPerSecond * MaximumSpeedCmPerSecond &&
        AngularSpeedSq >= 0.0 && AngularSpeedSq <= MaximumDegreesPerSecond * MaximumDegreesPerSecond;
}

constexpr bool CanReflectFromFace(double FaceAlignment, double Incidence)
{
    return FaceAlignment >= 0.99 && Incidence < -0.001;
}
}
