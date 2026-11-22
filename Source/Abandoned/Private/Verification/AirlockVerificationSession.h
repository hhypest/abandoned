#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "AirlockVerificationSession.generated.h"

class ATrainingChamberCharacter;
class AVanguardCharacter;

// Observations span OpenLevel but belong to one game instance, including in PIE.
UCLASS()
class UAirlockVerificationSession final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    bool ShouldCreateSubsystem(UObject* Outer) const override;
#if WITH_DEV_AUTOMATION_TESTS
    static UAirlockVerificationSession* Find(const AActor& Context);

    struct FChamber
    {
        int32 Visits = 0;
        TWeakObjectPtr<ATrainingChamberCharacter> Pawn;
    } Chamber;

    struct FPassage
    {
        int32 Visits = 0;
        TWeakObjectPtr<UWorld> SourceWorld;
        TWeakObjectPtr<APlayerController> SourceController;
        TWeakObjectPtr<ATrainingChamberCharacter> SourcePawn;
        TWeakObjectPtr<AVanguardCharacter> Suit;
    } Passage;

    struct FStation
    {
        TWeakObjectPtr<UWorld> PassageWorld;
        TWeakObjectPtr<ULevel> PassageLevel;
        TWeakObjectPtr<AController> Controller;
        float LastOxygen = 0;
    } Station;
#endif
};
