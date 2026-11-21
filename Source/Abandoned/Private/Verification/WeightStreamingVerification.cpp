#include "Verification/WeightStreamingVerification.h"

UWeightStreamingVerification::UWeightStreamingVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/PrimitiveComponent.h"
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
