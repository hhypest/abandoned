#pragma once

namespace Abandoned::Domain::Puzzles
{
enum class DoorContact { None, Player, Carryable };
enum class ClosingContact { Block, Clamp, ResetPlayer };

constexpr ClosingContact ResolveClosingContact(bool Closing, DoorContact Contact, bool PlayerAtRisk)
{
    if (Closing && Contact == DoorContact::Carryable)
    {
        return ClosingContact::Clamp;
    }
    if (Closing && Contact == DoorContact::Player && PlayerAtRisk)
    {
        return ClosingContact::ResetPlayer;
    }
    return ClosingContact::Block;
}

constexpr bool CanReleaseDoorClamp(bool Opening, double DoorBottomCm, double ObjectTopCm)
{
    constexpr double ClearanceCm = 4.0;
    return Opening && DoorBottomCm - ObjectTopCm >= ClearanceCm;
}
}
