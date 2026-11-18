#pragma once

#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "ObjectCarryComponent.generated.h"

class UBoxComponent;

UCLASS()
class UObjectCarryComponent final : public UPhysicsHandleComponent
{
    GENERATED_BODY()

public:
    UObjectCarryComponent();
    DECLARE_DELEGATE_OneParam(FLoadChanged, float);
    FLoadChanged LoadChanged;
    UBoxComponent* HeldBody() const;
    bool TryGrab(UBoxComponent& Body, float MassKg);
    bool Transit(const FTransform& Through);
    void Throw(const FVector& Velocity, const FVector& Impulse);
    void UpdateHold(const FVector& Origin, const FRotator& View, double HoldDistance, double Reach);
    void ReleaseComponent() override;

private:
    UFUNCTION()
    void OnHeldActorEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    TWeakObjectPtr<AActor> HeldActor;
    TOptional<ECollisionResponse> OriginalPawnResponse;
};
