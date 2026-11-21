#include "Verification/WeightStreamingVerification.h"

UWeightStreamingVerification::UWeightStreamingVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/Level.h"
#include "TrainingChamber/ChamberAssembly.h"
#include "Materials/MaterialInterface.h"
#include "Puzzles/PuzzleCube.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogWeightStreaming, Log, All);

namespace
{
// Place the pilot outside the playable Stairs layout, at the same height.
const FVector PilotOrigin(0.0, 12000.0, 0.0);
constexpr TCHAR PilotMap[] = TEXT("/Game/Abandoned/Verification/WeightShellPilot");

void RemoveInstance(ULevelStreamingDynamic& Level)
{
    Level.SetShouldBeVisible(false);
    Level.SetShouldBeLoaded(false);
    Level.SetIsRequestingUnloadAndRemoval(true);
}

void Fail(const TCHAR* Reason)
{
    UE_LOG(LogWeightStreaming, Error, TEXT("Weight streaming verification failed: %s"), Reason);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}
}

void UWeightStreamingVerification::AttachIfRequested(AActor& Stairs)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("VerifyWeightStreaming")))
    {
        auto* Check = NewObject<UWeightStreamingVerification>(&Stairs);
        Stairs.AddInstanceComponent(Check);
        Check->RegisterComponent();
    }
}

void UWeightStreamingVerification::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Delta, TickType, TickFunction);
    UWorld& World = *GetWorld();
    const auto* PC = World.GetFirstPlayerController();
    ACharacter* Player = PC ? PC->GetCharacter() : nullptr;
    if (!Player) { return; }
    auto* Level = Instance.Get();
    const auto* Base = Player->GetMovementBase();
    const ULevel* Support = Base && Base->GetOwner() ? Base->GetOwner()->GetLevel() : nullptr;
    FHitResult Hit;
    auto ProbeFloor = [&]()
    {
        return World.LineTraceSingleByChannel(Hit, PilotOrigin + FVector(0,0,60), PilotOrigin - FVector(0,0,60),
            ECC_Pawn, FCollisionQueryParams(SCENE_QUERY_STAT(WeightPilotFloor), false, Player));
    };

    switch (Phase)
    {
    case EPhase::Start:
    {
        if (Support != World.PersistentLevel) { return; }
        if (ProbeFloor()) { return Fail(TEXT("pilot site occupied before load")); }
        Home = Player->GetActorTransform();
        ULevelStreamingDynamic::FLoadLevelInstanceParams Params(&World, PilotMap, FTransform(PilotOrigin));
        Params.bInitiallyVisible = false;
        bool bAccepted = false;
        Level = ULevelStreamingDynamic::LoadLevelInstance(Params, bAccepted);
        if (!bAccepted || !Level) { return Fail(TEXT("could not request cooked sublevel")); }
        Instance = Level;
        if (!bCancelledFirstRequest)
        {
            RemoveInstance(*Level);
            Phase = EPhase::Removal;
        }
        else { Phase = EPhase::Hidden; }
        break;
    }
    case EPhase::Hidden:
        if (!Level || !Level->IsLevelLoaded()) { return; }
        if (Level->IsLevelVisible() || ProbeFloor()) { return Fail(TEXT("hidden shell already collides")); }
        Level->SetShouldBeVisible(true);
        Phase = EPhase::Visible;
        break;
    case EPhase::Visible:
        if (!Level || !Level->IsLevelVisible()) { return; }
        if (!ProbeFloor() || !Hit.GetActor() || Hit.GetActor()->GetLevel() != Level->GetLoadedLevel() ||
            !FMath::IsNearlyZero(Hit.ImpactPoint.Z, 1.0)) { return Fail(TEXT("visible Weight floor missing")); }
        for (AActor* Actor : Level->GetLoadedLevel()->Actors)
        {
            if (APuzzleCube* Cube = Cast<APuzzleCube>(Actor)) { Ballast.Add(Cube); }
            if (auto* Room = Cast<AChamberAssembly>(Actor)) { Assembly = Room; }
        }
        {
            if (!Assembly.IsValid()) { return Fail(TEXT("missing Weight device assembly")); }
            FString Error;
            Assembly->BuildStreamed(*GetDefault<UEntryWeightLayout>(), LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface")), Error);
            if (!Error.IsEmpty() || !Assembly->IsStreamedRoomReady()) { return Fail(TEXT("incomplete Weight circuit")); }
            DeviceBodies = {Assembly->PlateCircuit->GetPlate(0), Assembly->DoorDrive->GetDoor(), Assembly->Button.Get()};
            const auto& Spawns = GetDefault<UEntryWeightLayout>()->CubeSpawns;
            if (Ballast.IsEmpty() || Ballast.Num() != Spawns.Num()) { return Fail(TEXT("streamed ballast count")); }
            for (const FTrainingCubeSpawn& Spawn : Spawns)
            {
                const float Mass = Spawn.Type == EPuzzleCubeType::Small ? 5.0f : 10.0f;
                const FVector Center = PilotOrigin + Spawn.Center;
                if (!Ballast.ContainsByPredicate([&](const TWeakObjectPtr<APuzzleCube>& Entry)
                {
                    const APuzzleCube* Cube = Entry.Get();
                    return Cube && Cube->GetLevel() == Level->GetLoadedLevel() && Cube->GetBody()->IsSimulatingPhysics() &&
                        FMath::IsNearlyEqual(Cube->GetBody()->GetMass(), Mass, 0.1f) &&
                        FVector::Dist(Cube->GetActorLocation(), Center) < 12.0;
                })) { return Fail(TEXT("missing physical ballast at configured spawn")); }
            }
        }
        if (!Player->TeleportTo(PilotOrigin + FVector(0,0,125), Home.Rotator())) { return Fail(TEXT("arrival obstructed")); }
        Phase = EPhase::Land;
        break;
    case EPhase::Land:
        if (!Level || !Support || Support != Level->GetLoadedLevel() || !Player->GetCharacterMovement()->IsMovingOnGround()) { return; }
        if (!Player->TeleportTo(Home.GetLocation(), Home.Rotator())) { return Fail(TEXT("return obstructed")); }
        Phase = EPhase::Return;
        break;
    case EPhase::Return:
        if (Support != World.PersistentLevel) { return; }
        if (!Level) { return Fail(TEXT("lost Weight before return")); }
        ++CompletedVisits;
        UE_LOG(LogWeightStreaming, Display, TEXT("Weight streaming visit %d: player landed on Weight and returned to Stairs."), CompletedVisits);
        RemoveInstance(*Level);
        Phase = EPhase::Removal;
        break;
    case EPhase::Removal:
        if (Level && (Level->IsLevelLoaded() || World.GetStreamingLevels().Contains(Level))) { return; }
        if (ProbeFloor()) { return Fail(TEXT("orphan collision after removal")); }
        for (const auto& Entry : Ballast)
        {
            if (const APuzzleCube* Cube = Entry.Get(); Cube && Cube->GetBody()->IsRegistered())
            {
                return Fail(TEXT("ballast body still registered after unload"));
            }
        }
        if (DeviceBodies.ContainsByPredicate([](const TWeakObjectPtr<UBoxComponent>& Body)
        {
            return Body.IsValid() && Body->IsRegistered();
        })) { return Fail(TEXT("Weight device collision survived unload")); }
        if (!Ballast.IsEmpty())
        {
            UE_LOG(LogWeightStreaming, Display, TEXT("Weight visit %d: plate, switch and door drive removed."), CompletedVisits);
            DeviceBodies.Reset();
            Assembly.Reset();
            UE_LOG(LogWeightStreaming, Display, TEXT("Weight visit %d: removed %d verified ballast bodies."), CompletedVisits, Ballast.Num());
            Ballast.Reset();
        }
        if (!bCancelledFirstRequest)
        {
            bCancelledFirstRequest = true;
            UE_LOG(LogWeightStreaming, Display, TEXT("Weight streaming cancelled before visibility; site clear."));
        }
        if (CompletedVisits == 2)
        {
            UE_LOG(LogWeightStreaming, Display, TEXT("Weight streaming verification passed: two removals, site clear."));
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        Phase = EPhase::Start;
        break;
    }
}
#endif
