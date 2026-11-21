#include "Verification/PassageAirlockVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRoute.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Misc/CommandLine.h"
#include "TrainingChamber/TrainingChamber.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogPassageAirlockTest, Log, All);

namespace Abandoned::Verification::PassageAirlock
{
namespace
{
int32 Visits = 0;
TWeakObjectPtr<UWorld> SourceWorld;
TWeakObjectPtr<APlayerController> SourceController;
TWeakObjectPtr<ATrainingChamberCharacter> SourcePawn;
TWeakObjectPtr<AVanguardCharacter> Suit;
}

bool Requested() { return FParse::Param(FCommandLine::Get(), TEXT("VerifyPassageAirlock")); }

bool SlotIsBalance()
{
    const auto Saved = Infrastructure::Expedition::UnrealCheckpointStore{}.Read();
    return Saved.Chamber == Domain::Expedition::Segment::Balance && Saved.Archive.Size() == 1 &&
        Saved.Archive.At(0) == Domain::Expedition::SuitRecord::OxygenRestored;
}

bool ReturnedToBalance(ATrainingChamberCharacter& Player)
{
    const auto* PC = Cast<APlayerController>(Player.GetController());
    if (!SlotIsBalance() || !PC || !Cast<ATrainingChamberHUD>(PC->GetHUD()) || Suit.IsValid() ||
        (Visits > 0 && SourceWorld.Get() == Player.GetWorld()))
    {
        UE_LOG(LogPassageAirlockTest, Error, TEXT("Incorrect checkpoint, pawn, HUD or world on return to Balance."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return true;
    }
    if (Visits > 0)
    {
        UE_LOG(LogPassageAirlockTest, Display, TEXT("Passage return %d: Balance controls, HUD and archive restored."), Visits);
    }
    if (Visits < 2) { return false; }
    FPlatformMisc::RequestExitWithStatus(false, 0);
    return true;
}

void RecordDeparture(ATrainingChamberCharacter& Player)
{
    SourceWorld = Player.GetWorld();
    SourceController = Cast<APlayerController>(Player.GetController());
    SourcePawn = &Player;
}

void CheckPassage(const AVanguardSegment& Segment, AVanguardCharacter& Player)
{
    if (Suit.Get() == &Player) { return; }
    const auto* PC = Cast<APlayerController>(Player.GetController());
    const auto* Floor = Player.GetMovementBase();
    if (!PC || PC != SourceController.Get() || SourceWorld.Get() != Player.GetWorld() || SourcePawn.IsValid() ||
        !Cast<AVanguardHUD>(PC->GetHUD()) || Player.GetLevel() != Segment.GetLevel() ||
        Segment.GetLevel() == Player.GetWorld()->PersistentLevel || !Floor || Floor->GetOwner()->GetLevel() != Segment.GetLevel() ||
        !SlotIsBalance())
    {
        UE_LOG(LogPassageAirlockTest, Error, TEXT("Passage handoff lost the world, controller, floor, HUD or Balance slot."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    Suit = &Player;
    UE_LOG(LogPassageAirlockTest, Display, TEXT("Passage visit %d: suit on streamed floor, same world and controller, Balance slot intact."), ++Visits);
    // Exercise the regular out-of-bounds recovery on the next gameplay tick.
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Player.GetActorLocation() - FVector(0, 0, 1000)));
}
}
#endif
