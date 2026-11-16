#include "Expedition/VanguardRoute.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRouteLayout.h"

#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"

DEFINE_LOG_CATEGORY_STATIC(LogRadiationVerification, Log, All);

void AVanguardRouteGameMode::VerifyRadiation(AVanguardCharacter& Player, float DeltaSeconds)
{
    VerificationSeconds += DeltaSeconds;
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogRadiationVerification, Error, TEXT("Radiation verification failed: %s (step %d)."),
            Reason, static_cast<int>(RadiationVerification));
        bVerifyRadiation = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    if (VerificationSeconds > 35.0f)
    {
        Fail(TEXT("timeout"));
        return;
    }
    const UVanguardSession* Session = GetSession();
    const auto Reading = GetDosimeter();
    if (AtStation())
    {
        if (Reading || !Session || !Session->bRecoveredFromRadiation || !HasClearedRadiation() ||
            Session->Dose.Percent() != 0.0)
        {
            Fail(TEXT("the station should clear the remaining dose after a full passage run"));
            return;
        }
        UE_LOG(LogRadiationVerification, Display, TEXT("Radiation passage verification passed: forecast, shelter, recovery outside, checkpoint and station decontamination."));
        bVerifyRadiation = false;
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return;
    }
    if (!Reading || !Session || !RadiationLayout)
    {
        Fail(TEXT("the passage dosimeter is unavailable"));
        return;
    }

    using Abandoned::Domain::Expedition::RadiationPosition;
    const FSceneBlock& FirstShelter = RadiationLayout->ShelterBounds[0];
    const FVector TowardShelter = FVector::RightVector * FMath::Sign(FirstShelter.Center.Y);
    const auto Capture = [this](const TCHAR* Name)
    {
        if (bCaptureRadiation)
        {
            FScreenshotRequest::RequestScreenshot(Name, false, false);
        }
    };
    switch (RadiationVerification)
    {
    case ERadiationVerification::Gate:
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
            RadiationVerification = ERadiationVerification::RunToStation;
            return;
        }
        Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(FirstShelter.Center.X, FirstShelter.Center.Y, Layout->Spawn.Z)));
        RadiationVerification = ERadiationVerification::WaitForFlare;
        break;
    case ERadiationVerification::WaitForFlare:
        if (Reading->Position != RadiationPosition::Sheltered || Reading->DosePercent != 0.0 ||
            Reading->DosePerSecond != 0.0)
        {
            Fail(TEXT("the first shelter should block all exposure"));
            return;
        }
        if (Reading->FlareActive)
        {
            Capture(TEXT("Radiation-Sheltered"));
            RadiationVerification = ERadiationVerification::LeaveShelter;
        }
        break;
    case ERadiationVerification::LeaveShelter:
        if (Reading->Position == RadiationPosition::Exposed && Reading->FlareActive && Reading->DosePercent >= 5.0)
        {
            Capture(TEXT("Radiation-Exposed"));
            UE_LOG(LogRadiationVerification, Display, TEXT("Radiation exposure increased dose during a flare: %.2f%%."), Reading->DosePercent);
            RadiationVerification = ERadiationVerification::ReturnToShelter;
        }
        else
        {
            Player.AddMovementInput(-TowardShelter);
        }
        break;
    case ERadiationVerification::ReturnToShelter:
        if (Reading->Position == RadiationPosition::Sheltered)
        {
            Player.GetCharacterMovement()->StopMovementImmediately();
            ProtectedDose = Reading->DosePercent;
            RadiationVerification = ERadiationVerification::HoldInShelter;
        }
        else
        {
            Player.AddMovementInput(TowardShelter);
        }
        break;
    case ERadiationVerification::HoldInShelter:
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
            const FSceneBlock& Exposure = RadiationLayout->ExposureBounds;
            const double BeforeExposure = Exposure.Center.X - Exposure.Size.X * 0.5 - 85.0;
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(BeforeExposure, 0.0, Layout->Spawn.Z)));
            RadiationVerification = ERadiationVerification::RecoverOutside;
        }
        break;
    case ERadiationVerification::RecoverOutside:
        if (Reading->Position != RadiationPosition::Outside || Reading->DosePerSecond != -RadiationLayout->RecoveryDosePerSecond ||
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
            RadiationVerification = ERadiationVerification::LethalExposure;
        }
        break;
    case ERadiationVerification::LethalExposure:
        // Ordinary passage logic reloads the world when the exposed player exceeds the dose limit.
        break;
    case ERadiationVerification::RunToStation:
        Player.AddMovementInput(FVector::ForwardVector);
        break;
    }
}
#endif
