#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "Components/BoxComponent.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogChamberAirlock, Log, All);

using Abandoned::Domain::Expedition::Segment;

namespace
{
const FVector ButtonOrigin(0, 6000, 0);
const FVector WeightOrigin(0, 12000, 0);

void UnloadChamber(ULevelStreamingDynamic& Instance)
{
    Instance.SetShouldBeVisible(false);
    Instance.SetShouldBeLoaded(false);
    Instance.SetIsRequestingUnloadAndRemoval(true);
}
}

bool ATrainingChamberGameMode::NeedsAirlock() const
{
    return !bDev20Run && (Course.Current == Segment::Stairs || Course.Current == Segment::Button);
}

void ATrainingChamberGameMode::RequestNextChamber()
{
    // A reset can arrive while UE is still removing a failed instance at this location.
    if (IncomingInstance && (IncomingInstance->IsLevelLoaded() || GetWorld()->GetStreamingLevels().Contains(IncomingInstance)))
    {
        bChamberRetryRequested = true;
        return;
    }
    bChamberRetryRequested = false;
    bool bAccepted = false;
    const bool bToButton = Course.Current == Segment::Stairs;
    ULevelStreamingDynamic::FLoadLevelInstanceParams Request(GetWorld(),
        bToButton ? TEXT("/Game/Abandoned/Chambers/Button") : TEXT("/Game/Abandoned/Verification/WeightShellPilot"),
        FTransform(bToButton ? ButtonOrigin : WeightOrigin));
#if WITH_DEV_AUTOMATION_TESTS
    Request.bInitiallyVisible = !FParse::Param(FCommandLine::Get(), bToButton ? TEXT("VerifyButtonAirlock") : TEXT("VerifyWeightAirlock"));
#endif
    IncomingInstance = ULevelStreamingDynamic::LoadLevelInstance(Request, bAccepted);
    bChamberLoadFailed = !bAccepted || !IncomingInstance;
    if (bChamberLoadFailed) { UE_LOG(LogChamberAirlock, Warning, TEXT("Chamber load request failed. Reset to retry.")); }
}

void ATrainingChamberGameMode::PrepareNextChamber()
{
    if (bChamberLoadFailed)
    {
        if (bChamberRetryRequested) { RequestNextChamber(); }
        return;
    }
    if (!IncomingInstance) { return; }
    if (IncomingChamber)
    {
        if (IsValid(IncomingChamber) && IncomingInstance->IsLevelVisible() && IncomingChamber->IsStreamedRoomReady()) { return; }
        IncomingChamber = nullptr;
        bChamberLoadFailed = true;
    }
    if (bChamberLoadFailed || IncomingInstance->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
    {
        bChamberLoadFailed = true;
    }
    else
    {
        if (!IncomingInstance->IsLevelVisible()) { return; }
        ULevel* Level = IncomingInstance->GetLoadedLevel();
        AChamberAssembly* Room = nullptr;
        for (AActor* Actor : Level->Actors)
        {
            if (auto* Assembly = Cast<AChamberAssembly>(Actor)) { Room = Assembly; break; }
        }
        FString Error;
        if (Room)
        {
            const bool bToButton = Course.Current == Segment::Stairs;
            const UTrainingChamberLayout* Template = bToButton
                ? static_cast<const UTrainingChamberLayout*>(GetDefault<UEntryButtonLayout>()) : GetDefault<UEntryWeightLayout>();
            Room->BuildStreamed(*Template, SurfaceMaterial, Error);
            FHitResult Floor;
            const FVector Spawn = Room->Layout ? Room->Layout->Spawn : FVector::ZeroVector;
            if (Error.IsEmpty() && Room->IsStreamedRoomReady() &&
                GetWorld()->LineTraceSingleByChannel(Floor, Spawn, Spawn - FVector(0,0,150), ECC_Pawn) &&
                Floor.GetActor() && Floor.GetActor()->GetLevel() == Level)
            {
                IncomingChamber = Room;
                UE_LOG(LogChamberAirlock, Display, TEXT("%s airlock ready: floor, ballast and circuit registered."), bToButton ? TEXT("Button") : TEXT("Weight"));
                return;
            }
        }
        UE_LOG(LogChamberAirlock, Warning, TEXT("Chamber devices unavailable. Reset to retry. %s"), *Error);
        bChamberLoadFailed = true;
    }
    UnloadChamber(*IncomingInstance);
}

bool ATrainingChamberGameMode::EnterNextChamber(ATrainingChamberCharacter& Player)
{
    if (!IncomingChamber || !IncomingChamber->IsStreamedRoomReady() || !IsDoorOpen() ||
        !Layout->GetExitBounds().IsInside(Player.GetActorLocation())) { return false; }

    if (!Player.TeleportTo(IncomingChamber->Layout->Spawn, FRotator::ZeroRotator)) { return false; }
    Player.ReleaseHeldObject();
    Player.GetCharacterMovement()->StopMovementImmediately();
    Player.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    if (Player.GetController()) { Player.GetController()->SetControlRotation(FRotator::ZeroRotator); }
    AChamberAssembly* Previous = Devices;
    Devices = IncomingChamber;
    IncomingChamber = nullptr;
    Layout = Devices->Layout;
    Course.Current = Course.Current == Segment::Stairs ? Segment::Button : Segment::Weight;
    HintReader = {};
    bReachedExit = false;
    if (ActiveInstance)
    {
        Previous->DoorDrive->ReleaseClamp();
        UnloadChamber(*ActiveInstance);
    }
    else { Previous->Retire(); }
    ActiveInstance = IncomingInstance;
    IncomingInstance = nullptr;
    UE_LOG(LogChamberAirlock, Display, TEXT("%s airlock crossed: same pawn transferred to the prepared chamber."),
        Course.Current == Segment::Button ? TEXT("Button") : TEXT("Weight"));
    SaveEntryCheckpoint();
    if (NeedsAirlock()) { RequestNextChamber(); }
    ActivateChamber();
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool ATrainingChamberGameMode::RevealChamberForVerification()
{
    if (!IncomingInstance || !IncomingInstance->IsLevelLoaded() || IncomingInstance->IsLevelVisible()) { return false; }
    IncomingInstance->SetShouldBeVisible(true);
    return true;
}
#endif
