#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "Components/BoxComponent.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogChamberAirlock, Log, All);

using Abandoned::Domain::Expedition::Segment;

namespace
{
struct FStreamedChamber
{
    Segment Destination;
    const TCHAR* Map;
    FVector Origin;
    UClass* LayoutClass;
    const TCHAR* VerificationFlag;
};

const FStreamedChamber* NextRoom(Segment Current)
{
    static const FStreamedChamber Rooms[] = {
        {Segment::Button, TEXT("/Game/Abandoned/Chambers/Button"), {0,6000,0}, UEntryButtonLayout::StaticClass(), TEXT("VerifyButtonAirlock")},
        {Segment::Weight, TEXT("/Game/Abandoned/Verification/WeightShellPilot"), {0,12000,0}, UEntryWeightLayout::StaticClass(), TEXT("VerifyWeightAirlock")},
        {Segment::Balance, TEXT("/Game/Abandoned/Chambers/L_Balance"), {0,18000,0}, UWeightBalanceLayout::StaticClass(), TEXT("VerifyBalanceAirlock")}
    };
    const auto Next = NextSegment(Current);
    for (const auto& Room : Rooms)
    {
        if (Room.Destination == Next) { return &Room; }
    }
    return nullptr;
}

void UnloadChamber(ULevelStreamingDynamic& Instance)
{
    Instance.SetShouldBeVisible(false);
    Instance.SetShouldBeLoaded(false);
    Instance.SetIsRequestingUnloadAndRemoval(true);
}
}

bool ATrainingChamberGameMode::NeedsAirlock() const
{
    return !bDev20Run && NextRoom(Course.Current) != nullptr;
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
    const auto* Next = NextRoom(Course.Current);
    if (!Next) { return; }
    ULevelStreamingDynamic::FLoadLevelInstanceParams Request(GetWorld(), Next->Map, FTransform(Next->Origin));
#if WITH_DEV_AUTOMATION_TESTS
    Request.bInitiallyVisible = !FParse::Param(FCommandLine::Get(), Next->VerificationFlag);
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
        const auto* Next = NextRoom(Course.Current);
        if (Room && Next)
        {
            Room->BuildStreamed(*Next->LayoutClass->GetDefaultObject<UTrainingChamberLayout>(), SurfaceMaterial, Error);
            FHitResult Floor;
            const FVector Spawn = Room->Layout ? Room->Layout->Spawn : FVector::ZeroVector;
            if (Error.IsEmpty() && Room->IsStreamedRoomReady() &&
                GetWorld()->LineTraceSingleByChannel(Floor, Spawn, Spawn - FVector(0,0,150), ECC_Pawn) &&
                Floor.GetActor() && Floor.GetActor()->GetLevel() == Level)
            {
                IncomingChamber = Room;
                UE_LOG(LogChamberAirlock, Display, TEXT("%s airlock ready: floor, ballast and circuit registered."), *Abandoned::Infrastructure::Expedition::WriteSegment(Next->Destination));
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
    const auto* Next = NextRoom(Course.Current);
    if (!Next || !IncomingChamber || !IncomingChamber->IsStreamedRoomReady() || !IsDoorOpen() ||
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
    Course.Current = Next->Destination;
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
        *Abandoned::Infrastructure::Expedition::WriteSegment(Course.Current));
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
