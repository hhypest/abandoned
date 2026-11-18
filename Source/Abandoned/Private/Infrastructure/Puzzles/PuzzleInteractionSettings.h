#pragma once

#include "CoreMinimal.h"
#include "PuzzleInteractionSettings.generated.h"

UCLASS(Config=Game)
class UPuzzleInteractionSettings final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    float ThrowImpulse = 4200.0f;

    UPROPERTY(Config)
    FLinearColor PlacementGhostTint = FLinearColor(0.10, 0.82, 1.0, 0.42);

    UPROPERTY(Config)
    FLinearColor BlockedGhostTint = FLinearColor(0.90, 0.10, 0.14, 0.48);

    float GetThrowImpulse() const
    {
        return FMath::IsFinite(ThrowImpulse) && ThrowImpulse > 0.0f ? ThrowImpulse : 4200.0f;
    }
};
