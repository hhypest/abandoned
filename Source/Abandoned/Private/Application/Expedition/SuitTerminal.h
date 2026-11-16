#pragma once

#include <optional>

#include "Domain/Expedition/RadiationExposure.h"
#include "Domain/Expedition/SuitArchive.h"

namespace Abandoned::Application::Expedition
{
enum class SuitSignal { Unavailable, ExteriorForecast, VanguardStation };
enum class SuitPage { Status, Signal, Archive };

struct SuitTelemetry
{
    float OxygenPercent;
    double DosePercent;
    std::optional<Domain::Expedition::DosimeterReading> Forecast;
    bool AtStation;

    SuitSignal Signal() const
    {
        if (Forecast)
        {
            return SuitSignal::ExteriorForecast;
        }
        return AtStation ? SuitSignal::VanguardStation : SuitSignal::Unavailable;
    }
};

class SuitTerminal
{
public:
    std::optional<SuitPage> Page() const { return Current; }

    void Toggle()
    {
        if (Current)
        {
            Close();
            return;
        }
        Current = SuitPage::Status;
        RecordIndex = 0;
    }

    void Close() { Current.reset(); }

    void NextPage()
    {
        if (Current)
        {
            switch (*Current)
            {
            case SuitPage::Status: Current = SuitPage::Signal; break;
            case SuitPage::Signal: Current = SuitPage::Archive; break;
            case SuitPage::Archive: Current = SuitPage::Status; break;
            }
        }
    }

    std::size_t ArchiveIndex() const { return RecordIndex; }

    void MoveArchive(bool Forward, const Domain::Expedition::SuitArchive& Archive)
    {
        if (Current != SuitPage::Archive)
        {
            return;
        }
        if (Forward && RecordIndex + 1 < Archive.Size())
        {
            ++RecordIndex;
        }
        else if (!Forward && RecordIndex > 0)
        {
            --RecordIndex;
        }
    }

private:
    std::optional<SuitPage> Current;
    std::size_t RecordIndex = 0;
};
}
