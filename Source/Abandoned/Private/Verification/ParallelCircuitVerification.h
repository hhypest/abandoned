#pragma once

#include "Components/ActorComponent.h"
#include "Verification/VerificationKeyPress.h"
#include "ParallelCircuitVerification.generated.h"

class APuzzleCube;
class ATrainingChamberGameMode;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS()
class UParallelCircuitVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UParallelCircuitVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static bool IsRequested();
    static void Attach(ATrainingChamberGameMode& Chamber, const TArray<TObjectPtr<APuzzleCube>>& Cubes,
        const TArray<TObjectPtr<UStaticMeshComponent>>& Traces, UMaterialInterface* Idle, UMaterialInterface* Powered);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase { Initial, UnarmedA, ArmedA, Both, OnlyB, RemoveB, Unloaded, Overload, AfterReset };
    bool SignalsMatch(const ATrainingChamberGameMode& Chamber, bool A, bool B, bool Drive) const;
    void Fail(const TCHAR* Reason);
    EPhase Phase = EPhase::Initial;
    double PhaseSeconds = 0.0;
    double Elapsed = 0.0;
    TWeakObjectPtr<APuzzleCube> Ballast[2];
    TArray<TWeakObjectPtr<UStaticMeshComponent>> RelayTraces;
    TWeakObjectPtr<UMaterialInterface> IdleMaterial;
    TWeakObjectPtr<UMaterialInterface> PoweredMaterial;
    FVerificationKeyPress Input;
#endif
};
