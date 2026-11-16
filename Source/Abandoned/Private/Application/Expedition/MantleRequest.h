#pragma once

#include "Domain/Expedition/MantleRules.h"

#include <algorithm>
#include <cmath>

namespace Abandoned::Application::Expedition
{
class MantleRequest
{
public:
    void Press()
    {
        if (!Held)
        {
            Held = true;
            Seconds = 0.0;
        }
    }

    void Release()
    {
        Held = false;
        Consumed = false;
        Seconds = 0.0;
    }

    void Advance(double DeltaSeconds)
    {
        if (Held && !Consumed && std::isfinite(DeltaSeconds) && DeltaSeconds > 0.0)
        {
            Seconds = std::min(Seconds + DeltaSeconds, Domain::Expedition::MantleHoldSeconds);
        }
    }

    bool IsReady() const { return Held && !Consumed && Seconds >= Domain::Expedition::MantleHoldSeconds; }
    void Consume() { Consumed = true; }

private:
    double Seconds = 0.0;
    bool Held = false;
    bool Consumed = false;
};
}
