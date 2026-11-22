#include "Verification/PassageAirlockVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRoute.h"
#include "Verification/AirlockVerificationSession.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Misc/CommandLine.h"
#include "TrainingChamber/TrainingChamber.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogPassageAirlockTest, Log, All);

namespace Abandoned::Verification::PassageAirlock
{
bool Requested() { return FParse::Param(FCommandLine::Get(), TEXT("VerifyPassageAirlock")); }

bool SlotIsBalance()
{
    const auto Saved = Infrastructure::Expedition::UnrealCheckpointStore{}.Read();
    return Saved.Chamber == Domain::Expedition::Segment::Balance && Saved.Archive.Size() == 1 &&
        Saved.Archive.At(0) == Domain::Expedition::SuitRecord::OxygenRestored;
}

bool ReturnedToBalance(ATrainingChamberCharacter& Player)
{
    auto* Session = UAirlockVerificationSession::Find(Player);
    const auto* PC = Cast<APlayerController>(Player.GetController());
    if (!Session || !SlotIsBalance() || !PC || !Cast<ATrainingChamberHUD>(PC->GetHUD()) || Session->Passage.Suit.IsValid() ||
        (Session->Passage.Visits > 0 && Session->Passage.SourceWorld.Get() == Player.GetWorld()))
    {
        UE_LOG(LogPassageAirlockTest, Error, TEXT("Incorrect checkpoint, pawn, HUD or world on return to Balance."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return true;
    }
    if (Session->Passage.Visits > 0)
    {
        UE_LOG(LogPassageAirlockTest, Display, TEXT("Passage return %d: Balance controls, HUD and archive restored."), Session->Passage.Visits);
    }
    if (Session->Passage.Visits < 2) { return false; }
    FPlatformMisc::RequestExitWithStatus(false, 0);
    return true;
}

void RecordDeparture(ATrainingChamberCharacter& Player)
{
    auto* Session = UAirlockVerificationSession::Find(Player);
    if (!Session) { return; }
    Session->Passage.SourceWorld = Player.GetWorld();
    Session->Passage.SourceController = Cast<APlayerController>(Player.GetController());
    Session->Passage.SourcePawn = &Player;
}

void CheckPassage(const AVanguardSegment& Segment, AVanguardCharacter& Player)
{
    auto* Session = UAirlockVerificationSession::Find(Player);
    if (Session && Session->Passage.Suit.Get() == &Player) { return; }
    const auto* PC = Cast<APlayerController>(Player.GetController());
    const auto* Floor = Player.GetMovementBase();
    if (!Session || !PC || PC != Session->Passage.SourceController.Get() || Session->Passage.SourceWorld.Get() != Player.GetWorld() || Session->Passage.SourcePawn.IsValid() ||
        !Cast<AVanguardHUD>(PC->GetHUD()) || Player.GetLevel() != Segment.GetLevel() ||
        Segment.GetLevel() == Player.GetWorld()->PersistentLevel || !Floor || Floor->GetOwner()->GetLevel() != Segment.GetLevel() ||
        !SlotIsBalance())
    {
        UE_LOG(LogPassageAirlockTest, Error, TEXT("Passage handoff lost the world, controller, floor, HUD or Balance slot."));
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    Session->Passage.Suit = &Player;
    UE_LOG(LogPassageAirlockTest, Display, TEXT("Passage visit %d: suit on streamed floor, same world and controller, Balance slot intact."), ++Session->Passage.Visits);
    // Exercise the regular out-of-bounds recovery on the next gameplay tick.
    Player.SetActorLocation(Player.GetActorLocation() - FVector(0, 0, 1000), false, nullptr, ETeleportType::TeleportPhysics);
}
}
#endif
