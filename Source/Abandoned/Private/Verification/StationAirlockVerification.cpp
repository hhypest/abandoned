#include "CoreMinimal.h"
#include "Verification/StationAirlockVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardSession.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogStationAirlockTest, Log, All);

namespace Abandoned::Verification::StationAirlock
{
namespace
{
using Domain::Expedition::SuitRecord;
TWeakObjectPtr<UWorld> PassageWorld;
TWeakObjectPtr<ULevel> PassageLevel;
TWeakObjectPtr<AController> Controller;
float LastOxygen = 0;

bool ExpectedArchive(const Domain::Expedition::SuitArchive& Archive, bool bResupplied)
{
    return Archive.Size() == (bResupplied ? 2 : 1) && Archive.At(0) == SuitRecord::ForecastSynchronized &&
        (!bResupplied || Archive.At(1) == SuitRecord::OxygenRestored);
}

bool CheckSlot(bool bResupplied)
{
    const auto Slot = Infrastructure::Expedition::UnrealCheckpointStore{}.Read();
    return Slot.Chamber == Domain::Expedition::Segment::Balance && ExpectedArchive(Slot.Archive, bResupplied);
}
}

bool Requested() { return FParse::Param(FCommandLine::Get(), TEXT("VerifyStationAirlock")); }
bool OriginalSlot() { return CheckSlot(false); }

void ObservePassage(const AVanguardSegment& Passage, const AVanguardCharacter& Player)
{
    PassageWorld = Passage.GetWorld();
    PassageLevel = Passage.GetLevel();
    Controller = Player.GetController();
    LastOxygen = Passage.GetRouteState().GetOxygenPercent();
}

bool CheckArrival(const AVanguardSegment& Station, const AVanguardCharacter& Player)
{
    const auto* Session = UVanguardSession::Find(Player);
    const auto* Floor = Player.GetMovementBase();
    const float Oxygen = Station.GetRouteState().GetOxygenPercent();
    const bool bPassed = PassageWorld.Get() == Station.GetWorld() && Controller.IsValid() && Player.GetController() == Controller.Get() &&
        !Station.GetWorld()->GetLevels().Contains(PassageLevel.Get()) && Station.GetLevel() != Station.GetWorld()->PersistentLevel &&
        Player.GetLevel() == Station.GetLevel() && Floor && Floor->GetOwner()->GetLevel() == Station.GetLevel() &&
        Station.GetRouteState().AtStation() && LastOxygen < 100.0f && FMath::IsNearlyEqual(Oxygen, LastOxygen, 0.02f) &&
        Player.IsFlashlightOn() && Session && ExpectedArchive(Session->Suit.Records(), false) && OriginalSlot();
    if (bPassed)
    {
        UE_LOG(LogStationAirlockTest, Display, TEXT("Station handoff verified: same world and controller, passage removed, oxygen %.3f -> %.3f, light and archive retained."), LastOxygen, Oxygen);
    }
    return bPassed;
}

bool CheckResupply(const AVanguardCharacter& Player)
{
    const auto* Session = UVanguardSession::Find(Player);
    return Session && Session->Suit.OxygenPercent() == 100.0f && ExpectedArchive(Session->Suit.Records(), true) && CheckSlot(true);
}
}
#endif
