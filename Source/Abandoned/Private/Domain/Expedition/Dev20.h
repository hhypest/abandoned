#pragma once

#include <optional>
#include <string_view>

namespace Abandoned::Domain::Expedition
{
inline constexpr std::string_view Dev20Id = "DEV-20";
inline constexpr float Dev20MassKg = 34.0f;

enum class Dev20Stage { Station, Button, Weight };

constexpr std::optional<Dev20Stage> NextDev20Stage(Dev20Stage From)
{
    switch (From)
    {
    case Dev20Stage::Station: return Dev20Stage::Button;
    case Dev20Stage::Button: return Dev20Stage::Weight;
    case Dev20Stage::Weight: return std::nullopt;
    }
    return std::nullopt;
}
}
