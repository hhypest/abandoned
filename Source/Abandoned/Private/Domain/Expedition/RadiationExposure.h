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
    double RecoveryDosePerSecond;

    bool IsValid() const
    {
        return QuietSeconds > 0.0 && FlareSeconds > 0.0 && std::isfinite(QuietSeconds + FlareSeconds) &&
            QuietDosePerSecond >= 0.0 && FlareDosePerSecond > QuietDosePerSecond && std::isfinite(FlareDosePerSecond) &&
            RecoveryDosePerSecond > 0.0 && std::isfinite(RecoveryDosePerSecond);
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

class RadiationDose
{
public:
    static constexpr double Limit = 100.0;

    double Percent() const { return Accumulated; }
    bool IsLethal() const { return Accumulated >= Limit; }
    void Clear() { Accumulated = 0.0; }
    void Apply(double Change)
    {
        if (!std::isnan(Change))
        {
            Accumulated = std::clamp(Accumulated + Change, 0.0, Limit);
        }
    }

private:
    double Accumulated = 0.0;
};

class RadiationExposure
{
public:
    explicit RadiationExposure(FlarePattern Schedule) : Pattern(Schedule) {}

    DosimeterReading Read(RadiationPosition Position, const RadiationDose& Dose) const
    {
        const bool bFlare = PhaseSeconds >= Pattern.QuietSeconds;
        const double NextBoundary = bFlare ? Pattern.QuietSeconds + Pattern.FlareSeconds : Pattern.QuietSeconds;
        double Rate = 0.0;
        if (Position == RadiationPosition::Exposed)
        {
            Rate = bFlare ? Pattern.FlareDosePerSecond : Pattern.QuietDosePerSecond;
        }
        else if (Position == RadiationPosition::Outside && Dose.Percent() > 0.0 && !Dose.IsLethal())
        {
            Rate = -Pattern.RecoveryDosePerSecond;
        }
        return {Position, bFlare, NextBoundary - PhaseSeconds, Dose.Percent(), Rate};
    }

    bool Advance(double DeltaSeconds, RadiationPosition Position, RadiationDose& Dose)
    {
        const double End = PhaseSeconds + DeltaSeconds;
        if (Dose.IsLethal() || DeltaSeconds <= 0.0 || !std::isfinite(End) || !Pattern.IsValid())
        {
            return Dose.IsLethal();
        }
        const double Cycle = Pattern.QuietSeconds + Pattern.FlareSeconds;
        const double NextPhase = std::fmod(End, Cycle);
        if (Position == RadiationPosition::Exposed)
        {
            // Integrate both rates when an update straddles one or more phase boundaries.
            const double EndFlare = std::floor(End / Cycle) * Pattern.FlareSeconds +
                std::max(0.0, NextPhase - Pattern.QuietSeconds);
            const double StartFlare = std::max(0.0, PhaseSeconds - Pattern.QuietSeconds);
            const double FlareElapsed = std::clamp(EndFlare - StartFlare, 0.0, DeltaSeconds);
            const double Added = FlareElapsed * Pattern.FlareDosePerSecond +
                (DeltaSeconds - FlareElapsed) * Pattern.QuietDosePerSecond;
            Dose.Apply(Added);
        }
        else if (Position == RadiationPosition::Outside)
        {
            Dose.Apply(-Pattern.RecoveryDosePerSecond * DeltaSeconds);
        }
        PhaseSeconds = NextPhase;
        return Dose.IsLethal();
    }

private:
    FlarePattern Pattern;
    double PhaseSeconds = 0.0;
};
}
