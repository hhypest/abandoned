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
    bool RecordResupply() const;

private:
    bool Update(std::optional<Domain::Expedition::Segment> Chamber,
        std::optional<Domain::Expedition::SuitRecord> Record) const;
};

const TCHAR* CheckpointIssueText(Application::Expedition::CheckpointIssue Issue);
std::optional<Domain::Expedition::Segment> ReadSegment(const FString& Id);
FString WriteSegment(Domain::Expedition::Segment Value);
}
