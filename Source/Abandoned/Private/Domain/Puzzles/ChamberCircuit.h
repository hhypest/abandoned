#pragma once

#include <algorithm>
#include <cmath>

#include "Domain/Puzzles/PowerSignals.h"

namespace Abandoned::Domain::Puzzles
{
struct CircuitRules
{
    enum class Gate { Plate, ButtonAndPlate, StackLatch, ButtonAndParallelPlates };
    float MinimumKg;
    float MaximumKg;
    Gate DoorGate;
};

struct CircuitState
{
    bool ButtonLatched = false;
    bool PlatePressed = false;
    bool AlternatePlatePressed = false;
    bool ParallelOutput = false;
    bool ExitEnabled = false;
};

class ChamberCircuit
{
public:
    void PressButton() { State.ButtonLatched = true; }
    const CircuitState& GetState() const { return State; }

    void Observe(const CircuitRules& Rules, float GroundedKg, bool StackInZone, float DeltaSeconds, float AlternateKg = 0.0f)
    {
        const float Elapsed = SignalSeconds(DeltaSeconds);
        State.PlatePressed = ObservePlate(Rules, GroundedKg, Elapsed, ContactSeconds);
        const bool UsesAlternatePlate = Rules.DoorGate == CircuitRules::Gate::ButtonAndParallelPlates;
        if (!UsesAlternatePlate)
        {
            AlternateContactSeconds = 0.0f;
        }
        State.AlternatePlatePressed = UsesAlternatePlate &&
            ObservePlate(Rules, AlternateKg, Elapsed, AlternateContactSeconds);
        const bool Inputs[] = {State.PlatePressed, State.AlternatePlatePressed};
        State.ParallelOutput = UsesAlternatePlate && AnyPowered(Inputs);
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
        case CircuitRules::Gate::ButtonAndParallelPlates:
            State.ExitEnabled = State.ButtonLatched && State.ParallelOutput;
            break;
        }
    }

    void Reset()
    {
        State = {};
        ContactSeconds = 0.0f;
        AlternateContactSeconds = 0.0f;
    }

private:
    static bool ObservePlate(const CircuitRules& Rules, float GroundedKg, float Elapsed, float& Contact)
    {
        constexpr float SettleSeconds = 0.1f;
        constexpr float MassToleranceKg = 0.01f;
        const bool HasLoad = std::isfinite(GroundedKg) &&
            GroundedKg >= Rules.MinimumKg - MassToleranceKg && GroundedKg <= Rules.MaximumKg + MassToleranceKg;
        Contact = HasLoad ? std::min(Contact + Elapsed, SettleSeconds) : 0.0f;
        return Contact >= SettleSeconds;
    }

    CircuitState State;
    float ContactSeconds = 0.0f;
    float AlternateContactSeconds = 0.0f;
};
}
