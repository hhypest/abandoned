#pragma once

#include "Domain/Expedition/EntryRoute.h"

namespace Abandoned::Application::Expedition
{
using Domain::Expedition::Segment;

struct Checkpoint
{
    std::optional<Segment> Chamber;
    bool Incompatible = false;
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
    bool CheckpointFallback = false;
    bool StandaloneBalance = false;
};

inline CourseStart StartCourse(std::optional<Segment> Requested, const CheckpointStore* Store, bool CampaignTravel = false)
{
    if (Requested)
    {
        return {*Requested, false, *Requested == Segment::Balance && !CampaignTravel};
    }
    if (Store)
    {
        const Checkpoint Existing = Store->Read();
        if (Existing.Incompatible || (Existing.Chamber && !Domain::Expedition::IsCheckpoint(*Existing.Chamber)))
        {
            return {Segment::Stairs, true};
        }
        if (Existing.Chamber)
        {
            return {*Existing.Chamber, false};
        }
    }
    return {};
}

inline bool RecordArrival(CourseStart State, const CheckpointStore& Store)
{
    return State.CheckpointFallback || State.StandaloneBalance ||
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
    if (!Existing.Incompatible && Existing.Chamber && Domain::Expedition::IsCheckpoint(*Existing.Chamber))
    {
        return *Existing.Chamber;
    }
    return Segment::Weight;
}
}
