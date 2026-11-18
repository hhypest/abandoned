#pragma once

#include "Components/ActorComponent.h"
#include "Domain/Puzzles/ChamberCircuit.h"
#include "Domain/Puzzles/PlateBalance.h"
#include "TrainingPlateCircuitComponent.generated.h"

class ADev20Actor;
class APuzzleCharacter;
class APuzzleCube;
class UBoxComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UTrainingChamberLayout;

USTRUCT()
struct FTrainingPressurePlate
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> Body;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Visual;

    UPROPERTY(Transient)
    TObjectPtr<UPointLightComponent> Indicator;

    bool bPressed = false;
};

UCLASS()
class UTrainingPlateCircuitComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    void Configure(const UTrainingChamberLayout& Settings, bool bCountCarriedMass);
    void AddPlate(UBoxComponent& Body, UStaticMeshComponent& Visual, UPointLightComponent* Indicator);
    void Observe(const APuzzleCharacter& Player, const TArray<TObjectPtr<APuzzleCube>>& Cubes,
        const ADev20Actor* Dev20, float DeltaSeconds);
    void PressButton() { Circuit.PressButton(); }
    void Reset();

    bool IsExitEnabled() const;
    const Abandoned::Domain::Puzzles::CircuitState& GetState() const { return Circuit.GetState(); }
    const TArray<FTrainingPressurePlate>& GetPlates() const { return Plates; }
    UBoxComponent* GetPlate(int32 Index) const { return Plates.IsValidIndex(Index) ? Plates[Index].Body.Get() : nullptr; }
    bool IsPressed(int32 Index) const { return Plates.IsValidIndex(Index) && Plates[Index].bPressed; }
    const TArray<float>& GetLoads() const { return PlateLoads; }
    float GetMeasuredMass(int32 Index) const { return PlateLoads.IsValidIndex(Index) ? PlateLoads[Index] : 0.0f; }

private:
    void SetPlatePressed(int32 Index, bool bPressed);

    UPROPERTY(Transient)
    TObjectPtr<const UTrainingChamberLayout> Layout;

    UPROPERTY(Transient)
    TArray<FTrainingPressurePlate> Plates;

    TArray<float> PlateLoads;
    Abandoned::Domain::Puzzles::ChamberCircuit Circuit;
    Abandoned::Domain::Puzzles::PlateBalance Balance;
    bool bIncludeHeldMass = false;
};
