#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/PackageName.h"
#include "World/Blockout.h"

bool AVanguardSegment::PollPassageAssets()
{
    if (!bRequestedAssets)
    {
        bRequestedAssets = true;
        TArray<FSoftObjectPath> Paths;
        const auto Add = [&Paths](const FSoftObjectPath& Path)
        {
            if (!Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName())) { Paths.AddUnique(Path); }
        };
        const auto* Passage = GetDefault<UVanguardPassageLayout>();
        for (const auto& Finish : Passage->Finishes) { Add(Finish.Material.ToSoftObjectPath()); }
        Add(Passage->LeafMesh.ToSoftObjectPath());
        Add(Passage->FrameMesh.ToSoftObjectPath());
        const auto* Suit = GetDefault<UVanguardMantleVisuals>();
        Add(Suit->WristSurface.ToSoftObjectPath());
        Add(GetDefault<UVanguardInjurySettings>()->BreathSound.ToSoftObjectPath());
        if (!Paths.IsEmpty()) { AssetRequest = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths); }
    }
    return !AssetRequest || AssetRequest->HasLoadCompleted();
}

#if WITH_EDITOR
bool AVanguardSegment::BakePassageRoom(bool bStation)
{
    UWorld* World = GetWorld();
    UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
    if (!World || World->WorldType != EWorldType::Editor || !Cube) { return false; }
    Modify();
    for (AStaticMeshActor* Block : PlacedRoom)
    {
        if (IsValid(Block)) { World->EditorDestroyActor(Block, true); }
    }
    PlacedRoom.Reset();
    const UVanguardRouteLayout* Settings = bStation ? static_cast<const UVanguardRouteLayout*>(GetDefault<UVanguardStationLayout>()) : GetDefault<UVanguardPassageLayout>();
    for (const auto& Block : Settings->Room)
    {
        FActorSpawnParameters Params;
        Params.OverrideLevel = GetLevel();
        auto* Actor = World->SpawnActor<AStaticMeshActor>(Block.Center, FRotator::ZeroRotator, Params);
        if (!Actor) { return false; }
        Actor->SetActorLabel(FString::Printf(TEXT("Room %03d %s"), PlacedRoom.Num(), *Block.Finish.ToString()));
        auto* Visual = Actor->GetStaticMeshComponent();
        Visual->SetStaticMesh(Cube);
        Visual->SetCollisionProfileName(Block.bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
        Actor->SetActorScale3D(Block.Size / 100.0);
        PlacedRoom.Add(Actor);
    }
    return !PlacedRoom.IsEmpty();
}
#endif

FVector AVanguardSegment::PlayerStart() const
{
    return Layout->Spawn;
}

bool AVanguardSegment::HasPassageSupport() const
{
    const auto Ready = [this](const UBoxComponent* Body)
    {
        return IsValid(Body) && Body->IsRegistered() && Body->IsQueryCollisionEnabled() && Body->GetOwner()->GetLevel() == GetLevel();
    };
    const auto* Passage = Cast<UVanguardPassageLayout>(Layout.Get());
    if (!Passage || !Ready(Door) || !Ready(StationGate) || Ledges.Num() != Passage->MantleLedges.Num() ||
        Ledges.ContainsByPredicate([&](const auto& Ledge) { return !Ready(Ledge); })) { return false; }
    FHitResult Floor;
    return GetWorld()->LineTraceSingleByChannel(Floor, Passage->Spawn, Passage->Spawn - FVector(0, 0, 150), ECC_Pawn) &&
        Floor.GetActor() && Floor.GetActor()->GetLevel() == GetLevel();
}
