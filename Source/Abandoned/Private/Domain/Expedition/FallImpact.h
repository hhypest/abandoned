#pragma once

#include <cmath>

namespace Abandoned::Domain::Expedition
{
enum class FallImpact { None, MinorInjury, Fatal };

inline FallImpact AssessFall(double DropCm)
{
    constexpr double InjuryThresholdCm = 300.0;
    if (!std::isfinite(DropCm) || DropCm < InjuryThresholdCm)
    {
        return FallImpact::None;
    }
    return DropCm > InjuryThresholdCm ? FallImpact::Fatal : FallImpact::MinorInjury;
}
}
