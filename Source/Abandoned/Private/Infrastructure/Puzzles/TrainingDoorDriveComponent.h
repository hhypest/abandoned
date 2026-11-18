#pragma once

#include "Components/ActorComponent.h"
#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"
#include "TrainingDoorDriveComponent.generated.h"

class ADev20Actor;
class APuzzleCharacter;
class UBoxComponent;

enum class ETrainingDoorResult { Continue, ResetPlayer };

UCLASS()
class UTrainingDoorDriveComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    void Bind(UBoxComponent& Body, FVector ClosedLocation);
    ETrainingDoorResult Move(APuzzleCharacter& Player, const ADev20Actor* Dev20, bool bOpening, bool bPlayerAtRisk, float DeltaSeconds);
    UBoxComponent* GetDoor() const { return Door.Get(); }
    bool IsOpen() const;
    bool HasClampedObject() const { return Clamp.HasObject(); }
    void ReleaseIfHolding(const UBoxComponent* Body);
    void ReleaseClamp() { Clamp.Release(); }
    void ReturnToClosedPosition();
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    TWeakObjectPtr<UBoxComponent> Door;
    FVector Closed = FVector::ZeroVector;
    Abandoned::Infrastructure::Puzzles::SlidingDoorClamp Clamp;
};
