#pragma once

#include <algorithm>

namespace Abandoned::Domain::Puzzles
{
enum class StackBlock { None, Size, Occupied, Unstable, Height };

constexpr StackBlock AssessStack(double LowerHalfSize, double UpperHalfSize, bool Occupied, bool Settled, int Depth)
{
    if (UpperHalfSize > LowerHalfSize)
    {
        return StackBlock::Size;
    }
    if (Occupied)
    {
        return StackBlock::Occupied;
    }
    if (!Settled)
    {
        return StackBlock::Unstable;
    }
    return Depth >= 3 ? StackBlock::Height : StackBlock::None;
}

constexpr float MaximumCarryKg = 40.0f;
constexpr float CarrySpeedScale(float Kg)
{
    return 1.0f - 0.45f * std::clamp(Kg / MaximumCarryKg, 0.0f, 1.0f);
}
}
