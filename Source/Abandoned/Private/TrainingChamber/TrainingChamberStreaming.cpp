#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "Components/BoxComponent.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeightAirlock, Log, All);

using Abandoned::Domain::Expedition::Segment;

namespace
{
// Both chambers have a sealed back wall. Transfer at the far end of Button's exit passage.
const FVector WeightOrigin(0, 12000, 0);
}

bool ATrainingChamberGameMode::NeedsWeightAirlock() const
{
    return Course.Current == Segment::Button && !bDev20Run;
}

void ATrainingChamberGameMode::RequestWeight()
{
    bool bAccepted = false;
    ULevelStreamingDynamic::FLoadLevelInstanceParams Request(GetWorld(),
        TEXT("/Game/Abandoned/Verification/WeightShellPilot"), FTransform(WeightOrigin));
#if WITH_DEV_AUTOMATION_TESTS
    Request.bInitiallyVisible = !FParse::Param(FCommandLine::Get(), TEXT("VerifyWeightAirlock"));
#endif
    WeightInstance = ULevelStreamingDynamic::LoadLevelInstance(Request, bAccepted);
    bWeightLoadFailed = !bAccepted || !WeightInstance;
    if (bWeightLoadFailed) { UE_LOG(LogWeightAirlock, Warning, TEXT("Weight load request failed. Reset to retry.")); }
}

void ATrainingChamberGameMode::PrepareWeight()
{
    if (bWeightLoadFailed || !WeightInstance) { return; }
    if (IncomingWeight)
    {
        if (IsValid(IncomingWeight) && WeightInstance->IsLevelVisible() && IncomingWeight->IsWeightReady()) { return; }
        IncomingWeight = nullptr;
        bWeightLoadFailed = true;
    }
    if (bWeightLoadFailed || WeightInstance->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
    {
        bWeightLoadFailed = true;
    }
    else
    {
        if (!WeightInstance->IsLevelVisible()) { return; }
        ULevel* Level = WeightInstance->GetLoadedLevel();
        AChamberAssembly* Room = nullptr;
        for (AActor* Actor : Level->Actors)
        {
            if (auto* Assembly = Cast<AChamberAssembly>(Actor)) { Room = Assembly; break; }
        }
        FString Error;
        if (Room)
        {
            Room->BuildStreamedWeight(SurfaceMaterial, Error);
            FHitResult Floor;
            const FVector Spawn = Room->Layout->Spawn;
            if (Error.IsEmpty() && Room->IsWeightReady() &&
                GetWorld()->LineTraceSingleByChannel(Floor, Spawn, Spawn - FVector(0,0,150), ECC_Pawn) &&
                Floor.GetActor() && Floor.GetActor()->GetLevel() == Level)
            {
                IncomingWeight = Room;
                UE_LOG(LogWeightAirlock, Display, TEXT("Weight airlock ready: floor, four ballast bodies, plate, switch and drive."));
                return;
            }
        }
        UE_LOG(LogWeightAirlock, Warning, TEXT("Weight devices unavailable. Reset to retry. %s"), *Error);
        bWeightLoadFailed = true;
    }
    WeightInstance->SetShouldBeVisible(false);
    WeightInstance->SetShouldBeLoaded(false);
    WeightInstance->SetIsRequestingUnloadAndRemoval(true);
    WeightInstance = nullptr;
}

bool ATrainingChamberGameMode::EnterWeight(ATrainingChamberCharacter& Player)
{
    if (!IncomingWeight || !IncomingWeight->IsWeightReady() || !IsDoorOpen() ||
        !Layout->GetExitBounds().IsInside(Player.GetActorLocation())) { return false; }

    if (!Player.TeleportTo(IncomingWeight->Layout->Spawn, FRotator::ZeroRotator)) { return false; }
    Player.ReleaseHeldObject();
    Player.GetCharacterMovement()->StopMovementImmediately();
    Player.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    if (Player.GetController()) { Player.GetController()->SetControlRotation(FRotator::ZeroRotator); }
    AChamberAssembly* Previous = Devices;
    Devices = IncomingWeight;
    IncomingWeight = nullptr;
    Layout = Devices->Layout;
    Course.Current = Segment::Weight;
    HintReader = {};
    bReachedExit = false;
    Previous->Retire();
    UE_LOG(LogWeightAirlock, Display, TEXT("Weight airlock crossed: same pawn transferred to the prepared chamber."));
    SaveEntryCheckpoint();
    ActivateChamber();
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool ATrainingChamberGameMode::RevealWeightForVerification()
{
    if (!WeightInstance || !WeightInstance->IsLevelLoaded() || WeightInstance->IsLevelVisible()) { return false; }
    WeightInstance->SetShouldBeVisible(true);
    return true;
}
#endif
