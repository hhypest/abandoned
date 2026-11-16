#pragma once

#include <optional>

#include "CoreMinimal.h"

class ACharacter;
class UBoxComponent;

namespace Abandoned::Infrastructure::Expedition
{
struct MantlePath
{
    FVector Start;
    FVector Raised;
    FVector Landing;
};

void MarkMantleLedge(UBoxComponent& Ledge);
std::optional<MantlePath> FindMantlePath(const ACharacter& Player, double SupportHeight);
}
