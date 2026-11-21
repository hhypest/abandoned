#pragma once

#include "Application/Puzzles/OpticsCourse.h"
#include "Components/ActorComponent.h"
#include "OpticalCircuitComponent.generated.h"

class UBoxComponent;
class UOpticalReflectorComponent;
class UStaticMeshComponent;

USTRUCT()
struct FOpticalLane
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UBoxComponent> Emitter;

    UPROPERTY()
    TObjectPtr<UBoxComponent> Receiver;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> EmitterHousing;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> EmitterLamp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ReceiverLamp;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> PowerLine;

    FVector Direction = FVector::RightVector;
};

UCLASS()
class UOpticalCircuitComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    static constexpr double BeamReachCm = 2400.0;
    static constexpr ECollisionChannel BeamChannel = ECC_GameTraceChannel1;

    bool Build(const FOpticalLane& Direct, const FOpticalLane& Reflected, UStaticMesh& Mesh,
        UStaticMeshComponent& SupplyLamp, UMaterialInterface& BeamLight, UMaterialInterface& PowerLight, UMaterialInterface& Off);
    void Reset();
    void PressSupplyButton() { Course.PressSupplyButton(); }
    Abandoned::Application::Puzzles::OpticsDoors Update(float Delta, const UOpticalReflectorComponent& Reflector);
    UBoxComponent* AimableEmitter() const { return Reflection.Emitter.Get(); }
    void AimEmitter(FVector Point);
    void AimReflector(UOpticalReflectorComponent& Reflector, FVector Point) const;
    bool HasSupply() const { return Course.GetPower().DirectEmitter; }
    bool HasReflection() const { return bReflectedSignal; }

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class UOpticsVerification;
    friend class UPulseDoorVerification;
#endif
    bool TraceLane(const FOpticalLane& Lane, bool bPowered, const UOpticalReflectorComponent* Reflector, int32& BeamIndex);
    void DrawBeam(FVector Start, FVector End, int32& BeamIndex);

    UPROPERTY()
    FOpticalLane Interruption;

    UPROPERTY()
    FOpticalLane Reflection;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Beams;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ButtonLamp;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Cyan;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Amber;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Inactive;

    Abandoned::Application::Puzzles::OpticsCourse Course;
    bool bDirectSignal = false;
    bool bReflectedSignal = false;
};
