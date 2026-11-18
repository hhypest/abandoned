#pragma once

#include <cmath>
#include <optional>
#include "Application/Expedition/PassageProgress.h"
#include "Application/Expedition/SuitSession.h"

namespace Abandoned::Application::Expedition
{
// One passage visit. The suit and its archive can survive travel to another world.
class PassageHazards
{
public:
    void ResetMovement()
    {
        GroundHeight.reset();
        Impact = Domain::Expedition::FallImpact::None;
        Injury.Reset();
    }

    void ForgetSupport() { GroundHeight.reset(); }
    std::optional<double> SupportHeight() const { return GroundHeight; }
    Domain::Expedition::FallImpact LastImpact() const { return Impact; }
    const Domain::Expedition::MinorInjuryRecovery& Recovery() const { return Injury; }

    std::optional<double> Land(double SurfaceHeight)
    {
        if (!GroundHeight)
        {
            return std::nullopt;
        }
        // Measure to millimetres so contact noise at exactly 3 m cannot turn an injury into a fatal fall.
        const double DropCm = std::round((*GroundHeight - SurfaceHeight) * 10.0) / 10.0;
        GroundHeight.reset();
        Impact = Domain::Expedition::AssessFall(DropCm);
        return DropCm;
    }

    bool AdvanceMovement(std::optional<double> WalkableHeight, float Seconds, Domain::Expedition::InjuryRecoveryProfile Profile)
    {
        if (WalkableHeight)
        {
            GroundHeight = WalkableHeight;
        }
        Injury.Advance(Seconds);
        if (Impact != Domain::Expedition::FallImpact::MinorInjury)
        {
            return false;
        }
        Injury.Start(Profile);
        Impact = Domain::Expedition::FallImpact::None;
        return true;
    }

    void EnableRadiation(Domain::Expedition::FlarePattern Pattern) { Radiation.emplace(Pattern); }

    std::optional<Domain::Expedition::DosimeterReading> ReadDosimeter(const SuitSession& Suit) const
    {
        return Radiation ? std::optional{Radiation->Read(Position, Suit.RadiationDose())} : std::nullopt;
    }

    PassageObservation Observe(bool AtExit, bool OutOfBounds, Domain::Expedition::RadiationPosition CurrentPosition,
        float Seconds, SuitSession* Suit)
    {
        Position = CurrentPosition;
        const bool DoseLimit = Suit && Radiation && Suit->AdvanceExposure(*Radiation, Position, Seconds);
        const bool OxygenDepleted = Suit && Suit->SpendInPassage(Seconds);
        return {AtExit, Impact, OutOfBounds, OxygenDepleted, DoseLimit};
    }

private:
    std::optional<double> GroundHeight;
    Domain::Expedition::FallImpact Impact = Domain::Expedition::FallImpact::None;
    Domain::Expedition::MinorInjuryRecovery Injury;
    std::optional<Domain::Expedition::RadiationExposure> Radiation;
    Domain::Expedition::RadiationPosition Position = Domain::Expedition::RadiationPosition::Outside;
};
}
