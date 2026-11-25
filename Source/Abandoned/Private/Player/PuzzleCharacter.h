#pragma once

#include "Player/AbandonedCharacter.h"
#include "PuzzleCharacter.generated.h"

class ADev20Actor;
class APuzzleCube;
class UBoxComponent;
class UCubePlacementComponent;
class UObjectCarryComponent;

UCLASS(Abstract)
class APuzzleCharacter : public AAbandonedCharacter
{
    GENERATED_BODY()

public:
    static constexpr double HoldDistanceCm = 165.0;
    APuzzleCharacter();
    UBoxComponent* GetHeldBody() const;
    APuzzleCube* GetHeldCube() const;
    ADev20Actor* GetHeldDev20() const;
    const TCHAR* GetPlacementPrompt() const;
    const APuzzleCube* GetSightedCube() const;
    const UCubePlacementComponent& GetPlacementPreview() const { return *Placement; }
    bool GrabBody(UBoxComponent& Body, float MassKg);
    void ReleaseHeldObject();
    bool TransitHeldBody(const FTransform& Through);

protected:
    bool SupportsCustomSuitFit() const override { return true; }
    void BindAdditionalInput(UEnhancedInputComponent& Input) override;
    void Tick(float DeltaSeconds) override;
    void UnPossessed() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Interact();
    void ResetChamber();
    UPrimitiveComponent* TraceInteractable() const;

private:
    FVector InteractionRayEnd() const;
    void ThrowHeldObject();

    UPROPERTY()
    TObjectPtr<UObjectCarryComponent> Grip;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCubePlacementComponent> Placement;
};
