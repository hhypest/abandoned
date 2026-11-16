#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"

class UBoxComponent;
class UPrimitiveComponent;
class UTextRenderComponent;

namespace Abandoned::Infrastructure::Puzzles
{
bool IsDoorClamped(const UPrimitiveComponent* Body);

struct DoorSweep
{
    FHitResult Hit;
    bool Closing = false;
};

class SlidingDoorClamp
{
public:
    void AddSigns(UBoxComponent& Door);
    DoorSweep Move(UBoxComponent& Door, double ClosedZ, double TravelCm, double StepCm, bool Opening);
    bool Capture(UBoxComponent& Body);
    void Release();
    bool HasObject() const { return Clamped.IsValid(); }
    bool Holds(const UBoxComponent* Body) const { return Body && Clamped.Get() == Body; }

private:
    void ShowSigns(bool Visible);

    TWeakObjectPtr<UBoxComponent> Clamped;
    TArray<TWeakObjectPtr<UTextRenderComponent>> Signs;
};
}
