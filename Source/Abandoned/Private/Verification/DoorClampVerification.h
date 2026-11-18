#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DoorClampVerification.generated.h"

class ATrainingChamberGameMode;
class ATrainingChamberCharacter;
class APuzzleCube;

namespace Abandoned::Verification
{
bool WantsDoorClampVerification();
}

UCLASS()
class UDoorClampVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UDoorClampVerification();
    static void Attach(ATrainingChamberGameMode& Chamber);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
    enum class EPhase
    {
        PowerOn, Open, Clamp, Hold, ReleaseBlockedInput, CheckBlockedPickup,
        Release, ReleasePickupInput, CheckPickup, Reopen, ClampForReset,
        ReleaseResetInput, CheckReset, OpenForPlayer, CrushPlayer
    };
    void Verify(ATrainingChamberGameMode& Chamber, ATrainingChamberCharacter& Player, float DeltaSeconds);
    void Fail(const TCHAR* Reason);
    void Step(EPhase Next);
    EPhase Phase = EPhase::PowerOn;
    double PhaseSeconds = 0.0;
    double TotalSeconds = 0.0;
    TWeakObjectPtr<APuzzleCube> Cubes[3];
    FVector ClampedObjectPosition = FVector::ZeroVector;
    FVector ObstructedDoorPosition = FVector::ZeroVector;
#endif
};
