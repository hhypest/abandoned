#include "Infrastructure/Expedition/UnrealEntryCourse.h"

#include "Kismet/GameplayStatics.h"

namespace Abandoned::Infrastructure::Expedition
{
using Domain::Expedition::Segment;

std::optional<Segment> ReadSegment(const FString& Id)
{
    const FTCHARToUTF8 Key(*Id);
    return Domain::Expedition::ParseSegment(std::string_view(Key.Get(), Key.Length()));
}

FString WriteSegment(Segment Value)
{
    return UTF8_TO_TCHAR(Domain::Expedition::SegmentKey(Value).data());
}

void TravelToSegment(const UObject* WorldContext, Segment Destination, std::optional<Segment> PassageOrigin)
{
    const bool bExpeditionMode = Destination == Segment::Passage || Destination == Segment::Vanguard;
    const FString Mode = bExpeditionMode ? TEXT("/Script/Abandoned.VanguardRouteGameMode")
                                         : TEXT("/Script/Abandoned.TrainingChamberGameMode");
    FString Options = TEXT("game=") + Mode + TEXT("?Chamber=") + WriteSegment(Destination);
    if (Destination == Segment::Balance)
    {
        Options += TEXT("?EntryCourse=1");
    }
    if (bExpeditionMode && PassageOrigin && Domain::Expedition::IsCheckpoint(*PassageOrigin))
    {
        Options += TEXT("?EntryCourse=1?Checkpoint=") + WriteSegment(*PassageOrigin);
    }
    UGameplayStatics::OpenLevel(WorldContext, FName(TEXT("/Engine/Maps/Entry")), true, Options);
}

void TravelToRadiationPassage(const UObject* WorldContext)
{
    UGameplayStatics::OpenLevel(WorldContext, FName(TEXT("/Engine/Maps/Entry")), true,
        TEXT("game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage?RadiationRun=1"));
}
}
