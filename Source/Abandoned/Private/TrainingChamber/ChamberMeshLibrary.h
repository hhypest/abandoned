#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "ChamberMeshLibrary.generated.h"

class UStaticMesh;

// Shared by consecutive chambers in one game instance. No world or actor is held alive.
UCLASS()
class UChamberMeshLibrary final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    UStaticMesh* LoadForChamber(UWorld& World, const TCHAR* Path);

private:
    void OnMapLoaded(UWorld* World);
    TWeakObjectPtr<UWorld> LastChamber;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> Meshes;
};
