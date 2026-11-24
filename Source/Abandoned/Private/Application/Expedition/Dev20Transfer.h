#pragma once

#include "Domain/Expedition/Dev20.h"

namespace Abandoned::Application::Expedition
{
// Keep the expedition item in the current world or in one pending hand-off.
class Dev20Transfer
{
public:
    using Stage = Domain::Expedition::Dev20Stage;

    std::optional<Stage> Depart(Stage From, bool Held)
    {
        const auto Next = Domain::Expedition::NextDev20Stage(From);
        if (bAbandoned || !Held || Current != From || Pending || !Next)
        {
            return std::nullopt;
        }
        Pending = Next;
        return Pending;
    }

    bool AbandonHeldUnit(bool Held)
    {
        if (!Domain::Expedition::CanAbandonDev20(Held, Pending.has_value(), bAbandoned)) { return false; }
        bAbandoned = true;
        return true;
    }

    bool EvacuationLost() const { return bAbandoned; }
    bool IsExpected(Stage Destination) const { return Pending == Destination; }

    bool Arrive(Stage Destination)
    {
        if (Pending != Destination)
        {
            return false;
        }
        Current = Destination;
        Pending.reset();
        return true;
    }

    void RecoverAtStation()
    {
        bAbandoned = false;
        Current = Stage::Station;
        Pending.reset();
    }

private:
    bool bAbandoned = false;
    Stage Current = Stage::Station;
    std::optional<Stage> Pending;
};
}
