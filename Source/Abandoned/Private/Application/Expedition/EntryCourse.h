#pragma once

#include "Domain/Expedition/EntryRoute.h"
#include "Domain/Expedition/SuitArchive.h"

namespace Abandoned::Application::Expedition
{
using Domain::Expedition::Segment;

enum class CheckpointIssue { None, Unreadable, UnsupportedVersion, UnknownCheckpoint, InvalidArchive };

struct Checkpoint
{
    std::optional<Segment> Chamber;
    CheckpointIssue Issue = CheckpointIssue::None;
    Domain::Expedition::SuitArchive Archive;
};

class CheckpointStore
{
public:
    virtual ~CheckpointStore() = default;
    virtual Checkpoint Read() const = 0;
    virtual bool Write(Segment Chamber) const = 0;
};

struct CourseStart
{
    Segment Current = Segment::Stairs;
    CheckpointIssue Issue = CheckpointIssue::None;
    bool StandaloneBalance = false;
};

inline CourseStart StartCourse(std::optional<Segment> Requested, const CheckpointStore* Store, bool CampaignTravel = false)
{
    if (Requested)
    {
        return {*Requested, CheckpointIssue::None, *Requested == Segment::Balance && !CampaignTravel};
    }
    if (Store)
    {
        const Checkpoint Existing = Store->Read();
        if (Existing.Issue != CheckpointIssue::None)
        {
            return {Segment::Stairs, Existing.Issue};
        }
        if (Existing.Chamber && !Domain::Expedition::IsCheckpoint(*Existing.Chamber))
        {
            return {Segment::Stairs, CheckpointIssue::UnknownCheckpoint};
        }
        if (Existing.Chamber)
        {
            return {*Existing.Chamber};
        }
    }
    return {};
}

inline bool RecordArrival(CourseStart State, const CheckpointStore& Store)
{
    return State.Issue != CheckpointIssue::None || State.StandaloneBalance ||
        !Domain::Expedition::IsCheckpoint(State.Current) || Store.Write(State.Current);
}

inline std::optional<Segment> NextCourseSegment(CourseStart State)
{
    return State.StandaloneBalance ? std::nullopt : Domain::Expedition::NextSegment(State.Current);
}

inline Segment PassageCheckpoint(std::optional<Segment> Origin, const CheckpointStore* Store)
{
    if (Origin && Domain::Expedition::IsCheckpoint(*Origin))
    {
        return *Origin;
    }
    const Checkpoint Existing = Store ? Store->Read() : Checkpoint{};
    if (Existing.Issue == CheckpointIssue::None && Existing.Chamber && Domain::Expedition::IsCheckpoint(*Existing.Chamber))
    {
        return *Existing.Chamber;
    }
    return Segment::Weight;
}
}
