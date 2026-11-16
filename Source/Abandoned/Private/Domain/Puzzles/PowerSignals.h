#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace Abandoned::Domain::Puzzles
{
constexpr bool AllPowered(std::span<const bool> Inputs)
{
    return !Inputs.empty() && std::ranges::all_of(Inputs, [](bool Value) { return Value; });
}

constexpr bool AnyPowered(std::span<const bool> Inputs)
{
    return std::ranges::any_of(Inputs, [](bool Value) { return Value; });
}

constexpr bool InvertPower(bool Input) { return !Input; }

inline float SignalSeconds(float Seconds)
{
    return std::isfinite(Seconds) ? std::max(Seconds, 0.0f) : 0.0f;
}

enum class SignalMode { Continuous, Latch, Pulse, Toggle };

class SignalOutput
{
public:
    explicit SignalOutput(SignalMode Mode, float PulseSeconds = 0.0f)
        : Mode(Mode), PulseSeconds(SignalSeconds(PulseSeconds)) {}

    bool Observe(bool Input, float DeltaSeconds)
    {
        const bool Rising = Input && !PreviousInput;
        PreviousInput = Input;
        switch (Mode)
        {
        case SignalMode::Continuous:
            Powered = Input;
            break;
        case SignalMode::Latch:
            Powered = Powered || Rising;
            break;
        case SignalMode::Toggle:
            Powered = Powered != Rising;
            break;
        case SignalMode::Pulse:
            Remaining = Rising ? PulseSeconds : std::max(0.0f, Remaining - SignalSeconds(DeltaSeconds));
            Powered = Remaining > 0.0f;
            break;
        }
        return Powered;
    }

    void Reset() { Powered = PreviousInput = false; Remaining = 0.0f; }
    bool IsPowered() const { return Powered; }
    float RemainingSeconds() const { return Remaining; }

private:
    SignalMode Mode;
    float PulseSeconds;
    float Remaining = 0.0f;
    bool Powered = false;
    bool PreviousInput = false;
};

class DelayOnPower
{
public:
    explicit DelayOnPower(float Seconds) : Delay(SignalSeconds(Seconds)) {}

    bool Observe(bool Input, float DeltaSeconds)
    {
        Elapsed = Input ? std::min(Elapsed + SignalSeconds(DeltaSeconds), Delay) : 0.0f;
        return Input && Elapsed >= Delay;
    }

    void Reset() { Elapsed = 0.0f; }

private:
    float Delay;
    float Elapsed = 0.0f;
};

class PowerBudget
{
public:
    explicit constexpr PowerBudget(unsigned Units) : Available(Units) {}

    // Allocate in the caller's deterministic priority order.
    constexpr bool PowerBranch(bool Requested, unsigned Cost)
    {
        if (!Requested || Cost > Available)
        {
            return false;
        }
        Available -= Cost;
        return true;
    }

private:
    unsigned Available;
};
}
