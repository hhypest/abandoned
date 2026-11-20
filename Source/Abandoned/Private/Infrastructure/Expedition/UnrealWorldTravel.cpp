#include "Infrastructure/Expedition/UnrealWorldTravel.h"

#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldTravel, Log, All);

namespace Abandoned::Infrastructure::Expedition
{
std::optional<WorldDestination> ReadWorldDestination(const FString& Options, EntryWorld World, FString& Error)
{
    const FTCHARToUTF8 Encoded(*Options);
    const auto Decoded = DecodeWorldOptions(std::string_view(Encoded.Get(), Encoded.Length()), World);
    if (!Decoded.Destination)
    {
        Error = TEXT("Invalid segment travel options.");
        UE_LOG(LogWorldTravel, Warning, TEXT("%s"), *Error);
    }
    else if (Decoded.InvalidRecoveryOrigin)
    {
        UE_LOG(LogWorldTravel, Warning, TEXT("Invalid passage checkpoint origin. Using the saved checkpoint or the direct-entry fallback."));
    }
    return Decoded.Destination;
}

void TravelToWorld(const UObject* Context, const WorldDestination& Destination, bool bSkipOutgoingRedraw)
{
    std::string Options = EncodeWorldOptions(Destination);
    if (bSkipOutgoingRedraw)
    {
        // LoadMap otherwise redraws the outgoing scene without presenting it before the full GC.
        Options += "?quiet";
    }
    UGameplayStatics::OpenLevel(Context, FName(UTF8_TO_TCHAR(EntryMapPath)), true, UTF8_TO_TCHAR(Options.c_str()));
}
}
