#pragma once

#include <algorithm>
#include <cmath>

namespace Abandoned::Domain::Expedition
{
class SuitSupply
{
public:
    float Percent() const { return Oxygen; }
    void Refill() { Oxygen = 100.0f; }

    bool SpendInPassage(float DeltaSeconds)
    {
        constexpr float PercentPerSecond = 0.18f;
        const float Elapsed = std::isfinite(DeltaSeconds) ? std::max(DeltaSeconds, 0.0f) : 0.0f;
        Oxygen = std::max(0.0f, Oxygen - Elapsed * PercentPerSecond);
        return Oxygen <= 0.0f;
    }

private:
    float Oxygen = 100.0f;
};
}
