#pragma once

#include <optional>
#include <string_view>

namespace Abandoned::Domain::Expedition
{
enum class Segment { Stairs, Button, Weight, Passage, Vanguard, Sandbox };

constexpr std::string_view SegmentKey(Segment Value)
{
    switch (Value)
    {
    case Segment::Stairs: return "Stairs";
    case Segment::Button: return "Button";
    case Segment::Weight: return "Weight";
    case Segment::Passage: return "Passage";
    case Segment::Vanguard: return "Vanguard";
    case Segment::Sandbox: return "Sandbox";
    }
    return {};
}

constexpr std::optional<Segment> ParseSegment(std::string_view Key)
{
    constexpr Segment Route[] = {Segment::Stairs, Segment::Button, Segment::Weight,
        Segment::Passage, Segment::Vanguard, Segment::Sandbox};
    for (Segment Value : Route)
    {
        if (Key == SegmentKey(Value))
        {
            return Value;
        }
    }
    return std::nullopt;
}

constexpr bool IsCheckpoint(Segment Value)
{
    return Value == Segment::Stairs || Value == Segment::Button || Value == Segment::Weight;
}

constexpr std::optional<Segment> NextSegment(Segment Value)
{
    switch (Value)
    {
    case Segment::Stairs: return Segment::Button;
    case Segment::Button: return Segment::Weight;
    case Segment::Weight: return Segment::Passage;
    case Segment::Passage: return Segment::Vanguard;
    default: return std::nullopt;
    }
}
}
