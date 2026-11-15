#pragma once

#include <algorithm>
#include <cmath>

namespace Abandoned::Domain::Puzzles
{
struct CircuitRules
{
    enum class Gate { Plate, ButtonAndPlate, StackLatch };
    float MinimumKg;
    float MaximumKg;
    Gate DoorGate;
};

struct CircuitState
{
    bool ButtonLatched = false;
    bool PlatePressed = false;
    bool ExitEnabled = false;
};

class ChamberCircuit
{
public:
    void PressButton() { State.ButtonLatched = true; }
    const CircuitState& GetState() const { return State; }

    void Observe(const CircuitRules& Rules, float GroundedKg, bool StackInZone, float DeltaSeconds)
    {
        constexpr float SettleSeconds = 0.1f;
        constexpr float MassToleranceKg = 0.01f;
        const bool HasLoad = std::isfinite(GroundedKg) &&
            GroundedKg >= Rules.MinimumKg - MassToleranceKg && GroundedKg <= Rules.MaximumKg + MassToleranceKg;
        const float Elapsed = std::isfinite(DeltaSeconds) ? std::max(DeltaSeconds, 0.0f) : 0.0f;
        ContactSeconds = HasLoad ? std::min(ContactSeconds + Elapsed, SettleSeconds) : 0.0f;
        State.PlatePressed = ContactSeconds >= SettleSeconds;
        switch (Rules.DoorGate)
        {
        case CircuitRules::Gate::Plate:
            State.ExitEnabled = State.PlatePressed;
            break;
        case CircuitRules::Gate::ButtonAndPlate:
            State.ExitEnabled = State.ButtonLatched && State.PlatePressed;
            break;
        case CircuitRules::Gate::StackLatch:
            State.ExitEnabled = State.ExitEnabled || StackInZone;
            break;
        }
    }

    void Reset()
    {
        State = {};
        ContactSeconds = 0.0f;
    }

private:
    CircuitState State;
    float ContactSeconds = 0.0f;
};
}
