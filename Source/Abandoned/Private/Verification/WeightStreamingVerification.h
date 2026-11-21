#pragma once

#include "Components/ActorComponent.h"
#include "WeightStreamingVerification.generated.h"

class ULevelStreamingDynamic;
class APuzzleCube;
class AChamberAssembly;
class UBoxComponent;

UCLASS()
class UWeightStreamingVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UWeightStreamingVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static void AttachIfRequested(AActor& Stairs);
    void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
    enum class EPhase { Start, Hidden, Visible, Land, Return, Removal };
    EPhase Phase = EPhase::Start;
    TWeakObjectPtr<ULevelStreamingDynamic> Instance;
    TArray<TWeakObjectPtr<APuzzleCube>> Ballast;
    TWeakObjectPtr<AChamberAssembly> Assembly;
    TArray<TWeakObjectPtr<UBoxComponent>> DeviceBodies;
    FTransform Home;
    int32 CompletedVisits = 0;
    bool bCancelledFirstRequest = false;
#endif
};
