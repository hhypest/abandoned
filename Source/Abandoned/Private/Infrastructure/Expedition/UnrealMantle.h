#pragma once

#include <optional>

#include "CoreMinimal.h"

class ACharacter;
class UBoxComponent;
class UPrimitiveComponent;

namespace Abandoned::Infrastructure::Expedition
{
struct MantlePath
{
    FVector Start;
    FVector Raised;
    FVector Landing;
    FVector Edge;
    FVector IntoLedge;
    TWeakObjectPtr<const UPrimitiveComponent> Ledge;
};

struct MantleHandhold
{
    FVector Edge[2];
    FQuat Orientation;
};

void MarkMantleLedge(UBoxComponent& Ledge);
std::optional<MantlePath> FindMantlePath(const ACharacter& Player, double SupportHeight);
std::optional<MantleHandhold> FindMantleHandhold(const ACharacter& Player, const MantlePath& Path);
}
