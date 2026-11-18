#pragma once

#include "Components/ActorComponent.h"
#include "Domain/Puzzles/PortalTransit.h"
#include "PortalTransitComponent.generated.h"

class UPrimitiveComponent;

// Tracks one uninterrupted trajectory. The caller clears it on pickup, respawn or scripted repositioning.
struct FPortalTravelerState
{
    FVector Previous = FVector::ZeroVector;
    int32 Approach = INDEX_NONE;
    bool bTracked = false;
    bool bUnsupportedShape = false;
};

#if WITH_DEV_AUTOMATION_TESTS
struct FPortalTransferStats
{
    int32 Transfers = 0;
    int32 Blocked = 0;
    FVector LastBefore = FVector::ZeroVector;
    FVector LastAfter = FVector::ZeroVector;
};
#endif

UCLASS()
class UPortalTransitComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    void Configure(const FTransform& Arrival, const FTransform& Departure, FVector2D ApertureHalfSize);
    Abandoned::Domain::Puzzles::PortalEvent Observe(UPrimitiveComponent& Body, FPortalTravelerState& State, bool bPowered);
    bool IsExitOccupied() const { return bExitOccupied; }
    void ClearExitStatus() { bExitOccupied = false; }
#if WITH_DEV_AUTOMATION_TESTS
    const FPortalTransferStats& GetStats() const { return Stats; }
#endif

private:
    bool Fits(const FVector& Local, const FVector& Extent) const;
    bool IntersectsAperture(const FVector& Local, const FVector& Extent) const;
    bool ExitClear(const UPrimitiveComponent& Body, const FTransform& Destination, const UPrimitiveComponent* Other) const;

    FTransform Poses[2];
    FVector2D HalfSize = FVector2D::ZeroVector;
    bool bExitOccupied = false;
#if WITH_DEV_AUTOMATION_TESTS
    void InspectTransfer(const FVector& Before, const FVector& After);
    FPortalTransferStats Stats;
#endif
};
