#pragma once

#include <algorithm>
#include <cmath>

namespace Abandoned::Domain::Expedition
{
enum class RadiationPosition { Outside, Exposed, Sheltered };

struct FlarePattern
{
    double QuietSeconds;
    double FlareSeconds;
    double QuietDosePerSecond;
    double FlareDosePerSecond;

    bool IsValid() const
    {
        return QuietSeconds > 0.0 && FlareSeconds > 0.0 && std::isfinite(QuietSeconds + FlareSeconds) &&
            QuietDosePerSecond >= 0.0 && FlareDosePerSecond > QuietDosePerSecond && std::isfinite(FlareDosePerSecond);
    }
};

struct DosimeterReading
{
    RadiationPosition Position;
    bool FlareActive;
    double SecondsToChange;
    double DosePercent;
    double DosePerSecond;
};

class RadiationExposure
{
public:
    static constexpr double DoseLimit = 100.0;

    explicit RadiationExposure(FlarePattern Schedule) : Pattern(Schedule) {}

    DosimeterReading Read(RadiationPosition Position) const
    {
        const bool bFlare = PhaseSeconds >= Pattern.QuietSeconds;
        const double NextBoundary = bFlare ? Pattern.QuietSeconds + Pattern.FlareSeconds : Pattern.QuietSeconds;
        const double Rate = Position == RadiationPosition::Exposed
            ? (bFlare ? Pattern.FlareDosePerSecond : Pattern.QuietDosePerSecond) : 0.0;
        return {Position, bFlare, NextBoundary - PhaseSeconds, DosePercent, Rate};
    }

    bool Advance(double DeltaSeconds, RadiationPosition Position)
    {
        const double End = PhaseSeconds + DeltaSeconds;
        if (DeltaSeconds <= 0.0 || !std::isfinite(End) || !Pattern.IsValid())
        {
            return DosePercent >= DoseLimit;
        }
        const double Cycle = Pattern.QuietSeconds + Pattern.FlareSeconds;
        const double NextPhase = std::fmod(End, Cycle);
        if (Position == RadiationPosition::Exposed && DosePercent < DoseLimit)
        {
            // Integrate both rates when an update straddles one or more phase boundaries.
            const double EndFlare = std::floor(End / Cycle) * Pattern.FlareSeconds +
                std::max(0.0, NextPhase - Pattern.QuietSeconds);
            const double StartFlare = std::max(0.0, PhaseSeconds - Pattern.QuietSeconds);
            const double FlareElapsed = std::clamp(EndFlare - StartFlare, 0.0, DeltaSeconds);
            const double Added = FlareElapsed * Pattern.FlareDosePerSecond +
                (DeltaSeconds - FlareElapsed) * Pattern.QuietDosePerSecond;
            DosePercent = std::min(DoseLimit, DosePercent + Added);
        }
        PhaseSeconds = NextPhase;
        return DosePercent >= DoseLimit;
    }

private:
    FlarePattern Pattern;
    double PhaseSeconds = 0.0;
    double DosePercent = 0.0;
};
}
