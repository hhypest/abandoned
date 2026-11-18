#include "Verification/VanguardRadiationVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"
#include "Verification/VanguardWristTerminalVerification.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogRadiationVerification, Log, All);

UVanguardRadiationVerification* UVanguardRadiationVerification::AttachIfRequested(
    AVanguardRouteGameMode& Route, const UVanguardRouteLayout& Layout)
{
    const auto* Passage = Cast<UVanguardRadiationLayout>(&Layout);
    const bool bCheckWrist = FParse::Param(FCommandLine::Get(), TEXT("VerifyWristTerminal"));
    if ((!Passage && !Route.AtStation()) || (!bCheckWrist && !FParse::Param(FCommandLine::Get(), TEXT("VerifyRadiation"))))
    {
        return nullptr;
    }
    auto* Check = NewObject<UVanguardRadiationVerification>(&Route);
    Check->PassageLayout = Passage;
    Check->bCapture = FParse::Param(FCommandLine::Get(), TEXT("CaptureRadiation"));
    if (bCheckWrist)
    {
        Check->WristCheck = UVanguardWristTerminalVerification::Attach(Route, Layout.Spawn.Z);
    }
    Route.AddInstanceComponent(Check);
    Check->RegisterComponent();
    return Check;
}

void UVanguardRadiationVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogRadiationVerification, Error, TEXT("Radiation verification failed: %s (step %d)."),
        Reason, static_cast<int>(Phase));
    Phase = EPhase::Finished;
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UVanguardRadiationVerification::ObserveDoseLimit(UVanguardSession& Session)
{
    if (Phase != EPhase::LethalExposure || !Session.Suit.RadiationDose().IsLethal())
    {
        Fail(TEXT("checkpoint recovery must follow lethal exposure on the open strip"));
        return;
    }
    Session.bRecoveredFromRadiation = true;
}

void UVanguardRadiationVerification::CheckStep(const AVanguardRouteGameMode& Route, AVanguardCharacter& Player, float DeltaSeconds)
{
    if (Phase == EPhase::Finished)
    {
        return;
    }
    auto* Wrist = WristCheck.Get();
    if (Wrist && !Wrist->CheckStep(Route, Player, DeltaSeconds))
    {
        return;
    }
    Elapsed += DeltaSeconds;
    if (Elapsed > 35.0f)
    {
        Fail(TEXT("timeout"));
        return;
    }
    const auto* Session = UGameInstance::GetSubsystem<UVanguardSession>(Route.GetGameInstance());
    const auto Reading = Route.GetDosimeter();
    if (Route.AtStation())
    {
        if (Reading || !Session || !Session->bRecoveredFromRadiation || !Route.HasClearedRadiation() ||
            Session->Suit.RadiationDose().Percent() != 0.0)
        {
            Fail(TEXT("the station should clear the remaining dose after a full passage run"));
            return;
        }
        UE_LOG(LogRadiationVerification, Display, TEXT("Radiation passage verification passed: forecast, shelter, recovery outside, checkpoint and station decontamination."));
        Phase = EPhase::Finished;
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return;
    }
    const auto* Layout = PassageLayout.Get();
    if (!Reading || !Session || !Layout || Layout->ShelterBounds.IsEmpty())
    {
        Fail(TEXT("the passage dosimeter is unavailable"));
        return;
    }

    using Abandoned::Domain::Expedition::RadiationPosition;
    const FSceneBlock& FirstShelter = Layout->ShelterBounds[0];
    const FVector TowardShelter = FVector::RightVector * FMath::Sign(FirstShelter.Center.Y);
    const auto Capture = [this](const TCHAR* Name)
    {
        if (bCapture)
        {
            FScreenshotRequest::RequestScreenshot(Name, false, false);
        }
    };
    switch (Phase)
    {
    case EPhase::Gate:
        if (Reading->Position != RadiationPosition::Outside || Reading->DosePercent != 0.0 ||
            Reading->DosePerSecond != 0.0 || Reading->FlareActive || Reading->SecondsToChange <= 0.0)
        {
            Fail(TEXT("the entrance should be outside exposure with a fresh forecast and dose"));
            return;
        }
        if (Session->bRecoveredFromRadiation)
        {
            UE_LOG(LogRadiationVerification, Display, TEXT("Radiation checkpoint restored: safe position, zero dose and fresh forecast."));
            Player.SetSprint(true);
            Phase = EPhase::RunToStation;
            return;
        }
        Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(FirstShelter.Center.X, FirstShelter.Center.Y, Layout->Spawn.Z)));
        Phase = EPhase::WaitForFlare;
        break;
    case EPhase::WaitForFlare:
        if (Reading->Position != RadiationPosition::Sheltered || Reading->DosePercent != 0.0 ||
            Reading->DosePerSecond != 0.0)
        {
            Fail(TEXT("the first shelter should block all exposure"));
            return;
        }
        if (Reading->FlareActive)
        {
            Capture(TEXT("Radiation-Sheltered"));
            Phase = EPhase::LeaveShelter;
        }
        break;
    case EPhase::LeaveShelter:
        if (Reading->Position == RadiationPosition::Exposed && Reading->FlareActive && Reading->DosePercent >= 5.0)
        {
            if (Wrist && !Wrist->CheckExposure(Player, Reading->DosePercent))
            {
                return;
            }
            Capture(TEXT("Radiation-Exposed"));
            UE_LOG(LogRadiationVerification, Display, TEXT("Radiation exposure increased dose during a flare: %.2f%%."), Reading->DosePercent);
            Phase = EPhase::ReturnToShelter;
        }
        else
        {
            Player.AddMovementInput(-TowardShelter);
        }
        break;
    case EPhase::ReturnToShelter:
        if (Reading->Position == RadiationPosition::Sheltered)
        {
            Player.GetCharacterMovement()->StopMovementImmediately();
            ProtectedDose = Reading->DosePercent;
            Phase = EPhase::HoldInShelter;
        }
        else
        {
            Player.AddMovementInput(TowardShelter);
        }
        break;
    case EPhase::HoldInShelter:
        if (Reading->Position != RadiationPosition::Sheltered || !Reading->FlareActive ||
            Reading->DosePerSecond != 0.0 || Reading->DosePercent != ProtectedDose)
        {
            Fail(TEXT("cover must stop the active flare dose without clearing accumulated exposure"));
            return;
        }
        ShelterSeconds += DeltaSeconds;
        if (ShelterSeconds >= 0.35f)
        {
            UE_LOG(LogRadiationVerification, Display, TEXT("Radiation shelter held dose at %.2f%% through the active flare."), ProtectedDose);
            const FSceneBlock& Exposure = Layout->ExposureBounds;
            const double BeforeExposure = Exposure.Center.X - Exposure.Size.X * 0.5 - 85.0;
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(BeforeExposure, 0.0, Layout->Spawn.Z)));
            Phase = EPhase::RecoverOutside;
        }
        break;
    case EPhase::RecoverOutside:
        if (Reading->Position != RadiationPosition::Outside || Reading->DosePerSecond != -Layout->RecoveryDosePerSecond ||
            Reading->DosePercent <= 0.0 || Reading->DosePercent >= ProtectedDose)
        {
            Fail(TEXT("dose should recover gradually after leaving the exposure zone"));
            return;
        }
        if (ProtectedDose - Reading->DosePercent >= 1.0)
        {
            UE_LOG(LogRadiationVerification, Display, TEXT("Radiation recovery outside the zone: %.2f%% to %.2f%%."),
                ProtectedDose, Reading->DosePercent);
            Capture(TEXT("Radiation-Recovering"));
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(FirstShelter.Center.X, 0.0, Layout->Spawn.Z)));
            Phase = EPhase::LethalExposure;
        }
        break;
    case EPhase::LethalExposure:
        // Ordinary passage logic reloads the world when the exposed player exceeds the dose limit.
        break;
    case EPhase::RunToStation:
        Player.AddMovementInput(FVector::ForwardVector);
        break;
    case EPhase::Finished:
        break;
    }
}
#endif
