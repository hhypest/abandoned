#pragma once

#include <variant>

#include "Domain/Expedition/Dev20.h"
#include "Domain/Expedition/EntryRoute.h"

namespace Abandoned::Application::Expedition
{
enum class RouteEntry { Direct, Campaign };
enum class VanguardStop { Passage, Station, RadiationPassage };
enum class LabDestination { Movement, Optics, Gravity, Portals };
enum class Dev20Entry { Standard, Recovery };

struct ChamberDestination
{
    std::optional<Domain::Expedition::Segment> Chamber;
    RouteEntry Entry = RouteEntry::Direct;
    bool operator==(const ChamberDestination&) const = default;
};

struct VanguardDestination
{
    VanguardStop Stop = VanguardStop::Passage;
    RouteEntry Entry = RouteEntry::Direct;
    std::optional<Domain::Expedition::Segment> Origin;
    bool operator==(const VanguardDestination&) const = default;
};

struct Dev20Destination
{
    Domain::Expedition::Dev20Stage Stage;
    Dev20Entry Entry = Dev20Entry::Standard;
    bool operator==(const Dev20Destination&) const = default;
};

using WorldDestination = std::variant<ChamberDestination, VanguardDestination, Dev20Destination, LabDestination>;

constexpr std::optional<Domain::Expedition::Segment> Dev20Chamber(Domain::Expedition::Dev20Stage Stage)
{
    using Domain::Expedition::Dev20Stage;
    using Domain::Expedition::Segment;
    switch (Stage)
    {
    case Dev20Stage::Button: return Segment::Button;
    case Dev20Stage::Weight: return Segment::Weight;
    default: return std::nullopt;
    }
}
}
