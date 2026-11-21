#pragma once

#include "GameFramework/Actor.h"
#include "Infrastructure/Puzzles/TrainingDoorDriveComponent.h"
#include "Infrastructure/Puzzles/TrainingPlateCircuitComponent.h"
#include "ChamberAssembly.generated.h"

class APuzzleCube;
class UTrainingChamberLayout;

UCLASS()
class AChamberAssembly final : public AActor
{
    GENERATED_BODY()
public:
    AChamberAssembly();
    void Build(const UTrainingChamberLayout& Settings, UMaterialInterface* SurfaceMaterial, bool bCountHeldMass, FString& ErrorMessage);
    void BuildStreamed(const UTrainingChamberLayout& Template, UMaterialInterface* SurfaceMaterial, FString& ErrorMessage);
    void Retire();
    bool IsStreamedRoomReady() const;
    void UpdateRelayTraces();

    UPROPERTY(Transient)
    TArray<TObjectPtr<APuzzleCube>> Cubes;

    UPROPERTY(Transient)
    TObjectPtr<UTrainingPlateCircuitComponent> PlateCircuit;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> RelayTraces;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> RelayIdleMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> RelayPoweredMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Button;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> HintTerminal;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> ButtonVisual;

    UPROPERTY(Transient)
    TObjectPtr<const UTrainingChamberLayout> Layout;

    UPROPERTY(Transient)
    TObjectPtr<UTrainingDoorDriveComponent> DoorDrive;

private:
    // Hard references on the placed actor travel in the cooked chamber package.
    UPROPERTY(EditInstanceOnly, Category="Weight")
    TObjectPtr<UStaticMesh> PlateBaseMesh;
    UPROPERTY(EditInstanceOnly, Category="Weight")
    TObjectPtr<UStaticMesh> PlateTopMesh;
    UPROPERTY(EditInstanceOnly, Category="Weight")
    TObjectPtr<UStaticMesh> DoorLeafMesh;
    UPROPERTY(EditInstanceOnly, Category="Weight")
    TObjectPtr<UStaticMesh> DoorFrameMesh;
    UPROPERTY(EditInstanceOnly, Category="Weight")
    TObjectPtr<UStaticMesh> SwitchMesh;
    UPROPERTY(EditInstanceOnly, Category="Chamber")
    TObjectPtr<UStaticMesh> RelayMesh;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> RelayHousing;
    bool SpawnRelayTraces(UStaticMesh& Mesh, UMaterialInterface* Idle, UMaterialInterface* Powered);
};
