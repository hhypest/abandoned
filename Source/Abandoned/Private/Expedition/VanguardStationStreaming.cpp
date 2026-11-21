#include "Expedition/VanguardRoute.h"

#include "Components/BoxComponent.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogStationAirlock, Log, All);

namespace
{
void RemoveInstance(ULevelStreaming& Instance)
{
    Instance.SetShouldBeVisible(false);
    Instance.SetShouldBeLoaded(false);
    Instance.SetIsRequestingUnloadAndRemoval(true);
}
}

AVanguardSegment* AVanguardSegment::ActiveIn(UWorld& World)
{
    for (TActorIterator<AVanguardSegment> It(&World); It; ++It)
    {
        if (It->bActive) { return *It; }
    }
    return nullptr;
}

bool AVanguardSegment::StationReady() const
{
    if (!IsValid(IncomingStation) || !StationInstance || !StationInstance->IsLevelVisible()) { return false; }
    const auto* Cabinet = IncomingStation->SupplyCabinet.Get();
    ULevel* Level = StationInstance->GetLoadedLevel();
    if (!IsValid(Cabinet) || !Cabinet->IsRegistered() || !Cabinet->IsQueryCollisionEnabled() ||
        Cabinet->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block || Cabinet->GetOwner()->GetLevel() != Level)
    {
        return false;
    }
    const FVector Spawn = IncomingStation->PlayerStart();
    FHitResult Floor;
    return GetWorld()->LineTraceSingleByChannel(Floor, Spawn, Spawn - FVector(0, 0, 150), ECC_Pawn) &&
        Floor.GetActor() && Floor.GetActor()->GetLevel() == Level;
}

void AVanguardSegment::PollStation()
{
    if (bStationFailed) { return; }
    if (StationInstance && StationInstance->GetIsRequestingUnloadAndRemoval())
    {
        if (GetWorld()->GetStreamingLevels().Contains(StationInstance)) { return; }
        StationInstance = nullptr;
    }
    if (!StationInstance)
    {
        bool bAccepted = false;
        ULevelStreamingDynamic::FLoadLevelInstanceParams Request(GetWorld(), TEXT("/Game/Abandoned/Chambers/L_Station"),
            FTransform(FVector(0, 36000, 0)));
#if WITH_DEV_AUTOMATION_TESTS
        Request.bInitiallyVisible = !FParse::Param(FCommandLine::Get(), TEXT("VerifyStationAirlock"));
#endif
        StationInstance = ULevelStreamingDynamic::LoadLevelInstance(Request, bAccepted);
        if (bAccepted && StationInstance) { return; }
    }
    else if (StationInstance->GetLevelStreamingState() != ELevelStreamingState::FailedToLoad)
    {
        if (!StationInstance->IsLevelVisible()) { return; }
        if (IncomingStation && StationReady()) { return; }
        if (!IncomingStation)
        {
            for (AActor* Actor : StationInstance->GetLoadedLevel()->Actors)
            {
                auto* Station = Cast<AVanguardSegment>(Actor);
                if (!Station) { continue; }
                FString Error;
                Station->Prepare(RouteState->NextStation(), Error);
                if (Error.IsEmpty())
                {
                    IncomingStation = Station;
                    if (StationReady())
                    {
                        UE_LOG(LogStationAirlock, Display, TEXT("Station airlock ready: floor and supply cabinet registered."));
                        return;
                    }
                }
                UE_LOG(LogStationAirlock, Warning, TEXT("Station preparation failed. %s"), *Error);
                break;
            }
        }
    }
    bStationFailed = true;
    IncomingStation = nullptr;
    if (StationInstance) { RemoveInstance(*StationInstance); }
    UE_LOG(LogStationAirlock, Warning, TEXT("Station unavailable. Airlock locked; press R to retry."));
}

bool AVanguardSegment::EnterStation(AVanguardCharacter& Player)
{
    auto* PC = Cast<APlayerController>(Player.GetController());
    if (!PC || !StationReady()) { return false; }
    FActorSpawnParameters Params;
    Params.OverrideLevel = IncomingStation->GetLevel();
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
    auto* Suit = GetWorld()->SpawnActor<AVanguardCharacter>(IncomingStation->PlayerStart(), FRotator::ZeroRotator, Params);
    if (!Suit) { return false; }
    PC->Possess(Suit);
    if (PC->GetPawn() != Suit) { Suit->Destroy(); return false; }
    Player.CloseWristTerminal();
    Player.Destroy();
    PC->SetControlRotation(FRotator::ZeroRotator);
    bActive = false;
    IncomingStation->Activate();
    IncomingStation->Equip(*Suit);
    UE_LOG(LogStationAirlock, Display, TEXT("Passage exit crossed: station active in the same world."));
    if (GetLevel() == GetWorld()->PersistentLevel)
    {
        // Direct passage launches build their devices in the persistent level.
        const auto Owned = Children;
        for (AActor* Actor : Owned) { if (IsValid(Actor)) { Actor->Destroy(); } }
        Destroy();
    }
    else
    {
        for (ULevelStreaming* Instance : GetWorld()->GetStreamingLevels())
        {
            if (Instance && Instance->GetLoadedLevel() == GetLevel()) { RemoveInstance(*Instance); break; }
        }
    }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool AVanguardSegment::RevealStationForVerification()
{
    if (!StationInstance || !StationInstance->IsLevelLoaded() || StationInstance->IsLevelVisible()) { return false; }
    StationInstance->SetShouldBeVisible(true);
    return true;
}
#endif
