#pragma once

#include "World/Blockout.h"
#include "World/SceneLayout.h"

class UPointLightComponent;

namespace Abandoned::Scene
{
using FinishMap = TMap<FName, UMaterialInterface*>;

FinishMap MakeFinishes(UObject& Owner, UMaterialInterface* Base, TConstArrayView<FSceneFinish> Data);
void SetFinish(const Blockout::FBox& Parts, const FSceneBlock& Block, const FinishMap& Finishes);
UPointLightComponent* AddLight(UWorld& World, const FSceneLight& Data);
}
