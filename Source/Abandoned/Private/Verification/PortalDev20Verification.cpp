#include "Verification/PortalDev20Verification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Domain/Expedition/Dev20.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Engine/World.h"
#include "Expedition/Dev20Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PortalLab.h"
#include "Puzzles/PortalLabLayout.h"
#include "UnrealClient.h"
#include "Verification/VerificationPlayerPose.h"

DEFINE_LOG_CATEGORY_STATIC(LogPortalDev20Test, Log, All);
#endif

UPortalDev20Verification::UPortalDev20Verification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void UPortalDev20Verification::AttachIfRequested(APortalLabGameMode& Lab, const UPortalLabLayout& Settings,
    ADev20Actor& Item, UBoxComponent& ArrivalConsole, bool bRecoveryArrival)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("VerifyDev20")))
    {
        return;
    }
    auto* Test = NewObject<UPortalDev20Verification>(&Lab);
    Test->Layout = &Settings;
    Test->Unit = &Item;
    Test->Console = &ArrivalConsole;
    Test->bCapture = FParse::Param(FCommandLine::Get(), TEXT("CaptureDev20"));
    if (bRecoveryArrival && FParse::Param(FCommandLine::Get(), TEXT("VerifyDev20Recovery")))
    {
        Test->Phase = EPhase::RecoveryArrival;
    }
    Lab.AddInstanceComponent(Test);
    Test->RegisterComponent();
    Test->AddTickPrerequisiteActor(&Lab);
    Test->SetComponentTickEnabled(true);
}

void UPortalDev20Verification::Advance(EPhase Next)
{
    Phase = Next;
    Elapsed = 0.0f;
}

void UPortalDev20Verification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogPortalDev20Test, Error, TEXT("DEV-20 portal test failed: %s (phase %d)."), Reason, static_cast<int32>(Phase));
    SetComponentTickEnabled(false);
    Input.Release();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UPortalDev20Verification::EndPlay(const EEndPlayReason::Type Reason)
{
    Input.Release();
    Super::EndPlay(Reason);
}

void UPortalDev20Verification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    using Abandoned::Domain::Expedition::Dev20MassKg;
    using Abandoned::Verification::PlacePlayer;
    Elapsed += DeltaSeconds;
    if (Elapsed > 8.0f)
    {
        Fail(TEXT("timed out waiting for the physical route or world travel"));
        return;
    }
    if (Input.Release())
    {
        return;
    }
    auto* Lab = Cast<APortalLabGameMode>(GetOwner());
    if (!Lab || !Unit.IsValid() || !Layout.IsValid() || !Console.IsValid() || !IsValid(Unit->GetBody()))
    {
        Fail(TEXT("the original unit or its station fixture disappeared"));
        return;
    }
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<APortalLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player)
    {
        return;
    }

    const FPortalTransitObservation Transit = Lab->GetTransitObservation();
    const auto LookFrom = [&](FVector At, const FVector& Target)
    {
        At.Z = Layout->Spawn.Z;
        const FVector Eye = At + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        PlacePlayer(*Player, FTransform((Target - Eye).Rotation(), At));
    };
    const auto WalkForward = [&]
    {
        Player->AddMovementInput(FRotator(0, Controller->GetControlRotation().Yaw, 0).Vector());
    };
    switch (Phase)
    {
    case EPhase::Inspect:
        if (Elapsed > 0.4f)
        {
            if (!FMath::IsNearlyEqual(Unit->GetBody()->GetMass(), Dev20MassKg, 0.05f))
            {
                Fail(TEXT("Chaos mass is not 34 kg"));
                return;
            }
            LookFrom(Layout->Dev20Station - FVector(133, -35, 0), Unit->GetActorLocation());
            Advance(EPhase::Power);
        }
        break;
    case EPhase::Power:
        if (bCapture && Elapsed >= 0.12f)
        {
            FScreenshotRequest::RequestScreenshot(TEXT("DEV20-Station"), false, false);
            bCapture = false;
            break;
        }
        if (Elapsed > 0.20f)
        {
            LookFrom(Layout->Arrival.Console + FVector(0, 120, 0), Layout->Arrival.Console);
            Input.Press(*Controller, EKeys::E);
            Advance(EPhase::Take);
        }
        break;
    case EPhase::Take:
        if (!Transit.bPowered)
        {
            Fail(TEXT("console did not power the portal pair"));
            return;
        }
        LookFrom(Layout->Dev20Station - FVector(100, -30, 0), Unit->GetActorLocation());
        Input.Press(*Controller, EKeys::E);
        Advance(EPhase::CheckGrip);
        break;
    case EPhase::CheckGrip:
        if (Player->GetHeldDev20() != Unit.Get() || !FMath::IsNearlyEqual(Player->GetCharacterMovement()->MaxWalkSpeed,
            Abandoned::MovementProfile::WalkSpeed * Abandoned::Domain::Puzzles::CarrySpeedScale(Dev20MassKg), 0.05f))
        {
            Fail(TEXT("could not pick up DEV-20 with the expected carry speed"));
            return;
        }
        UE_LOG(LogPortalDev20Test, Display, TEXT("DEV-20: 34 kg Chaos mass and carry slowdown confirmed."));
        PlacePlayer(*Player, FTransform(FRotator(-18, 0, 0), Layout->Arrival.Center - FVector(235, 0, 53)));
        Advance(bPowerLossVerified ? EPhase::Launch : EPhase::Approach);
        break;
    case EPhase::Approach:
        if (Elapsed > 0.30f)
        {
            WalkForward();
            if (Transit.bApproachingArrival)
            {
                // Cut power at the console while the carrier is inside the entrance.
                if (!Lab->UseDevice(*Player, *Console))
                {
                    Fail(TEXT("station console rejected the power cut"));
                    return;
                }
                Advance(EPhase::Interrupted);
            }
        }
        break;
    case EPhase::Interrupted:
        if (Transit.bInterrupted)
        {
            if (Transit.bPowered || Player->GetHeldDev20() || !Unit->GetActorLocation().Equals(Layout->Dev20Station, 4.0))
            {
                Fail(TEXT("power loss failed to recover the same DEV-20 at its station"));
                return;
            }
            UE_LOG(LogPortalDev20Test, Display, TEXT("DEV-20: loss of portal power returned the same item to its cradle."));
            bPowerLossVerified = true;
            Advance(EPhase::Power);
        }
        break;
    case EPhase::Launch:
        if (Elapsed >= 0.40f)
        {
            Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            Player->GetCharacterMovement()->Velocity = FVector(660, 0, 560);
            Advance(EPhase::Cross);
        }
        break;
    case EPhase::Cross:
        WalkForward();
        if (Transit.bDev20Crossed)
        {
            if (Player->GetHeldDev20() != Unit.Get() || Transit.Transfers != 1)
            {
                Fail(TEXT("expected exactly one transfer with the original DEV-20 in the grip"));
                return;
            }
            if (Transit.Before.X <= 300.0 || !Transit.After.Equals(FVector(-Transit.Before.Y, Transit.Before.X, Transit.Before.Z), 0.05))
            {
                Fail(TEXT("carrier velocity did not match the 90 degree portal turn"));
                return;
            }
            UE_LOG(LogPortalDev20Test, Display, TEXT("DEV-20: same 34 kg body remained in the grip after the portal."));
            Advance(EPhase::Depart);
        }
        break;
    case EPhase::Depart:
        if (Player->GetHeldDev20() != Unit.Get())
        {
            Fail(TEXT("DEV-20 was lost before the chamber hand-off"));
            return;
        }
        WalkForward();
        break;
    case EPhase::RecoveryArrival:
        if (Elapsed >= 0.25f)
        {
            TArray<AActor*> Units;
            UGameplayStatics::GetAllActorsOfClass(this, ADev20Actor::StaticClass(), Units);
            if (Units.Num() != 1 || Units[0] != Unit.Get() || Player->GetHeldDev20() || Transit.bPowered ||
                !Unit->GetActorLocation().Equals(Layout->Dev20Station, 4.0) ||
                !FMath::IsNearlyEqual(Unit->GetBody()->GetMass(), Dev20MassKg, 0.05f))
            {
                Fail(TEXT("recovery from the previous world did not restore one DEV-20 at the cradle"));
                return;
            }
            UE_LOG(LogPortalDev20Test, Display, TEXT("DEV-20 recovery verification passed: lost in Button, returned to the station."));
            SetComponentTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif
