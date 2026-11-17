#pragma once

#include "Domain/Expedition/RadiationExposure.h"
#include "Domain/Expedition/SuitArchive.h"
#include "Domain/Expedition/SuitSupply.h"

namespace Abandoned::Application::Expedition
{
class SuitSession
{
public:
    void EnterPassage(bool HasForecast)
    {
        Supply.Refill();
        Dose.Clear();
        FlashlightOn = false;
        if (HasForecast)
        {
            Archive.Reveal(Domain::Expedition::SuitRecord::ForecastSynchronized);
        }
    }

    double EnterStation()
    {
        const double Residual = Dose.Percent();
        if (Residual > 0.0)
        {
            Dose.Clear();
            Archive.Reveal(Domain::Expedition::SuitRecord::Decontaminated);
        }
        return Residual;
    }

    void Resupply()
    {
        Supply.Refill();
        Archive.Reveal(Domain::Expedition::SuitRecord::OxygenRestored);
    }

    float OxygenPercent() const { return Supply.Percent(); }
    const Domain::Expedition::RadiationDose& RadiationDose() const { return Dose; }
    const Domain::Expedition::SuitArchive& Records() const { return Archive; }
    bool IsFlashlightOn() const { return FlashlightOn; }
    void SetFlashlight(bool On) { FlashlightOn = On; }
    bool SpendInPassage(float DeltaSeconds) { return Supply.SpendInPassage(DeltaSeconds); }

    bool AdvanceExposure(Domain::Expedition::RadiationExposure& Exposure,
        Domain::Expedition::RadiationPosition Position, double DeltaSeconds)
    {
        return Exposure.Advance(DeltaSeconds, Position, Dose);
    }

private:
    Domain::Expedition::SuitSupply Supply;
    Domain::Expedition::RadiationDose Dose;
    Domain::Expedition::SuitArchive Archive;
    bool FlashlightOn = false;
};
}
