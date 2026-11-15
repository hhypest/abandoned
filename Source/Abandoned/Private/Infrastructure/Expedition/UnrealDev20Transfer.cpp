#include "Infrastructure/Expedition/UnrealDev20Transfer.h"

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

void ReturnToDev20Station(const UObject* Context, bool bRecovering)
{
    if (auto* Session = Dev20Session(Context))
    {
        Session->Transfer.RecoverAtStation();
        UGameplayStatics::OpenLevel(Context, FName(TEXT("/Engine/Maps/Entry")), true, bRecovering
            ? TEXT("game=/Script/Abandoned.PortalLabGameMode?Dev20Run=1?Dev20Recovered=1")
            : TEXT("game=/Script/Abandoned.PortalLabGameMode?Dev20Run=1"));
    }
}

bool TravelWithDev20(const UObject* Context, Dev20Stage From, bool bHeld)
{
    auto* Session = Dev20Session(Context);
    if (!Session || !Session->Transfer.Depart(From, bHeld))
    {
        return false;
    }
    // Stage identifiers are translated at the world boundary. The payload stays in GameInstance.
    UGameplayStatics::OpenLevel(Context, FName(TEXT("/Engine/Maps/Entry")), true, From == Dev20Stage::Station
        ? TEXT("game=/Script/Abandoned.TrainingChamberGameMode?Chamber=Button?Dev20Run=1")
        : TEXT("game=/Script/Abandoned.TrainingChamberGameMode?Chamber=Weight?Dev20Run=1"));
    return true;
}
}
