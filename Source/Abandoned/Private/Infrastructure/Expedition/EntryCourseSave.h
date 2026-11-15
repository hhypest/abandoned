#pragma once

#include "GameFramework/SaveGame.h"
#include "EntryCourseSave.generated.h"

UCLASS()
class UEntryCourseSave final : public USaveGame
{
    GENERATED_BODY()

public:
    static constexpr int32 CurrentVersion = 1;

    UPROPERTY(SaveGame)
    int32 Version = CurrentVersion;

    UPROPERTY(SaveGame)
    FString Chamber;
};
