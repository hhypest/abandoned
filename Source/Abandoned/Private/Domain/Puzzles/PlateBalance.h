#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace Abandoned::Domain::Puzzles
{
struct PlateBalanceRules
{
    float TotalKg;
    float ToleranceKg;
};

class PlateBalance
{
public:
    void Observe(const PlateBalanceRules& Rules, std::span<const float> Loads, float DeltaSeconds)
    {
        constexpr float SettleSeconds = 0.25f;
        const float Elapsed = std::isfinite(DeltaSeconds) ? std::max(0.0f, DeltaSeconds) : 0.0f;
        ContactSeconds = AtTarget(Rules, Loads) ? std::min(ContactSeconds + Elapsed, SettleSeconds) : 0.0f;
        Balanced = ContactSeconds >= SettleSeconds;
    }

    void Reset()
    {
        ContactSeconds = 0.0f;
        Balanced = false;
    }

    bool IsBalanced() const { return Balanced; }

private:
    static bool AtTarget(const PlateBalanceRules& Rules, std::span<const float> Loads)
    {
        if (Loads.size() < 2 || !std::isfinite(Rules.TotalKg) || Rules.TotalKg <= 0.0f ||
            !std::isfinite(Rules.ToleranceKg) || Rules.ToleranceKg < 0.0f)
        {
            return false;
        }
        const float Target = Rules.TotalKg / static_cast<float>(Loads.size());
        if (Rules.ToleranceKg >= Target)
        {
            return false;
        }
        float Total = 0.0f;
        for (const float Kg : Loads)
        {
            if (!std::isfinite(Kg) || std::abs(Kg - Target) > Rules.ToleranceKg)
            {
                return false;
            }
            Total += Kg;
        }
        return std::abs(Total - Rules.TotalKg) <= Rules.ToleranceKg;
    }

    float ContactSeconds = 0.0f;
    bool Balanced = false;
};
}
