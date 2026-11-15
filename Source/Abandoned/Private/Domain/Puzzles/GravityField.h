#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace Abandoned::Domain::Puzzles
{
enum class GravityDirection { Down, Left, Right, Up };

constexpr float GravityRollDegrees(GravityDirection Direction)
{
    switch (Direction)
    {
    case GravityDirection::Left: return -90.0f;
    case GravityDirection::Right: return 90.0f;
    case GravityDirection::Up: return 180.0f;
    case GravityDirection::Down: return 0.0f;
    }
    return 0.0f;
}

struct GravityField
{
    GravityDirection Direction;
    float CharacterScale;
    float Force;
    int Priority;
    bool Powered;
};

// The adapter passes only volumes containing the body, in stable layout order.
inline const GravityField* SelectGravityField(std::span<const GravityField> Containing)
{
    const GravityField* Selected = nullptr;
    for (const GravityField& Candidate : Containing)
    {
        if (Candidate.Powered && std::isfinite(Candidate.CharacterScale) && Candidate.CharacterScale > 0.0f &&
            std::isfinite(Candidate.Force) && Candidate.Force > 0.0f &&
            (!Selected || Candidate.Priority > Selected->Priority))
        {
            Selected = &Candidate;
        }
    }
    return Selected;
}

inline float GravityForceForBody(const GravityField& Field, float MassKg)
{
    if (!std::isfinite(MassKg) || MassKg <= 0.0f)
    {
        return 0.0f;
    }
    // A common force makes heavier bodies accelerate less. The cap limits tiny props.
    return std::min(Field.Force, 3600.0f * MassKg);
}
}
