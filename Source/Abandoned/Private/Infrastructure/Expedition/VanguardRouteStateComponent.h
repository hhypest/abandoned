#pragma once

#include "Application/Expedition/PassageHazards.h"
#include "Application/Expedition/SuitTerminal.h"
#include "Application/Expedition/WorldRoute.h"
#include "Components/ActorComponent.h"
#include "VanguardRouteStateComponent.generated.h"

class UVanguardPassageLayout;

struct FVanguardTransition
{
    Abandoned::Application::Expedition::WorldDestination Destination;
    bool bDoseLimitReached = false;
};

UCLASS()
class UVanguardRouteStateComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    bool Configure(Abandoned::Application::Expedition::VanguardDestination Entry,
        const UVanguardPassageLayout* Passage, FString& Error);
    void EnterSegment();
    bool AtStation() const;
    bool IsTravelling() const { return bTravelling; }
    bool HasResupplied() const { return bResupplied; }
    bool HasClearedRadiation() const { return bClearedRadiation; }
    bool HasCheckpointWriteFailed() const { return bCheckpointWriteFailed; }
    bool HasMinorInjury() const { return Hazards.Recovery().IsActive(); }
    float GroundSpeedScale() const { return Hazards.Recovery().GroundSpeedScale(); }
    std::optional<double> SupportHeight() const { return Hazards.SupportHeight(); }
    void ResetMovement() { Hazards.ResetMovement(); }
    void RecordLanding(double SurfaceHeight);
    bool AdvanceMovement(std::optional<double> GroundHeight, float Seconds);
    std::optional<FVanguardTransition> AdvancePassage(const FVector& Position, float DeltaSeconds, bool bStationGateOpen);
    Abandoned::Application::Expedition::VanguardDestination NextStation() const;
    std::optional<Abandoned::Application::Expedition::WorldDestination> RestartRadiationPassage();
    void Resupply(bool bAllowCampaignSave);
    float GetOxygenPercent() const;
    std::optional<Abandoned::Domain::Expedition::DosimeterReading> GetDosimeter() const;
    Abandoned::Application::Expedition::SuitTelemetry GetSuitTelemetry() const;
#if WITH_DEV_AUTOMATION_TESTS
    void ForgetSupportForReposition() { Hazards.ForgetSupport(); }
#endif

private:
    Abandoned::Domain::Expedition::RadiationPosition ClassifyPosition(const FVector& Position) const;
    Abandoned::Application::Expedition::VanguardDestination Destination;
    std::optional<Abandoned::Application::Expedition::WorldDestination> RecoveryDestination;
    Abandoned::Application::Expedition::PassageHazards Hazards;
    FBox ExitZone{ForceInit};
    FBox ExposureZone{ForceInit};
    TArray<FBox> Shelters;
    bool bTravelling = false;
    bool bResupplied = false;
    bool bClearedRadiation = false;
    bool bCheckpointWriteFailed = false;
};
