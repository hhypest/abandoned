#pragma once

#if WITH_DEV_AUTOMATION_TESTS
class AVanguardCharacter;
class AVanguardSegment;

namespace Abandoned::Verification::StationAirlock
{
bool Requested();
bool OriginalSlot();
void ObservePassage(const AVanguardSegment& Passage, const AVanguardCharacter& Player);
bool CheckArrival(const AVanguardSegment& Station, const AVanguardCharacter& Player);
bool CheckResupply(const AVanguardCharacter& Player);
}
#endif
