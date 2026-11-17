#pragma once

#include "CoreMinimal.h"
#include "Infrastructure/Expedition/WorldRouteOptions.h"

namespace Abandoned::Infrastructure::Expedition
{
std::optional<WorldDestination> ReadWorldDestination(const FString& Options, EntryWorld World, FString& Error);
void TravelToWorld(const UObject* Context, const WorldDestination& Destination);
}
