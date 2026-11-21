#pragma once

#include "Components/ActorComponent.h"
#include "Domain/Expedition/EntryRoute.h"
#include "Verification/VerificationKeyPress.h"
#include "EntryChamberVerification.generated.h"

class ADev20Actor;
class APuzzleCube;
class ATrainingChamberCharacter;
class ATrainingChamberGameMode;

UCLASS()
class UEntryChamberVerification final : public UActorComponent
{
    GENERATED_BODY()

public:
    UEntryChamberVerification();
#if WITH_DEV_AUTOMATION_TESTS
    static bool IsRequested(bool bDev20Route);
    static bool UsesCheckpoint();
    static void Attach(ATrainingChamberGameMode& Chamber, Abandoned::Domain::Expedition::Segment Segment,
        const TArray<TObjectPtr<APuzzleCube>>& Ballast);
    void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EPhase
    {
        Start, DropUnit, MeasureUnit, AwaitRecovery, Arm, TakeFirst, AimFirst, PlaceFirst, CheckFirst,
        RunUp, JumpToStack, CrossLedge, TakeUpper, AimUpper, PlaceUpper, CheckUpper,
        WaitDoor, CheckReset, WalkToExit, RetrieveUnit, CarryToExit
    };
    void Check(ATrainingChamberGameMode& Chamber, ATrainingChamberCharacter& Player, APlayerController& Controller);
    void Advance(EPhase Next);
    void Fail(const TCHAR* Reason);

    Abandoned::Domain::Expedition::Segment Segment = Abandoned::Domain::Expedition::Segment::Stairs;
    EPhase Phase = EPhase::Start;
    float Elapsed = 0.0f;
    TArray<TWeakObjectPtr<APuzzleCube>> Cubes;
    TWeakObjectPtr<ADev20Actor> Unit;
    FVerificationKeyPress Input;
    int32 ExitPlateIndex = 0;
    int32 UpperIndex = 1;
    bool bAlternateWeight = false;
    bool bDev20 = false;
    bool bVerifiedWeightReset = false;
    bool bCaptured = false;
    bool bRevealedWeight = false;
#endif
};
