#pragma once

#include "Application/Expedition/WorldRoute.h"
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

inline Checkpoint ReadCourseCheckpoint(const CheckpointStore* Store)
{
    Checkpoint Saved = Store ? Store->Read() : Checkpoint{};
    if (Saved.Issue == CheckpointIssue::None && Saved.Chamber && !Domain::Expedition::IsCheckpoint(*Saved.Chamber))
    {
        Saved.Issue = CheckpointIssue::UnknownCheckpoint;
    }
    if (Saved.Issue != CheckpointIssue::None)
    {
        Saved.Chamber.reset();
    }
    return Saved;
}

inline CourseStart StartCourse(ChamberDestination Destination, const CheckpointStore* Store)
{
    if (Destination.Chamber)
    {
        return {*Destination.Chamber, CheckpointIssue::None,
            *Destination.Chamber == Segment::Balance && Destination.Entry == RouteEntry::Direct};
    }
    const Checkpoint Saved = ReadCourseCheckpoint(Store);
    return {Saved.Chamber.value_or(Segment::Stairs), Saved.Issue};
}

inline bool RecordArrival(CourseStart State, const CheckpointStore& Store)
{
    return State.Issue != CheckpointIssue::None || State.StandaloneBalance ||
        !Domain::Expedition::IsCheckpoint(State.Current) || Store.Write(State.Current);
}

inline std::optional<WorldDestination> NextCourseDestination(CourseStart State)
{
    const auto Next = State.StandaloneBalance ? std::nullopt : Domain::Expedition::NextSegment(State.Current);
    if (!Next)
    {
        return std::nullopt;
    }
    if (*Next == Segment::Passage)
    {
        return VanguardDestination{VanguardStop::Passage, RouteEntry::Campaign, State.Current};
    }
    return ChamberDestination{*Next, RouteEntry::Campaign};
}

inline WorldDestination PassageRecovery(VanguardDestination Passage, const CheckpointStore* Store)
{
    if (Passage.Stop == VanguardStop::RadiationPassage)
    {
        return VanguardDestination{VanguardStop::RadiationPassage};
    }
    if (Passage.Origin && Domain::Expedition::IsCheckpoint(*Passage.Origin))
    {
        return ChamberDestination{*Passage.Origin, RouteEntry::Campaign};
    }
    return ChamberDestination{ReadCourseCheckpoint(Store).Chamber.value_or(Segment::Weight), RouteEntry::Campaign};
}

inline VanguardDestination StationAfterPassage(VanguardDestination Passage, const WorldDestination& Recovery)
{
    if (Passage.Stop == VanguardStop::Passage && Passage.Entry == RouteEntry::Campaign)
    {
        if (const auto* Chamber = std::get_if<ChamberDestination>(&Recovery))
        {
            return {VanguardStop::Station, RouteEntry::Campaign, Chamber->Chamber};
        }
    }
    return {VanguardStop::Station};
}
}
