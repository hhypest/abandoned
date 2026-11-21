#include "Puzzles/PortalLab.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Verification/PortalLabVerification.h"
#include "Verification/PortalDev20Verification.h"
#endif
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Expedition/Dev20Actor.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Infrastructure/Puzzles/PortalRoomViewComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/AbandonedTestController.h"
#include "Puzzles/PortalLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Blockout.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogPortalLab, Log, All);

namespace
{
using Abandoned::Domain::Puzzles::PortalEvent;
constexpr double HalfWidth = 118.0;
constexpr double HalfHeight = 145.0;
}

APortalLabGameMode::APortalLabGameMode()
{
    Transit = CreateDefaultSubobject<UPortalTransitComponent>(TEXT("PortalTransit"));
    DefaultPawnClass = APortalLabCharacter::StaticClass();
    PlayerControllerClass = AAbandonedTestController::StaticClass();
    HUDClass = APortalLabHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(
        TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface"));
    BaseMaterial = Surface.Object;
}

void APortalLabGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
    Super::InitGame(Map, Options, Error);
    if (!Error.IsEmpty())
    {
        return;
    }
    using namespace Abandoned::Infrastructure::Expedition;
    const auto Destination = ReadWorldDestination(Options, EntryWorld::Portal, Error);
    if (!Destination)
    {
        return;
    }
    SetActorTickEnabled(false);
    Layout = GetDefault<UPortalLabLayout>();
    const auto* Dev20Route = std::get_if<Dev20Destination>(&*Destination);
    bDev20Run = Dev20Route != nullptr;
    bDev20Recovered = Dev20Route && Dev20Route->Entry == Dev20Entry::Recovery;
    CubeMesh = Abandoned::Blockout::LoadCube();
    const auto MouthValid = [](const FPortalMouthLayout& Mouth)
    {
        return !Mouth.Center.ContainsNaN() && !Mouth.Console.ContainsNaN() && FMath::IsFinite(Mouth.Yaw);
    };
    if (!CubeMesh || !BaseMaterial || Layout->Room.IsEmpty() ||
        !MouthValid(Layout->Arrival) || !MouthValid(Layout->Departure) || Layout->Spawn.ContainsNaN() ||
        Layout->SmallCube.ContainsNaN() || Layout->LargeCube.ContainsNaN() ||
        (bDev20Run && (Layout->Dev20Station.ContainsNaN() || !Layout->Dev20Exit.IsValid())) ||
        !FMath::IsNearlyEqual(Layout->Arrival.Center.Z, Layout->Departure.Center.Z) ||
        FVector::DistSquared(Layout->Arrival.Center, Layout->Departure.Center) < FMath::Square(500.0))
    {
        Error = TEXT("Invalid portal lab layout or missing assets.");
        return;
    }

    UWorld& World = *GetWorld();
    Abandoned::Blockout::ApplyGravity(World);
    const auto Finishes = Abandoned::Scene::MakeFinishes(*this, BaseMaterial, Layout->Finishes);
    Lit = Finishes.FindRef(TEXT("Amber"));
    Unlit = Finishes.FindRef(TEXT("Unpowered"));
    if (!Lit || !Unlit || !Finishes.Contains(TEXT("Frame")))
    {
        Error = TEXT("Missing portal finishes.");
        return;
    }
    const auto Spawn = [&](const FSceneBlock& Block, EComponentMobility::Type Mobility = EComponentMobility::Static)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *CubeMesh, Block.Center, Block.Size, Mobility,
            Block.bCollision ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
        return Parts;
    };
    for (const FSceneBlock& Block : Layout->Room)
    {
        if (!Block.IsValid() || !Finishes.Contains(Block.Finish) || !Spawn(Block).Collision)
        {
            Error = TEXT("Invalid portal lab room block.");
            return;
        }
    }
    for (const FSceneLight& Light : Layout->Lights)
    {
        if (!Light.IsValid() || !Abandoned::Scene::AddLight(World, Light))
        {
            Error = TEXT("Invalid portal lab light.");
            return;
        }
    }

    for (const auto* Data : {&Layout->Arrival, &Layout->Departure})
    {
        FPortalMouth& Mouth = Mouths.AddDefaulted_GetRef();
        const FRotator Rotation(0.0, Data->Yaw, 0.0);
        Mouth.Pose = FTransform(Rotation, Data->Center);
        const auto Part = [&](FVector Center, FVector Size, FName Finish, bool bCollision)
        {
            const auto Parts = Spawn({Mouth.Pose.TransformPosition(Center), Size, bCollision, Finish},
                EComponentMobility::Movable);
            if (Parts.Collision)
            {
                Parts.Collision->SetWorldRotation(Rotation);
            }
            return Parts;
        };
        Part(FVector(0, -HalfWidth - 10, 0), FVector(42, 20, HalfHeight * 2), TEXT("Frame"), true);
        Part(FVector(0, HalfWidth + 10, 0), FVector(42, 20, HalfHeight * 2), TEXT("Frame"), true);
        Part(FVector(0, 0, HalfHeight + 10), FVector(42, HalfWidth * 2 + 40, 20), TEXT("Frame"), true);
        Mouth.PowerBar = Part(FVector(24, 0, HalfHeight + 9), FVector(6, 148, 7), TEXT("Unpowered"), false).Visual;
        Mouth.Console = Spawn({Data->Console, FVector(35, 58, 62), true, TEXT("Frame")}).Collision;
        Mouth.Shutter = Part(FVector::ZeroVector, FVector(10, 2 * HalfWidth, 2 * HalfHeight),
            TEXT("Unpowered"), true).Collision;
        if (!Mouth.PowerBar || !Mouth.Console || !Mouth.Shutter)
        {
            Error = TEXT("Could not assemble portal frame.");
            return;
        }
    }

    const FVector2D ApertureHalfSize(HalfWidth, HalfHeight);
    Transit->Configure(Mouths[0].Pose, Mouths[1].Pose, ApertureHalfSize);
    RoomView = UPortalRoomViewComponent::Create(World, Mouths[0].Pose, Mouths[1].Pose, ApertureHalfSize);
    if (!RoomView)
    {
        Error = TEXT("Could not initialize the portal room view.");
        return;
    }

    Cubes.SetNum(2);
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        Cubes[Index] = SpawnCube(Index);
        if (!Cubes[Index])
        {
            Error = TEXT("Could not spawn the portal lab cubes.");
            return;
        }
    }
    if (bDev20Run)
    {
        auto* Session = Abandoned::Infrastructure::Expedition::Dev20Session(this);
        if (!Session || !Spawn({Layout->Dev20Station - FVector(0, 0, 32), FVector(92, 71, 26),
            true, TEXT("Frame")}).Collision)
        {
            Error = TEXT("Could not initialize the DEV-20 recovery station.");
            return;
        }
        Session->Transfer.RecoverAtStation();
        RecoverDev20();
        if (!IsValid(Dev20))
        {
            Error = TEXT("Could not spawn DEV-20.");
            return;
        }
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bDev20Run)
    {
        UPortalDev20Verification::AttachIfRequested(*this, *Layout, *Dev20, *Mouths[0].Console, *Cubes[1], bDev20Recovered);
    }
    else
    {
        UPortalLabVerification::AttachIfRequested(*this);
    }
#endif
    SetActorTickEnabled(true);
}

EPuzzleCubeType APortalLabGameMode::CubeType(int32 Index) const
{
    return Index == 0 ? EPuzzleCubeType::Small : EPuzzleCubeType::Large;
}

APuzzleCube* APortalLabGameMode::SpawnCube(int32 Index)
{
    APuzzleCube* Cube = GetWorld()->SpawnActor<APuzzleCube>();
    if (Cube)
    {
        if (!Cube->Initialize(CubeType(Index))) { Cube->Destroy(); return nullptr; }
        Cube->ResetPosition(Index == 0 ? Layout->SmallCube : Layout->LargeCube);
    }
    return Cube;
}

void APortalLabGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Controller)
{
    if (Controller && Layout && IsActorTickEnabled())
    {
        RestartPlayerAtTransform(Controller, FTransform(FRotator::ZeroRotator, Layout->Spawn));
    }
}

void APortalLabGameMode::ResetLab(APortalLabCharacter& Player, bool bPowerFailure)
{
    Player.ReleaseHeldObject();
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
    Supply.Reset();
    PlayerState = {};
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        CubeStates[Index] = {};
        if (IsValid(Cubes[Index]))
        {
            Cubes[Index]->ResetPosition(Index == 0 ? Layout->SmallCube : Layout->LargeCube);
        }
        else
        {
            Cubes[Index] = SpawnCube(Index);
        }
    }
    bCrossedWithItem = false;
    bInterrupted = bPowerFailure;
    Transit->ClearExitStatus();
    if (bDev20Run)
    {
        RecoverDev20();
        bDev20Recovered = bPowerFailure;
    }
    UpdateMouths(nullptr);
}

void APortalLabGameMode::RecoverDev20()
{
    if (IsValid(Dev20))
    {
        Dev20->RestoreAt(Layout->Dev20Station);
    }
    else
    {
        Dev20 = GetWorld()->SpawnActor<ADev20Actor>(Layout->Dev20Station, FRotator::ZeroRotator);
    }
    Dev20State = {};
    bDev20Crossed = false;
}

void APortalLabGameMode::ToggleSupply()
{
    Supply.Observe(true, 0.0f);
    Supply.Observe(false, 0.0f);
    bInterrupted = false;
    Transit->ClearExitStatus();
}

bool APortalLabGameMode::UseConsole(const UPrimitiveComponent& Target)
{
    for (const FPortalMouth& Mouth : Mouths)
    {
        if (&Target == Mouth.Console)
        {
            ToggleSupply();
            return true;
        }
    }
    return false;
}

void APortalLabGameMode::UpdateMouths(const APlayerController* Viewer)
{
    const bool bPowered = Supply.IsPowered();
    for (FPortalMouth& Mouth : Mouths)
    {
        Mouth.Shutter->SetCollisionEnabled(bPowered ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        Mouth.Shutter->SetVisibility(!bPowered, true);
        Mouth.PowerBar->SetMaterial(0, bPowered ? Lit : Unlit);
    }
    RoomView->UpdateView(bPowered ? Viewer : nullptr);
}

void APortalLabGameMode::Tick(float Delta)
{
    Super::Tick(Delta);
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    APortalLabCharacter* Player = Controller ? Cast<APortalLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || bDev20Departed)
    {
        return;
    }
    const PortalEvent Crossing = Transit->Observe(*Player->GetCapsuleComponent(), PlayerState, Supply.IsPowered());
    if (Crossing == PortalEvent::Interrupted)
    {
        APuzzleCube* Lost = Player->GetHeldCube();
        Player->ReleaseHeldObject();
        if (Lost)
        {
            Lost->Destroy();
        }
        ResetLab(*Player, true);
        UE_LOG(LogPortalLab, Display, TEXT("Transit interrupted. Restored the checkpoint."));
    }
    else if (Crossing == PortalEvent::Transfer)
    {
        bCrossedWithItem |= Player->GetHeldBody() != nullptr;
        bDev20Crossed |= IsValid(Dev20) && Player->GetHeldDev20() == Dev20;
    }
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        if (APuzzleCube* Cube = Cubes[Index]; IsValid(Cube) && Cube != Player->GetHeldCube() &&
            Cube->GetBody()->IsSimulatingPhysics())
        {
            if (Transit->Observe(*Cube->GetBody(), CubeStates[Index], Supply.IsPowered()) == PortalEvent::Interrupted)
            {
                Cube->Destroy();
                Cubes[Index] = SpawnCube(Index);
                CubeStates[Index] = {};
            }
        }
        else
        {
            CubeStates[Index] = {};
        }
    }
    if (bDev20Run)
    {
        if (!IsValid(Dev20) || Dev20->GetActorLocation().Z < -240.0)
        {
            if (IsValid(Dev20) && Player->GetHeldDev20() == Dev20)
            {
                Player->ReleaseHeldObject();
            }
            RecoverDev20();
            bDev20Recovered = true;
        }
        if (IsValid(Dev20) && Player->GetHeldDev20() != Dev20)
        {
            if (Transit->Observe(*Dev20->GetBody(), Dev20State, Supply.IsPowered()) == PortalEvent::Interrupted)
            {
                RecoverDev20();
                bDev20Recovered = true;
            }
        }
        else
        {
            Dev20State = {};
        }
        const FSceneBlock& Exit = Layout->Dev20Exit;
        if (bDev20Crossed && IsValid(Dev20) && Player->GetHeldDev20() == Dev20 &&
            FBox::BuildAABB(Exit.Center, Exit.Size * 0.5).IsInside(Player->GetActorLocation()))
        {
            bDev20Departed = Abandoned::Infrastructure::Expedition::TravelWithDev20(
                this, Abandoned::Domain::Expedition::Dev20Stage::Station, true);
            if (bDev20Departed)
            {
                UE_LOG(LogPortalLab, Display, TEXT("DEV-20 departed the portal bay in the player's grip."));
                return;
            }
        }
    }
    UpdateMouths(Controller);
}

const TCHAR* APortalLabGameMode::GetMessage() const
{
    if (bDev20Run)
    {
        if (!Supply.IsPowered())
        {
            return bDev20Recovered ? TEXT("DEV-20 returned to its cradle. Power the pair [E].")
                : TEXT("DEV-20 transport. Power the pair [E], then take DEV-20 from its cradle.");
        }
        if (Transit->IsExitOccupied())
        {
            return TEXT("Exit blocked. Clear the destination before crossing.");
        }
        return bDev20Crossed ? TEXT("Carry DEV-20 to the amber panel at the end of this bay.")
            : TEXT("Carry DEV-20 (34 kg) through the active frame.");
    }
    if (bInterrupted)
    {
        return TEXT("Power lost during transit. Checkpoint restored.");
    }
    if (!Supply.IsPowered())
    {
        return TEXT("Use the console [E] to power the portal pair.");
    }
    if (Transit->IsExitOccupied())
    {
        return TEXT("Exit blocked. Clear the destination before crossing.");
    }
    return bCrossedWithItem ? TEXT("Transit complete. The carried item arrived with you.")
        : TEXT("Pick up a cube [E] and walk or jump through the active frame.");
}

#if WITH_DEV_AUTOMATION_TESTS
FPortalTransitObservation APortalLabGameMode::GetTransitObservation() const
{
    const FPortalTransferStats& Stats = Transit->GetStats();
    return {Supply.IsPowered(), PlayerState.Approach == 0, bInterrupted, bDev20Crossed,
        Stats.Transfers, Stats.Blocked, Stats.LastBefore, Stats.LastAfter};
}

#endif

void APortalLabGameMode::ResetPuzzle(APuzzleCharacter& Player)
{
    if (auto* LabPlayer = Cast<APortalLabCharacter>(&Player))
    {
        ResetLab(*LabPlayer);
    }
}

bool APortalLabGameMode::UseDevice(APuzzleCharacter&, UPrimitiveComponent& Target)
{
    return UseConsole(Target);
}

void APortalLabHUD::DrawHUD()
{
    Super::DrawHUD();
    if (const auto* Lab = GetWorld()->GetAuthGameMode<APortalLabGameMode>(); Canvas && Lab)
    {
        DrawText(TEXT("TRANSIT / POWERED PORTALS"), FLinearColor::White, 24, 24, nullptr, 1.1f);
        DrawText(Lab->GetMessage(), FLinearColor::White, 24, 54, nullptr, 1.0f);
        DrawText(TEXT("Reset [R]"), FLinearColor::White, 24, 80, nullptr, 0.9f);
        const float X = Canvas->ClipX * 0.5f;
        const float Y = Canvas->ClipY * 0.5f;
        DrawLine(X - 6, Y, X + 6, Y, FLinearColor::White, 1.0f);
        DrawLine(X, Y - 6, X, Y + 6, FLinearColor::White, 1.0f);
        if (const auto* Player = Cast<APortalLabCharacter>(GetOwningPawn()))
        {
            if (const TCHAR* Prompt = Player->GetPlacementPrompt())
            {
                DrawText(Prompt, FLinearColor::White, X + 18.0f, Y + 14.0f);
            }
        }
    }
}
