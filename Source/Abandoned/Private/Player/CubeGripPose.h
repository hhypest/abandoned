#pragma once

#include "Engine/DataAsset.h"
#include "CubeGripPose.generated.h"

// Finger rotations in EQP-00 parent space, prepared locally from the licensed clip.
UCLASS(BlueprintType)
class UCubeGripPose final : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Grip")
    TMap<FName, FQuat> FingerRotations;
};
