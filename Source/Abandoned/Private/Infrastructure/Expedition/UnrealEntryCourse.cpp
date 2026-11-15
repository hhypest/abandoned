#include "Infrastructure/Expedition/UnrealEntryCourse.h"

#include "Kismet/GameplayStatics.h"
#include "Infrastructure/Expedition/EntryCourseSave.h"

namespace Abandoned::Infrastructure::Expedition
{
using Domain::Expedition::Segment;

namespace
{
const FString CheckpointSlot = TEXT("AbandonedEntryCourse");
}

std::optional<Segment> ReadSegment(const FString& Id)
{
    const FTCHARToUTF8 Key(*Id);
    return Domain::Expedition::ParseSegment(std::string_view(Key.Get(), Key.Length()));
}

FString WriteSegment(Segment Value)
{
    return UTF8_TO_TCHAR(Domain::Expedition::SegmentKey(Value).data());
}

Application::Expedition::Checkpoint UnrealCheckpointStore::Read() const
{
    if (!UGameplayStatics::DoesSaveGameExist(CheckpointSlot, 0))
    {
        return {};
    }
    const UEntryCourseSave* Save = Cast<UEntryCourseSave>(UGameplayStatics::LoadGameFromSlot(CheckpointSlot, 0));
    if (!Save || Save->Version != UEntryCourseSave::CurrentVersion)
    {
        return {std::nullopt, true};
    }
    const auto Chamber = ReadSegment(Save->Chamber);
    return {Chamber, !Chamber};
}

bool UnrealCheckpointStore::Write(Segment Chamber) const
{
    UEntryCourseSave* Save = NewObject<UEntryCourseSave>();
    Save->Chamber = WriteSegment(Chamber);
    return UGameplayStatics::SaveGameToSlot(Save, CheckpointSlot, 0);
}

void TravelToSegment(const UObject* WorldContext, Segment Destination)
{
    const bool bExpeditionMode = Destination == Segment::Passage || Destination == Segment::Vanguard;
    const FString Mode = bExpeditionMode ? TEXT("/Script/Abandoned.VanguardRouteGameMode")
                                         : TEXT("/Script/Abandoned.TrainingChamberGameMode");
    const FString Options = TEXT("game=") + Mode + TEXT("?Chamber=") + WriteSegment(Destination);
    UGameplayStatics::OpenLevel(WorldContext, FName(TEXT("/Engine/Maps/Entry")), true, Options);
}

void TravelToRadiationPassage(const UObject* WorldContext)
{
    UGameplayStatics::OpenLevel(WorldContext, FName(TEXT("/Engine/Maps/Entry")), true,
        TEXT("game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage?RadiationRun=1"));
}
}
