#pragma once

#include "Application/Expedition/Dev20Transfer.h"
#include "Application/Expedition/WorldRoute.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UnrealDev20Transfer.generated.h"

UCLASS()
class UDev20TransferSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    Abandoned::Application::Expedition::Dev20Transfer Transfer;
};

namespace Abandoned::Infrastructure::Expedition
{
UDev20TransferSession* Dev20Session(const UObject* Context);
void ReturnToDev20Station(const UObject* Context,
    Application::Expedition::Dev20Entry Entry = Application::Expedition::Dev20Entry::Standard);
bool TravelWithDev20(const UObject* Context, Domain::Expedition::Dev20Stage From, bool bHeld);
}
