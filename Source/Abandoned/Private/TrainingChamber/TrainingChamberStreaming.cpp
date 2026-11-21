#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "Components/BoxComponent.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"

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
    WeightInstance = ULevelStreamingDynamic::LoadLevelInstance(Request, bAccepted);
    bWeightLoadFailed = !bAccepted || !WeightInstance;
    if (bWeightLoadFailed) { UE_LOG(LogTemp, Warning, TEXT("Weight load request failed. Reset to retry.")); }
}

void ATrainingChamberGameMode::PrepareWeight()
{
    if (IncomingWeight || bWeightLoadFailed || !WeightInstance) { return; }
    if (WeightInstance->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
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
                UE_LOG(LogTemp, Display, TEXT("Weight airlock ready: floor, four ballast bodies, plate, switch and drive."));
                return;
            }
        }
        UE_LOG(LogTemp, Warning, TEXT("Weight devices unavailable. Reset to retry. %s"), *Error);
        bWeightLoadFailed = true;
    }
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
    UE_LOG(LogTemp, Display, TEXT("Weight airlock crossed: same pawn transferred to the prepared chamber."));
    SaveEntryCheckpoint();
    ActivateChamber();
    return true;
}
