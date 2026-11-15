#pragma once

#include "Domain/Expedition/FallImpact.h"

namespace Abandoned::Application::Expedition
{
enum class PassageOutcome { Continue, ReturnToCheckpoint, EnterSanctuary };

struct PassageObservation
{
    bool ReachedExit = false;
    Domain::Expedition::FallImpact Impact = Domain::Expedition::FallImpact::None;
    bool OutOfBounds = false;
    bool OxygenDepleted = false;
    bool RadiationLimitReached = false;
};

constexpr PassageOutcome ResolvePassage(PassageObservation Observation)
{
    if (Observation.Impact == Domain::Expedition::FallImpact::Fatal || Observation.OutOfBounds ||
        Observation.OxygenDepleted || Observation.RadiationLimitReached)
    {
        return PassageOutcome::ReturnToCheckpoint;
    }
    return Observation.ReachedExit ? PassageOutcome::EnterSanctuary : PassageOutcome::Continue;
}
}
