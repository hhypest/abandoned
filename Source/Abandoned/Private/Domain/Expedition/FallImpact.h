#pragma once

#include <algorithm>
#include <cmath>

namespace Abandoned::Domain::Expedition
{
enum class FallImpact { None, MinorInjury, Fatal };

inline FallImpact AssessFall(double DropCm)
{
    constexpr double InjuryThresholdCm = 300.0;
    if (!std::isfinite(DropCm) || DropCm < InjuryThresholdCm)
    {
        return FallImpact::None;
    }
    return DropCm > InjuryThresholdCm ? FallImpact::Fatal : FallImpact::MinorInjury;
}

struct InjuryRecoveryProfile
{
    double DurationSeconds = 0.0;
    float InitialSpeedScale = 1.0f;

    bool IsValid() const
    {
        return std::isfinite(DurationSeconds) && DurationSeconds > 0.0 &&
            InitialSpeedScale > 0.0f && InitialSpeedScale < 1.0f;
    }
};

class MinorInjuryRecovery
{
public:
    void Start(InjuryRecoveryProfile Profile)
    {
        if (Profile.IsValid())
        {
            Settings = Profile;
            RemainingSeconds = Profile.DurationSeconds;
        }
    }

    void Advance(double DeltaSeconds)
    {
        if (std::isfinite(DeltaSeconds) && DeltaSeconds > 0.0)
        {
            RemainingSeconds = std::max(0.0, RemainingSeconds - DeltaSeconds);
        }
    }

    void Reset() { RemainingSeconds = 0.0; }
    bool IsActive() const { return RemainingSeconds > 0.0; }
    float GroundSpeedScale() const
    {
        if (!IsActive())
        {
            return 1.0f;
        }
        const double Fraction = RemainingSeconds / Settings.DurationSeconds;
        return static_cast<float>(1.0 - (1.0 - Settings.InitialSpeedScale) * Fraction);
    }

private:
    InjuryRecoveryProfile Settings;
    double RemainingSeconds = 0.0;
};
}
