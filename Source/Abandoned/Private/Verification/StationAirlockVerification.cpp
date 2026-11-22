#include "CoreMinimal.h"
#include "Verification/StationAirlockVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardSession.h"
#include "Verification/AirlockVerificationSession.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogStationAirlockTest, Log, All);

namespace Abandoned::Verification::StationAirlock
{
namespace
{
using Domain::Expedition::SuitRecord;
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
    auto* Check = UAirlockVerificationSession::Find(Player);
    if (!Check) { return; }
    Check->Station.PassageWorld = Passage.GetWorld();
    Check->Station.PassageLevel = Passage.GetLevel();
    Check->Station.Controller = Player.GetController();
    Check->Station.LastOxygen = Passage.GetRouteState().GetOxygenPercent();
}

bool CheckArrival(const AVanguardSegment& Station, const AVanguardCharacter& Player)
{
    const auto* Session = UVanguardSession::Find(Player);
    const auto* Check = UAirlockVerificationSession::Find(Player);
    if (!Check) { return false; }
    const auto* Floor = Player.GetMovementBase();
    const float Oxygen = Station.GetRouteState().GetOxygenPercent();
    const bool bPassed = Check->Station.PassageWorld.Get() == Station.GetWorld() && Check->Station.Controller.IsValid() && Player.GetController() == Check->Station.Controller.Get() &&
        !Station.GetWorld()->GetLevels().Contains(Check->Station.PassageLevel.Get()) && Station.GetLevel() != Station.GetWorld()->PersistentLevel &&
        Player.GetLevel() == Station.GetLevel() && Floor && Floor->GetOwner()->GetLevel() == Station.GetLevel() &&
        Station.GetRouteState().AtStation() && Check->Station.LastOxygen < 100.0f && FMath::IsNearlyEqual(Oxygen, Check->Station.LastOxygen, 0.02f) &&
        Player.IsFlashlightOn() && Session && ExpectedArchive(Session->Suit.Records(), false) && OriginalSlot();
    if (bPassed)
    {
        UE_LOG(LogStationAirlockTest, Display, TEXT("Station handoff verified: same world and controller, passage removed, oxygen %.3f -> %.3f, light and archive retained."), Check->Station.LastOxygen, Oxygen);
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
