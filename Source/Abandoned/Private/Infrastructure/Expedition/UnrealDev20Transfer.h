#pragma once

#include "Application/Expedition/Dev20Transfer.h"
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
void ReturnToDev20Station(const UObject* Context, bool bRecovering = false);
bool TravelWithDev20(const UObject* Context, Domain::Expedition::Dev20Stage From, bool bHeld);
}
