#pragma once

#include "Application/Puzzles/OpticsCourse.h"
#include "Application/Puzzles/TimedPassage.h"
#include "Components/ActorComponent.h"
#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"
#include "PowerLabDoorsComponent.generated.h"

class APuzzleCharacter;
class UBoxComponent;
class UPowerLabLayout;
class UTextRenderComponent;

enum class EPowerLabRecovery { None, ResetLab, PulseRetry };

UCLASS()
class UPowerLabDoorsComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    bool Build(const UPowerLabLayout& Data, UBoxComponent& DirectDoor, UBoxComponent& ReflectedDoor, UStaticMesh& Mesh,
        UMaterialInterface& Steel, UMaterialInterface& Panel, UMaterialInterface& Screen,
        UMaterialInterface& PowerLight, UMaterialInterface& Off);
    bool UseSwitch(const UPrimitiveComponent& Target);
    EPowerLabRecovery Update(float Delta, const Abandoned::Application::Puzzles::OpticsDoors& Signals,
        APuzzleCharacter& Player, UBoxComponent* Cube, UBoxComponent* LooseMirror);
    void ReleaseIfHolding(const UBoxComponent* Body);
    void Reset();
    void EndPlay(const EEndPlayReason::Type Reason) override;
    bool IsInPulseBay(const FVector& Player) const;
    const TCHAR* GetPulseObjective(const FVector& Player) const;
    bool OpticalDoorClamped() const { return Interruption.Clamp.HasObject() || Reflection.Clamp.HasObject(); }
    float GetPulseSeconds() const;
    float GetPulseFraction() const;

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class UOpticsVerification;
    friend class UPulseDoorVerification;
#endif
    struct FDoor
    {
        void Bind(UBoxComponent& Leaf);
        void Reset();
        TWeakObjectPtr<UBoxComponent> Body;
        FVector Closed = FVector::ZeroVector;
        Abandoned::Infrastructure::Puzzles::SlidingDoorClamp Clamp;
    };

    static bool MoveDoor(FDoor& Drive, bool bOpen, float Delta, APuzzleCharacter& Player,
        UBoxComponent* Cube, UBoxComponent* LooseMirror);
    void UpdatePulseDisplay();

    FDoor Interruption;
    FDoor Reflection;
    FDoor TimedDoor;
    Abandoned::Application::Puzzles::TimedPassage Pulse{0.0f};
    int32 DisplayedPulseTenths = -1;

    UPROPERTY()
    TObjectPtr<const UPowerLabLayout> Layout;

    UPROPERTY()
    TObjectPtr<UBoxComponent> PulseButton;

    UPROPERTY()
    TObjectPtr<UBoxComponent> PulseReturn;

    UPROPERTY()
    TObjectPtr<UTextRenderComponent> PulseClock;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Amber;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Inactive;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> PulseIndicators;
};
