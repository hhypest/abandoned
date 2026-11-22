#pragma once

#include "Application/Expedition/SuitSession.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VanguardSession.generated.h"

UCLASS()
class UVanguardSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    static UVanguardSession* Find(const UObject& Context);
    Abandoned::Application::Expedition::SuitSession Suit;
};
