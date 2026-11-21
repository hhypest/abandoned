#include "World/SceneAssembly.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace Abandoned::Scene
{
FinishMap MakeFinishes(UObject& Owner, UMaterialInterface* Base, TConstArrayView<FSceneFinish> Data)
{
    FinishMap Finishes;
    for (const FSceneFinish& Finish : Data)
    {
        UMaterialInterface* Material = Finish.Material.IsNull() ? nullptr : Finish.Material.LoadSynchronous();
        if (!Material && Base)
        {
            UMaterialInstanceDynamic* Fallback = UMaterialInstanceDynamic::Create(Base, &Owner);
            Fallback->SetVectorParameterValue(TEXT("DiffuseColor"), Finish.Tint);
            Fallback->SetScalarParameterValue(TEXT("Roughness"), Finish.Roughness);
            Fallback->SetScalarParameterValue(TEXT("Metallic"), Finish.Metallic);
            Fallback->SetScalarParameterValue(TEXT("Glow"), Finish.Glow);
            Material = Fallback;
        }
        if (Material)
        {
            Finishes.Add(Finish.Name, Material);
        }
    }
    return Finishes;
}

void SetFinish(const Blockout::FBox& Parts, const FSceneBlock& Block, const FinishMap& Finishes)
{
    if (UMaterialInterface* const* Material = Finishes.Find(Block.Finish); Material && Parts.Visual)
    {
        Parts.Visual->SetMaterial(0, *Material);
    }
}

UPointLightComponent* AddLight(UWorld& World, const FSceneLight& Data, AActor* Owner)
{
    FActorSpawnParameters Params;
    Params.Owner = Owner;
    APointLight* Lamp = World.SpawnActor<APointLight>(Data.Position, FRotator::ZeroRotator, Params);
    if (!Lamp)
    {
        return nullptr;
    }
    UPointLightComponent* Light = CastChecked<UPointLightComponent>(Lamp->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetCastShadows(false);
    Light->SetAttenuationRadius(Data.Radius);
    Light->SetIntensity(Data.Intensity);
    Light->SetLightColor(Data.Color);
    return Light;
}
}
