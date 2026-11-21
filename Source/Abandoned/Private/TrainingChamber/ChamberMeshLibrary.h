#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "ChamberMeshLibrary.generated.h"

class UStaticMesh;
struct FStreamableHandle;

// Shared by consecutive chambers in one game instance. No world or actor is held alive.
UCLASS()
class UChamberMeshLibrary final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    UStaticMesh* LoadForChamber(UWorld& World, const TCHAR* Path);
    static constexpr const TCHAR* WeightShellPath = TEXT("/Game/Abandoned/Collision/SM_WeightShell.SM_WeightShell");
    void PrepareWeightShell();

private:
    void OnMapLoaded(UWorld* World);
    void ReleaseWeightShellRequest();
    TSharedPtr<FStreamableHandle> WeightShellRequest;
    TWeakObjectPtr<UWorld> LastChamber;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMesh>> Meshes;
};
