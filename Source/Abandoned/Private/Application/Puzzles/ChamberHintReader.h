#pragma once

#include "Domain/Puzzles/ChamberHints.h"

#include <algorithm>

namespace Abandoned::Application::Puzzles
{
class ChamberHintReader
{
public:
    void Observe(Domain::Puzzles::HintNodes CorrectNodes, bool ChamberComplete, double DeltaSeconds, double DelaySeconds)
    {
        Hints.Observe(CorrectNodes, ChamberComplete, DeltaSeconds, DelaySeconds);
    }

    int Page() const { return OpenPage; }
    int AvailablePages() const { return Hints.AvailablePages(); }
    bool HasNewEntry() const { return AvailablePages() > HighestReadPage; }

    void Activate()
    {
        OpenPage = OpenPage < AvailablePages() ? OpenPage + 1 : 0;
        HighestReadPage = std::max(HighestReadPage, OpenPage);
    }

    void Close() { OpenPage = 0; }

private:
    Domain::Puzzles::ChamberHints Hints;
    int OpenPage = 0;
    int HighestReadPage = 1;
};
}
