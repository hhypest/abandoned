#include "TrainingChamber/ChamberAssembly.h"
#include "TrainingChamber/TrainingChamberLayout.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "World/Blockout.h"

bool AChamberAssembly::SpawnRelayTraces(UStaticMesh& Mesh, UMaterialInterface* Idle, UMaterialInterface* Powered)
{
    if (!Idle || !Powered)
    {
        return false;
    }
    RelayIdleMaterial = Idle;
    RelayPoweredMaterial = Powered;
    RelayTraces.Reserve(Layout->RelayTraces.Num());
    for (const FTrainingRelayTrace& Trace : Layout->RelayTraces)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(*GetWorld(), Mesh, Trace.Block.Center, Trace.Block.Size,
            EComponentMobility::Movable, TEXT("NoCollision"), this);
        if (!IsValid(Parts.Visual))
        {
            return false;
        }
        RelayTraces.Add(Parts.Visual);
        Parts.Visual->SetMaterial(0, Idle);
    }
    return true;
}

void AChamberAssembly::UpdateRelayTraces()
{
    const auto& State = PlateCircuit->GetState();
    const bool Powered[] = {State.PlatePressed, State.AlternatePlatePressed, State.ExitEnabled};
    for (int32 Index = 0; Index < RelayTraces.Num(); ++Index)
    {
        UStaticMeshComponent* Trace = RelayTraces[Index];
        if (IsValid(Trace))
        {
            const auto Signal = static_cast<uint8>(Layout->RelayTraces[Index].Signal);
            UMaterialInterface* Material = Powered[Signal] ? RelayPoweredMaterial.Get() : RelayIdleMaterial.Get();
            if (Trace->GetMaterial(0) != Material)
            {
                Trace->SetMaterial(0, Material);
            }
        }
    }
}
