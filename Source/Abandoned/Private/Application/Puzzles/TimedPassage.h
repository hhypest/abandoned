#pragma once

#include "Domain/Puzzles/PowerSignals.h"

namespace Abandoned::Application::Puzzles
{
class TimedPassage
{
public:
    explicit TimedPassage(float Duration)
        : Drive(Domain::Puzzles::SignalMode::Pulse, Duration) {}

    void Press()
    {
        if (!IsPowered())
        {
            Drive.Observe(true, 0.0f);
            Drive.Observe(false, 0.0f);
        }
    }

    void Observe(float DeltaSeconds, bool ReachedExit)
    {
        Drive.Observe(false, DeltaSeconds);
        Completed = Completed || ReachedExit;
    }

    void Reset()
    {
        Drive.Reset();
        Completed = false;
    }

    bool IsPowered() const { return Drive.IsPowered(); }
    bool IsComplete() const { return Completed; }
    float RemainingSeconds() const { return Drive.RemainingSeconds(); }

private:
    Domain::Puzzles::SignalOutput Drive;
    bool Completed = false;
};
}
