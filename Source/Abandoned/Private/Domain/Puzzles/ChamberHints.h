#pragma once

#include <cmath>
#include <cstdint>

namespace Abandoned::Domain::Puzzles
{
using HintNodes = std::uint64_t;
inline constexpr HintNodes ArmedButtonHintNode = 1;
inline constexpr HintNodes BallastStackHintNode = 2;
inline constexpr int MaximumHintPlates = 62;

constexpr HintNodes PressurePlateHintNode(int Index)
{
    return Index >= 0 && Index < MaximumHintPlates ? HintNodes{1} << (Index + 2) : 0;
}

class ChamberHints
{
public:
    static constexpr int PageCount = 3;

    int AvailablePages() const { return Available; }

    void Observe(HintNodes CorrectNodes, bool ChamberComplete, double DeltaSeconds, double DelaySeconds)
    {
        if ((CorrectNodes & ~SeenNodes) != 0)
        {
            SeenNodes |= CorrectNodes;
            IdleSeconds = 0.0;
            return;
        }
        if (ChamberComplete || Available == PageCount || !std::isfinite(DelaySeconds) || DelaySeconds <= 0.0 ||
            !std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0)
        {
            return;
        }
        IdleSeconds += DeltaSeconds;
        while (Available < PageCount && IdleSeconds >= DelaySeconds)
        {
            ++Available;
            IdleSeconds -= DelaySeconds;
        }
    }

private:
    HintNodes SeenNodes = 0;
    double IdleSeconds = 0.0;
    int Available = 1;
};
}
