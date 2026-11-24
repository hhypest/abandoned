#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "AbandonedCharacter.generated.h"

class UCameraComponent;
class UFosTechBody;
class UPoseableMeshComponent;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Config=Game)
class AAbandonedCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AAbandonedCharacter();
    UPoseableMeshComponent* GetSuitBody() const;
    UStaticMeshComponent* GetWristCasing() const;

protected:
    void Tick(float DeltaSeconds) override;
    void PostInitializeComponents() override;
    void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    void UnPossessed() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
    void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

    virtual void BindAdditionalInput(UEnhancedInputComponent&) {}
    void SetMovementLoad(float MassKg);
    void SetGroundSpeedScale(float Scale);
    UInputAction* MapAction(const FKey& Key, EInputActionValueType Type = EInputActionValueType::Boolean);
    virtual void Move(FVector LocalDirection);
    void SetSprint(bool bHeld);
    void SetSlow(bool bHeld);
    void PlaceAt(const FTransform& Transform);

    UPROPERTY()
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<UFosTechBody> SuitBody;

    UPROPERTY(VisibleDefaultsOnly, Category="Suit")
    TObjectPtr<UStaticMeshComponent> WristCasing;

private:
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> ExpeditionSidearm;
    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> SidearmAsset;
    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> WristCasingAsset;
    void Look(const FInputActionValue& Value);
    void UpdateSpeed();
    float LoadSpeedScale = 1.0f;
    float GroundSpeedScale = 1.0f;
    void StartCrouch();
    void StopCrouch();
    void RemoveInputMapping();

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> InputMapping;

    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystem;
    bool bSprintHeld = false;
    bool bSlowHeld = false;
};
