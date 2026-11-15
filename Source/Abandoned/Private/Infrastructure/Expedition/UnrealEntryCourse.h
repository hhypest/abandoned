#pragma once

#include "CoreMinimal.h"
#include "Application/Expedition/EntryCourse.h"

namespace Abandoned::Infrastructure::Expedition
{
class UnrealCheckpointStore final : public Application::Expedition::CheckpointStore
{
public:
    Application::Expedition::Checkpoint Read() const override;
    bool Write(Domain::Expedition::Segment Chamber) const override;
};

std::optional<Domain::Expedition::Segment> ReadSegment(const FString& Id);
FString WriteSegment(Domain::Expedition::Segment Value);
void TravelToSegment(const UObject* WorldContext, Domain::Expedition::Segment Destination);
}
