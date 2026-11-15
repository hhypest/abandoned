#pragma once

#include <cmath>

namespace Abandoned::Domain::Puzzles
{
enum class PortalEvent { None, Approach, Transfer, Blocked, Interrupted };

inline bool FitsPortal(double Horizontal, double Vertical, double HalfWidth, double HalfHeight,
    double BodyHalfWidth, double BodyHalfHeight)
{
    return std::abs(Horizontal) + BodyHalfWidth <= HalfWidth &&
        std::abs(Vertical) + BodyHalfHeight <= HalfHeight;
}

constexpr PortalEvent ObservePortal(bool Powered, bool WasApproaching, bool Touching, bool Crossed,
    bool Fits, bool ExitClear)
{
    if (!Powered)
    {
        return WasApproaching && (Touching || Crossed) ? PortalEvent::Interrupted : PortalEvent::None;
    }
    if (Crossed)
    {
        return Fits && ExitClear ? PortalEvent::Transfer : PortalEvent::Blocked;
    }
    return Touching && Fits ? PortalEvent::Approach : PortalEvent::None;
}
}
