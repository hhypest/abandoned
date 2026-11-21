#include "Blockout.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MovementProfile.h"

namespace Abandoned::Blockout
{
UStaticMesh* LoadCube()
{
    return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
}

void ApplyGravity(UWorld& World)
{
    AWorldSettings* Settings = World.GetWorldSettings();
    Settings->bGlobalGravitySet = true;
    Settings->GlobalGravityZ = MovementProfile::GravityZ;
    Settings->bWorldGravitySet = false;
}

FBox SpawnBox(UWorld& World, UStaticMesh& Cube, FVector Center, FVector Size,
    EComponentMobility::Type Mobility, FName CollisionProfile, AActor* Owner)
{
    FActorSpawnParameters Params;
    Params.Owner = Owner;
    AActor* Actor = World.SpawnActor<AActor>(Params);
    if (!ensure(Actor))
    {
        return {};
    }

    UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
    Actor->SetRootComponent(Box);
    Actor->AddInstanceComponent(Box);
    Box->InitBoxExtent(Size * 0.5);
    Box->SetWorldLocation(Center);
    Box->SetMobility(Mobility);
    Box->SetCollisionProfileName(CollisionProfile);
    Box->RegisterComponent();

    UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Actor);
    Actor->AddInstanceComponent(Visual);
    Visual->SetupAttachment(Box);
    Visual->SetMobility(Mobility);
    Visual->SetStaticMesh(&Cube);
    Visual->SetCollisionProfileName(TEXT("NoCollision"));
    Visual->SetRelativeScale3D(Size / 100.0);
    Visual->RegisterComponent();

    return {Box, Visual};
}
}
