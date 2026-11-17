#include "Infrastructure/Expedition/UnrealEntryCourse.h"

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

}
