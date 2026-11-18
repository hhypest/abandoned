#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"

#include "Engine/World.h"
#include "Expedition/VanguardRouteLayout.h"
#include "Expedition/VanguardSession.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"

DEFINE_LOG_CATEGORY_STATIC(LogVanguardState, Log, All);

namespace
{
using Abandoned::Application::Expedition::ChamberDestination;
using Abandoned::Application::Expedition::PassageOutcome;
using Abandoned::Application::Expedition::RouteEntry;
using Abandoned::Application::Expedition::VanguardStop;
using Abandoned::Application::Expedition::WorldDestination;
using Abandoned::Domain::Expedition::FallImpact;
using Abandoned::Domain::Expedition::RadiationPosition;

FBox BoundsOf(const FSceneBlock& Data)
{
    return FBox(Data.Center - Data.Size * 0.5, Data.Center + Data.Size * 0.5);
}
}

bool UVanguardRouteStateComponent::Configure(Abandoned::Application::Expedition::VanguardDestination Entry,
    const UVanguardPassageLayout* Passage, FString& Error)
{
    Destination = Entry;
    if (AtStation())
    {
        return true;
    }
    if (!Passage)
    {
        Error = TEXT("Vanguard passage requires a passage layout.");
        return false;
    }
    const Abandoned::Infrastructure::Expedition::UnrealCheckpointStore Store;
    RecoveryDestination = PassageRecovery(Destination, GetWorld()->WorldType == EWorldType::Game ? &Store : nullptr);
    ExitZone = BoundsOf(Passage->ExitZone);
    const auto* RadiationLayout = Cast<UVanguardRadiationLayout>(Passage);
    if (RadiationLayout)
    {
        const Abandoned::Domain::Expedition::FlarePattern Pattern{RadiationLayout->QuietSeconds,
            RadiationLayout->FlareSeconds, RadiationLayout->QuietDosePerSecond, RadiationLayout->FlareDosePerSecond,
            RadiationLayout->RecoveryDosePerSecond};
        ExposureZone = BoundsOf(RadiationLayout->ExposureBounds);
        if (!Pattern.IsValid() || !RadiationLayout->ExposureBounds.IsValid() || RadiationLayout->ShelterBounds.IsEmpty() ||
            ExposureZone.IsInsideOrOn(Passage->Spawn) || ExposureZone.Intersect(ExitZone))
        {
            Error = TEXT("Invalid radiation cycle, exposure bounds or shelters.");
            return false;
        }
        for (const FSceneBlock& Shelter : RadiationLayout->ShelterBounds)
        {
            const FBox Bounds = BoundsOf(Shelter);
            if (!Shelter.IsValid() || !ExposureZone.IsInsideOrOn(Bounds.Min) || !ExposureZone.IsInsideOrOn(Bounds.Max))
            {
                Error = TEXT("Invalid radiation cycle, exposure bounds or shelters.");
                return false;
            }
            Shelters.Add(Bounds);
        }
        if (!UVanguardSession::Find(*this))
        {
            Error = TEXT("Radiation passage requires an expedition session.");
            return false;
        }
        Hazards.EnableRadiation(Pattern);
    }
    return true;
}

void UVanguardRouteStateComponent::EnterSegment()
{
    if (UVanguardSession* Session = UVanguardSession::Find(*this))
    {
        if (Destination.Entry == RouteEntry::Campaign)
        {
            const auto Saved = Abandoned::Infrastructure::Expedition::UnrealCheckpointStore{}.Read();
            Session->Suit.RestoreRecords(Saved.Archive);
        }
        if (AtStation())
        {
            const double ClearedDose = Session->Suit.EnterStation();
            bClearedRadiation = ClearedDose > 0.0;
            if (bClearedRadiation)
            {
                UE_LOG(LogVanguardState, Display, TEXT("Vanguard station cleared remaining radiation dose: %.2f%%."), ClearedDose);
            }
        }
        else
        {
            Session->Suit.EnterPassage(Destination.Stop == VanguardStop::RadiationPassage);
        }
    }
}

bool UVanguardRouteStateComponent::AtStation() const
{
    return Destination.Stop == VanguardStop::Station;
}

float UVanguardRouteStateComponent::GetOxygenPercent() const
{
    const UVanguardSession* Session = UVanguardSession::Find(*this);
    return Session ? Session->Suit.OxygenPercent() : 100.0f;
}

std::optional<Abandoned::Domain::Expedition::DosimeterReading> UVanguardRouteStateComponent::GetDosimeter() const
{
    if (const UVanguardSession* Session = UVanguardSession::Find(*this))
    {
        return Hazards.ReadDosimeter(Session->Suit);
    }
    return std::nullopt;
}

Abandoned::Application::Expedition::SuitTelemetry UVanguardRouteStateComponent::GetSuitTelemetry() const
{
    const UVanguardSession* Session = UVanguardSession::Find(*this);
    if (!Session)
    {
        return {100.0f, 0.0, std::nullopt, AtStation()};
    }
    const auto& Suit = Session->Suit;
    return {Suit.OxygenPercent(), Suit.RadiationDose().Percent(), Hazards.ReadDosimeter(Suit), AtStation()};
}

RadiationPosition UVanguardRouteStateComponent::ClassifyPosition(const FVector& Position) const
{
    if (!ExposureZone.IsValid || !ExposureZone.IsInsideOrOn(Position))
    {
        return RadiationPosition::Outside;
    }
    const bool bSheltered = Shelters.ContainsByPredicate([&Position](const FBox& Bounds) { return Bounds.IsInsideOrOn(Position); });
    return bSheltered ? RadiationPosition::Sheltered : RadiationPosition::Exposed;
}

std::optional<WorldDestination> UVanguardRouteStateComponent::RestartRadiationPassage()
{
    if (Destination.Stop != VanguardStop::RadiationPassage || !RecoveryDestination || bTravelling)
    {
        return std::nullopt;
    }
    bTravelling = true;
    return RecoveryDestination;
}

void UVanguardRouteStateComponent::Resupply(bool bAllowCampaignSave)
{
    if (UVanguardSession* Session = UVanguardSession::Find(*this); AtStation() && Session)
    {
        Session->Suit.Resupply();
        bResupplied = true;
        UE_LOG(LogVanguardState, Display, TEXT("Vanguard oxygen supply restored."));
        if (Destination.Entry == RouteEntry::Campaign && bAllowCampaignSave)
        {
            bCheckpointWriteFailed = !Abandoned::Infrastructure::Expedition::UnrealCheckpointStore{}.RecordResupply();
            if (bCheckpointWriteFailed)
            {
                UE_LOG(LogVanguardState, Warning, TEXT("Could not save the resupply record. Previous data preserved."));
            }
        }
    }
}

void UVanguardRouteStateComponent::RecordLanding(double SurfaceHeight)
{
    if (const auto Drop = Hazards.Land(SurfaceHeight); Drop && Hazards.LastImpact() != FallImpact::None)
    {
        UE_LOG(LogVanguardState, Display, TEXT("Passage fall %.1f cm: %s."), *Drop,
            Hazards.LastImpact() == FallImpact::Fatal ? TEXT("fatal") : TEXT("minor injury"));
    }
}

bool UVanguardRouteStateComponent::AdvanceMovement(std::optional<double> GroundHeight, float Seconds)
{
    return Hazards.AdvanceMovement(GroundHeight, Seconds, GetDefault<UVanguardInjurySettings>()->Recovery());
}

std::optional<FVanguardTransition> UVanguardRouteStateComponent::AdvancePassage(const FVector& Position, float DeltaSeconds)
{
    if (bTravelling || !RecoveryDestination)
    {
        return std::nullopt;
    }
    UVanguardSession* Session = UVanguardSession::Find(*this);
    const auto Observation = Hazards.Observe(ExitZone.IsInside(Position), Position.Z < -600.0, ClassifyPosition(Position),
        DeltaSeconds, Session ? &Session->Suit : nullptr);
    switch (ResolvePassage(Observation))
    {
    case PassageOutcome::ReturnToCheckpoint:
        if (Observation.RadiationLimitReached)
        {
            UE_LOG(LogVanguardState, Display, TEXT("Radiation dose limit reached. Returning to the passage checkpoint."));
        }
        if (const auto* Chamber = std::get_if<ChamberDestination>(&*RecoveryDestination); Chamber && Chamber->Chamber)
        {
            UE_LOG(LogVanguardState, Display, TEXT("Passage checkpoint recovery: %s (%s)."),
                *Abandoned::Infrastructure::Expedition::WriteSegment(*Chamber->Chamber),
                Observation.OxygenDepleted ? TEXT("oxygen depleted") : TEXT("fall"));
        }
        bTravelling = true;
        return FVanguardTransition{*RecoveryDestination, Observation.RadiationLimitReached};
    case PassageOutcome::EnterSanctuary:
        bTravelling = true;
        return FVanguardTransition{StationAfterPassage(Destination, *RecoveryDestination)};
    default:
        return std::nullopt;
    }
}
