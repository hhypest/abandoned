#pragma once

namespace Abandoned::Domain::Expedition
{
inline constexpr double MantleHoldSeconds = 0.12;
inline constexpr double MaximumMantleRiseCm = 185.0;

constexpr bool CanReachMantleLedge(double RiseFromSupportCm, double RiseFromFeetCm, double Facing)
{
    return RiseFromSupportCm >= 110.0 && RiseFromSupportCm <= MaximumMantleRiseCm &&
        RiseFromFeetCm >= 28.0 && RiseFromFeetCm <= MaximumMantleRiseCm && Facing >= 0.7 && Facing <= 1.0;
}
}
