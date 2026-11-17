#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"

#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

namespace Abandoned::Infrastructure::Expedition
{
using Domain::Expedition::Dev20Stage;

UDev20TransferSession* Dev20Session(const UObject* Context)
{
    UGameInstance* Game = UGameplayStatics::GetGameInstance(Context);
    return Game ? Game->GetSubsystem<UDev20TransferSession>() : nullptr;
}

void ReturnToDev20Station(const UObject* Context, Dev20Entry Entry)
{
    if (auto* Session = Dev20Session(Context))
    {
        Session->Transfer.RecoverAtStation();
        TravelToWorld(Context, Dev20Destination{Dev20Stage::Station, Entry});
    }
}

bool TravelWithDev20(const UObject* Context, Dev20Stage From, bool bHeld)
{
    auto* Session = Dev20Session(Context);
    const auto Next = Session ? Session->Transfer.Depart(From, bHeld) : std::nullopt;
    if (!Next)
    {
        return false;
    }
    TravelToWorld(Context, Dev20Destination{*Next});
    return true;
}
}
