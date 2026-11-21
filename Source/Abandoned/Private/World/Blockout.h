#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;
class AActor;

namespace Abandoned::Blockout
{
struct FBox
{
    UBoxComponent* Collision = nullptr;
    UStaticMeshComponent* Visual = nullptr;
};

[[nodiscard]] UStaticMesh* LoadCube();
void ApplyGravity(UWorld& World);
FBox SpawnBox(UWorld& World, UStaticMesh& Cube, FVector Center, FVector Size,
    EComponentMobility::Type Mobility = EComponentMobility::Static, FName CollisionProfile = TEXT("BlockAll"), AActor* Owner = nullptr);
}
