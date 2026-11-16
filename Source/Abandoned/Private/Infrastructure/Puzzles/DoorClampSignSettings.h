#pragma once

#include "CoreMinimal.h"
#include "DoorClampSignSettings.generated.h"

UCLASS(Config=Game, DefaultConfig)
class UDoorClampSignSettings final : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    float TextSizeCm = 16.0f;

    UPROPERTY(Config)
    FColor TextColor = FColor::White;
};
