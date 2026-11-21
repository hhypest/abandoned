#pragma once

#if WITH_DEV_AUTOMATION_TESTS
class ATrainingChamberCharacter;
class AVanguardCharacter;
class AVanguardSegment;

namespace Abandoned::Verification::PassageAirlock
{
bool Requested();
bool SlotIsBalance();
bool ReturnedToBalance(ATrainingChamberCharacter& Player);
void RecordDeparture(ATrainingChamberCharacter& Player);
void CheckPassage(const AVanguardSegment& Segment, AVanguardCharacter& Player);
}
#endif
