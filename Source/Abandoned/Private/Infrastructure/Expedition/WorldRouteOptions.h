#pragma once

#include <array>
#include <string>

#include "Application/Expedition/WorldRoute.h"

namespace Abandoned::Infrastructure::Expedition
{
using Application::Expedition::ChamberDestination;
using Application::Expedition::Dev20Destination;
using Application::Expedition::Dev20Entry;
using Application::Expedition::LabDestination;
using Application::Expedition::RouteEntry;
using Application::Expedition::VanguardDestination;
using Application::Expedition::VanguardStop;
using Application::Expedition::WorldDestination;
using Domain::Expedition::Dev20Stage;
using Domain::Expedition::Segment;

enum class EntryWorld { Chamber, Vanguard, Movement, Optics, Gravity, Portal };
inline constexpr auto EntryMapPath = "/Engine/Maps/Entry";

inline constexpr std::string_view GameModePath(EntryWorld World)
{
    switch (World)
    {
    case EntryWorld::Chamber: return "/Script/Abandoned.TrainingChamberGameMode";
    case EntryWorld::Vanguard: return "/Script/Abandoned.VanguardRouteGameMode";
    case EntryWorld::Movement: return "/Script/Abandoned.MovementLabGameMode";
    case EntryWorld::Optics: return "/Script/Abandoned.PowerLabGameMode";
    case EntryWorld::Gravity: return "/Script/Abandoned.GravityLabGameMode";
    case EntryWorld::Portal: return "/Script/Abandoned.PortalLabGameMode";
    }
    return {};
}

inline constexpr EntryWorld WorldFor(const WorldDestination& Destination)
{
    if (std::holds_alternative<ChamberDestination>(Destination))
    {
        return EntryWorld::Chamber;
    }
    if (std::holds_alternative<VanguardDestination>(Destination))
    {
        return EntryWorld::Vanguard;
    }
    if (const auto* Dev20 = std::get_if<Dev20Destination>(&Destination))
    {
        return Dev20->Stage == Dev20Stage::Station ? EntryWorld::Portal : EntryWorld::Chamber;
    }
    switch (std::get<LabDestination>(Destination))
    {
    case LabDestination::Movement: return EntryWorld::Movement;
    case LabDestination::Optics: return EntryWorld::Optics;
    case LabDestination::Gravity: return EntryWorld::Gravity;
    case LabDestination::Portals: return EntryWorld::Portal;
    }
    return EntryWorld::Chamber;
}

inline std::string EncodeWorldOptions(const WorldDestination& Destination)
{
    using Domain::Expedition::SegmentKey;
    std::string Options("game=");
    Options += GameModePath(WorldFor(Destination));
    if (const auto* Chamber = std::get_if<ChamberDestination>(&Destination))
    {
        if (Chamber->Chamber)
        {
            Options += "?Chamber=";
            Options += SegmentKey(*Chamber->Chamber);
        }
        if (Chamber->Entry == RouteEntry::Campaign)
        {
            Options += "?EntryCourse=1";
        }
    }
    else if (const auto* Vanguard = std::get_if<VanguardDestination>(&Destination))
    {
        Options += Vanguard->Stop == VanguardStop::Station ? "?Chamber=Vanguard" : "?Chamber=Passage";
        if (Vanguard->Stop == VanguardStop::RadiationPassage)
        {
            Options += "?RadiationRun=1";
        }
        if (Vanguard->Entry == RouteEntry::Campaign)
        {
            Options += "?EntryCourse=1";
        }
        if (Vanguard->Origin)
        {
            Options += "?Checkpoint=";
            Options += SegmentKey(*Vanguard->Origin);
        }
    }
    else if (const auto* Dev20 = std::get_if<Dev20Destination>(&Destination))
    {
        if (const auto ChamberId = Application::Expedition::Dev20Chamber(Dev20->Stage))
        {
            Options += "?Chamber=";
            Options += SegmentKey(*ChamberId);
        }
        Options += "?Dev20Run=1";
        if (Dev20->Entry == Dev20Entry::Recovery)
        {
            Options += "?Dev20Recovered=1";
        }
    }
    return Options;
}

struct DecodedWorldOptions
{
    std::optional<WorldDestination> Destination;
    bool InvalidRecoveryOrigin = false;
};

namespace RouteOptions
{
constexpr char LowerAscii(char Value)
{
    return Value >= 'A' && Value <= 'Z' ? static_cast<char>(Value + ('a' - 'A')) : Value;
}

constexpr bool EqualOption(std::string_view Left, std::string_view Right)
{
    if (Left.size() != Right.size())
    {
        return false;
    }
    for (std::size_t Index = 0; Index < Left.size(); ++Index)
    {
        if (LowerAscii(Left[Index]) != LowerAscii(Right[Index]))
        {
            return false;
        }
    }
    return true;
}

constexpr std::optional<bool> Flag(std::optional<std::string_view> Value, bool AllowBare = false)
{
    if (!Value || *Value == "0")
    {
        return false;
    }
    if (*Value == "1" || (AllowBare && Value->empty()))
    {
        return true;
    }
    return std::nullopt;
}
}

inline DecodedWorldOptions DecodeWorldOptions(std::string_view Options, EntryWorld World)
{
    using Domain::Expedition::IsCheckpoint;
    using Domain::Expedition::ParseSegment;
    using RouteOptions::Flag;
    enum Field { Game, Chamber, Origin, Campaign, Radiation, Dev20, Recovered, Count };
    constexpr std::array<std::string_view, Count> Keys{
        "game", "chamber", "checkpoint", "entrycourse", "radiationrun", "dev20run", "dev20recovered"};
    std::array<std::optional<std::string_view>, Count> Fields{};
    while (!Options.empty())
    {
        const auto Next = Options.find('?');
        const auto Option = Options.substr(0, Next);
        const auto Equals = Option.find('=');
        const auto Key = Option.substr(0, Equals);
        for (std::size_t Index = 0; Index < Keys.size(); ++Index)
        {
            if (RouteOptions::EqualOption(Key, Keys[Index]))
            {
                if (Fields[Index])
                {
                    return {};
                }
                Fields[Index] = Equals == std::string_view::npos ? std::string_view{} : Option.substr(Equals + 1);
                break;
            }
        }
        Options.remove_prefix(Next == std::string_view::npos ? Options.size() : Next + 1);
    }
    const auto ChamberId = Fields[Chamber] ? ParseSegment(*Fields[Chamber]) : std::nullopt;
    const auto IsCampaign = Flag(Fields[Campaign]);
    const auto IsRadiation = Flag(Fields[Radiation]);
    const auto IsDev20 = Flag(Fields[Dev20], true);
    const auto IsRecovered = Flag(Fields[Recovered], true);
    const auto ClassPath = GameModePath(World);
    const auto ClassName = ClassPath.substr(ClassPath.find_last_of('.') + 1);
    if ((Fields[Game] && !RouteOptions::EqualOption(*Fields[Game], ClassPath) &&
                         !RouteOptions::EqualOption(*Fields[Game], ClassName)) ||
        (Fields[Chamber] && !ChamberId) || !IsCampaign || !IsRadiation || !IsDev20 || !IsRecovered ||
        (*IsRecovered && !*IsDev20))
    {
        return {};
    }
    const RouteEntry Entry = *IsCampaign ? RouteEntry::Campaign : RouteEntry::Direct;
    if (World == EntryWorld::Vanguard)
    {
        if (*IsDev20 || (ChamberId && ChamberId != Segment::Passage && ChamberId != Segment::Vanguard) ||
            (*IsRadiation && (ChamberId == Segment::Vanguard || *IsCampaign || Fields[Origin])))
        {
            return {};
        }
        if (*IsRadiation)
        {
            return {VanguardDestination{VanguardStop::RadiationPassage}};
        }
        const auto OriginId = Fields[Origin] ? ParseSegment(*Fields[Origin]) : std::nullopt;
        const bool ValidOrigin = OriginId && IsCheckpoint(*OriginId);
        const auto Stop = ChamberId == Segment::Vanguard ? VanguardStop::Station : VanguardStop::Passage;
        return {VanguardDestination{Stop, Entry, ValidOrigin ? OriginId : std::nullopt}, Fields[Origin] && !ValidOrigin};
    }
    if (*IsRadiation || Fields[Origin])
    {
        return {};
    }
    if (World == EntryWorld::Chamber)
    {
        if (*IsDev20)
        {
            if (*IsCampaign || *IsRecovered || (ChamberId != Segment::Button && ChamberId != Segment::Weight))
            {
                return {};
            }
            return {Dev20Destination{ChamberId == Segment::Button ? Dev20Stage::Button : Dev20Stage::Weight}};
        }
        if (ChamberId && !IsCheckpoint(*ChamberId) && ChamberId != Segment::Sandbox)
        {
            return {};
        }
        return {ChamberDestination{ChamberId, Entry}};
    }
    if (ChamberId || *IsCampaign)
    {
        return {};
    }
    if (World == EntryWorld::Portal && *IsDev20)
    {
        return {Dev20Destination{Dev20Stage::Station, *IsRecovered ? Dev20Entry::Recovery : Dev20Entry::Standard}};
    }
    if (*IsDev20)
    {
        return {};
    }
    switch (World)
    {
    case EntryWorld::Movement: return {LabDestination::Movement};
    case EntryWorld::Optics: return {LabDestination::Optics};
    case EntryWorld::Gravity: return {LabDestination::Gravity};
    case EntryWorld::Portal: return {LabDestination::Portals};
    default: return {};
    }
}
}
