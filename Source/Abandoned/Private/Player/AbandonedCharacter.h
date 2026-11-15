#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "AbandonedCharacter.generated.h"

class UCameraComponent;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;

UCLASS()
class AAbandonedCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AAbandonedCharacter();

protected:
    void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    void UnPossessed() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

    virtual void BindAdditionalInput(UEnhancedInputComponent&) {}
    void SetMovementLoad(float MassKg);
    UInputAction* MapAction(const FKey& Key, EInputActionValueType Type = EInputActionValueType::Boolean);
    void Move(FVector LocalDirection);
    void SetSprint(bool bHeld);
    void SetSlow(bool bHeld);
    void PlaceAt(const FTransform& Transform);

    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;

private:
    void Look(const FInputActionValue& Value);
    void UpdateSpeed();
    float LoadSpeedScale = 1.0f;
    void StartCrouch();
    void StopCrouch();
    void RemoveInputMapping();

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> InputMapping;

    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
    bool bSprintHeld = false;
    bool bSlowHeld = false;
};
