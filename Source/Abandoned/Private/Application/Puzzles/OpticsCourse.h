#pragma once

#include "Domain/Puzzles/PowerSignals.h"

namespace Abandoned::Application::Puzzles
{
struct OpticsPower
{
    bool DirectEmitter;
    bool ReflectionEmitter;
};

struct OpticsDoors
{
    bool Interruption;
    bool Reflection;
};

class OpticsCourse
{
public:
    void PressSupplyButton()
    {
        SupplySwitch.Observe(true, 0.0f);
        SupplySwitch.Observe(false, 0.0f);
    }

    OpticsPower GetPower() const
    {
        Domain::Puzzles::PowerBudget Budget{4};
        return {
            Budget.PowerBranch(SupplySwitch.IsPowered(), 1),
            Budget.PowerBranch(InterruptionDoor.IsPowered(), 3)
        };
    }

    OpticsDoors ObserveReceivers(bool DirectSignal, bool ReflectedSignal, float DeltaSeconds)
    {
        using namespace Domain::Puzzles;
        const OpticsPower Power = GetPower();
        const bool Interrupted[] = {Power.DirectEmitter, InvertPower(DirectSignal)};
        const bool Reflection[] = {Power.ReflectionEmitter, ReflectedSignal};
        return {
            InterruptionDoor.Observe(InterruptionDelay.Observe(AllPowered(Interrupted), DeltaSeconds), DeltaSeconds),
            ReflectionDoor.Observe(ReflectionDelay.Observe(AllPowered(Reflection), DeltaSeconds), DeltaSeconds)
        };
    }

    void Reset()
    {
        SupplySwitch.Reset();
        InterruptionDoor.Reset();
        ReflectionDoor.Reset();
        InterruptionDelay.Reset();
        ReflectionDelay.Reset();
    }

private:
    Domain::Puzzles::SignalOutput SupplySwitch{Domain::Puzzles::SignalMode::Latch};
    Domain::Puzzles::SignalOutput InterruptionDoor{Domain::Puzzles::SignalMode::Latch};
    Domain::Puzzles::SignalOutput ReflectionDoor{Domain::Puzzles::SignalMode::Continuous};
    Domain::Puzzles::DelayOnPower InterruptionDelay{0.30f};
    Domain::Puzzles::DelayOnPower ReflectionDelay{0.20f};
};
}
