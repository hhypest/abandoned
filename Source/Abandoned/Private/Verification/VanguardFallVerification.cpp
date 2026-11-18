#include "Verification/VanguardFallVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "World/Blockout.h"

DEFINE_LOG_CATEGORY_STATIC(LogVanguardFalls, Log, All);

namespace
{
constexpr double DropHeights[] = {250.0, 300.0, 325.0};
constexpr double TestPlatformX = 1550.0;
}

UVanguardFallVerification* UVanguardFallVerification::Attach(AVanguardRouteGameMode& Route)
{
    auto* Check = NewObject<UVanguardFallVerification>(&Route);
    Check->bVerifyAudio = FParse::Param(FCommandLine::Get(), TEXT("VerifyInjuryAudio"));
    Route.AddInstanceComponent(Check);
    Check->RegisterComponent();
    return Check;
}

void UVanguardFallVerification::ReleaseFixture()
{
    if (auto* Breath = ObservedBreath.Get())
    {
        Breath->OnAudioPlaybackPercentNative.Remove(PlaybackHandle);
        ObservedBreath.Reset();
    }
    if (AActor* Platform = PlatformActor.Get())
    {
        Platform->Destroy();
        PlatformActor.Reset();
    }
}

void UVanguardFallVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseFixture();
    Super::EndPlay(Reason);
}

void UVanguardFallVerification::ObserveBreath(const UAudioComponent*, const USoundWave*, float PlayedFraction)
{
    bBreathDecoded |= PlayedFraction > 0.02f;
}

void UVanguardFallVerification::FinishDrop()
{
    UE_LOG(LogVanguardFalls, Display, TEXT("Vanguard fall verified at %.0f cm."), DropHeights[DropIndex]);
    ReleaseFixture();
    ++DropIndex;
    Phase = EPhase::PlacePlatform;
}

void UVanguardFallVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogVanguardFalls, Error, TEXT("Vanguard fall verification failed at %.0f cm: %s."),
        DropHeights[DropIndex], Reason);
    Phase = EPhase::Failed;
    ReleaseFixture();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UVanguardFallVerification::CheckStep(AVanguardRouteGameMode& Route, AVanguardCharacter& Player, float DeltaSeconds)
{
    if (Phase == EPhase::Failed)
    {
        return;
    }
    using namespace Abandoned::MovementProfile;
    UCharacterMovementComponent& Movement = *Player.GetCharacterMovement();
    Elapsed += DeltaSeconds;
    if (Elapsed > 15.0f)
    {
        Fail(Phase == EPhase::Recover ? TEXT("recovery or audio playback timed out") : TEXT("test timed out"));
        return;
    }
    if (Phase == EPhase::Recover)
    {
        if (Route.HasMinorInjuryNotice() || (bVerifyAudio && !bBreathDecoded))
        {
            return;
        }
        if (!FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed))
        {
            Fail(TEXT("normal ground speed did not return"));
            return;
        }
        UE_LOG(LogVanguardFalls, Display, TEXT("Vanguard minor injury slowed ground movement, then restored normal speed."));
        if (bVerifyAudio)
        {
            UE_LOG(LogVanguardFalls, Display, TEXT("Vanguard injury breath decoded in audio mixer."));
        }
        FinishDrop();
        return;
    }
    if (Phase == EPhase::PlacePlatform)
    {
        const double X = TestPlatformX + DropIndex * 500.0;
        const FVector FloorProbe(X, 0.0, 0.0);
        FHitResult Floor;
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(VanguardTestFloor), false, &Player);
        UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
        if (!Cube || !GetWorld()->LineTraceSingleByChannel(Floor, FloorProbe + FVector(0, 0, 50),
            FloorProbe - FVector(0, 0, 50), ECC_Visibility, Query))
        {
            Fail(TEXT("could not place a test platform"));
            return;
        }
        const double Top = Floor.ImpactPoint.Z + DropHeights[DropIndex];
        const auto Platform = Abandoned::Blockout::SpawnBox(*GetWorld(), *Cube, FVector(X, 0, Top - 10.0),
            FVector(120.0, 140.0, 20.0));
        if (!IsValid(Platform.Collision))
        {
            Fail(TEXT("test platform has no collision"));
            return;
        }
        PlatformActor = Platform.Collision->GetOwner();
        if (DropIndex == 1 && bVerifyAudio)
        {
            ObservedBreath = Player.InjuryBreath.Get();
            PlaybackHandle = Player.InjuryBreath->OnAudioPlaybackPercentNative.AddUObject(this, &UVanguardFallVerification::ObserveBreath);
        }
        Route.LastGroundHeight.reset();
        Player.PlaceAt(FTransform(FRotator::ZeroRotator,
            FVector(X, 0, Top + StandingHalfHeight + 2.0)));
        Phase = EPhase::WalkOff;
        return;
    }
    if (Movement.IsFalling())
    {
        const double X = TestPlatformX + DropIndex * 500.0;
        if (Player.GetActorLocation().X > X + 60.0)
        {
            Phase = EPhase::Falling;
        }
    }
    else if (Phase == EPhase::Falling)
    {
        if (Route.HasMinorInjuryNotice() != (DropIndex == 1) || DropIndex >= 2)
        {
            Fail(TEXT("unexpected injury outcome"));
            return;
        }
        if (DropIndex == 0)
        {
            if (!FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed) || Player.InjuryBreath->IsPlaying())
            {
                Fail(TEXT("a safe fall triggered injury feedback"));
                return;
            }
            FinishDrop();
            return;
        }
        const float InitialScale = GetDefault<UVanguardInjurySettings>()->InitialGroundSpeedScale;
        const bool bGroundSlowed = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed * InitialScale) &&
            FMath::IsNearlyEqual(Movement.MaxWalkSpeedCrouched, SlowSpeed * InitialScale);
        Player.SetSprint(true);
        const bool bSprintSlowed = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, SprintSpeed * InitialScale);
        Player.SetSprint(false);
        Movement.SetMovementMode(MOVE_Falling);
        const bool bAirSpeedUnchanged = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed);
        Movement.SetMovementMode(MOVE_Walking);
        const bool bGroundSlowedAgain = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed * InitialScale);
        if (!bGroundSlowed || !bSprintSlowed || !bAirSpeedUnchanged || !bGroundSlowedAgain)
        {
            Fail(TEXT("unexpected ground or air speed after injury"));
            return;
        }
        Phase = EPhase::Recover;
        return;
    }
    Player.AddMovementInput(FVector::ForwardVector);
}
#endif
