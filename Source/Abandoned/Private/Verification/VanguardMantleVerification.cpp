#include "Verification/VanguardMantleVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "World/Blockout.h"

DEFINE_LOG_CATEGORY_STATIC(LogMantleVerification, Log, All);

UVanguardMantleVerification* UVanguardMantleVerification::Attach(AActor& Owner, const FSceneBlock& Ledge, bool bCapture)
{
    auto* Check = NewObject<UVanguardMantleVerification>(&Owner);
    Check->Front = Ledge.Center.X - Ledge.Size.X * 0.5;
    Check->Top = Ledge.Center.Z + Ledge.Size.Z * 0.5;
    Check->bCapture = bCapture;
    Owner.AddInstanceComponent(Check);
    Check->RegisterComponent();
    return Check;
}

void UVanguardMantleVerification::ReleaseTestInputsAndBlocker()
{
    JumpKey.Release();
    CrouchKey.Release();
    if (AActor* Blocker = TestBlocker.Get())
    {
        Blocker->Destroy();
        TestBlocker.Reset();
    }
}

void UVanguardMantleVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseTestInputsAndBlocker();
    Super::EndPlay(Reason);
}

bool UVanguardMantleVerification::CheckStep(AVanguardCharacter& Player, float DeltaSeconds)
{
    if (Phase == EPhase::Done)
    {
        return true;
    }
    const auto Fail = [this, &Player](const TCHAR* Why)
    {
        UE_LOG(LogMantleVerification, Error, TEXT("Passage climb verification failed: %s at %s (step %d)."),
            Why, *Player.GetActorLocation().ToCompactString(), static_cast<int>(Phase));
        ReleaseTestInputsAndBlocker();
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return false;
    };
    auto* PC = Cast<APlayerController>(Player.GetController());
    if (!PC)
    {
        return Fail(TEXT("no controller"));
    }
    const bool bGrounded = Player.GetCharacterMovement()->IsMovingOnGround();
    const float FullLamp = GetDefault<UVanguardFlashlightSettings>()->IntensityLumens;
    const FVector Location = Player.GetActorLocation();
    Elapsed += DeltaSeconds;
    const bool bAnyHandsVisible = Player.MantleGloves[0]->IsVisible() || Player.MantleGloves[1]->IsVisible() ||
        Player.MantleSleeves[0]->IsVisible() || Player.MantleSleeves[1]->IsVisible();
    if (!Player.IsMantling() && bAnyHandsVisible)
    {
        return Fail(TEXT("hands were visible without an active climb"));
    }

    switch (Phase)
    {
    case EPhase::Approach:
        if (bCapture && !bCapturedApproach && Player.CanMantle())
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Vanguard-Climb-Prompt"), false, false);
            bCapturedApproach = true;
        }
        if (Location.X > Front)
        {
            return Fail(TEXT("walking bypassed the ledge"));
        }
        if (bGrounded && Player.CanMantle() && Location.X >= Front - 35.0)
        {
            JumpKey.Press(*PC, EKeys::SpaceBar);
            Phase = EPhase::ReleaseTap;
        }
        break;
    case EPhase::ReleaseTap:
        JumpKey.Release();
        bTapLeftGround = !bGrounded;
        Elapsed = 0.0f;
        Phase = EPhase::WaitForLanding;
        break;
    case EPhase::WaitForLanding:
        bTapLeftGround |= !bGrounded;
        if (Player.IsMantling() || Location.X > Front)
        {
            return Fail(TEXT("a short jump bypassed the ledge"));
        }
        if (Elapsed > 0.7f && bGrounded)
        {
            if (!bTapLeftGround)
            {
                return Fail(TEXT("the short press should still jump"));
            }
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(Front - 280.0, Location.Y, Location.Z)));
            Phase = EPhase::DuckRunUp;
        }
        break;
    case EPhase::DuckRunUp:
        if (bGrounded && Location.X >= Front - 110.0)
        {
            JumpKey.Press(*PC, EKeys::SpaceBar);
            Phase = EPhase::DuckJump;
        }
        break;
    case EPhase::DuckJump:
        if (!bGrounded)
        {
            CrouchKey.Press(*PC, EKeys::LeftControl);
            JumpKey.Release();
            Elapsed = 0.0f;
            Phase = EPhase::DuckLanding;
        }
        break;
    case EPhase::DuckLanding:
        bDuckedInAir |= Player.bIsCrouched && !bGrounded;
        if (Player.IsMantling() || Location.X > Front)
        {
            return Fail(TEXT("a crouch jump bypassed the ledge"));
        }
        if (Elapsed > 0.7f && bGrounded)
        {
            if (!bDuckedInAir)
            {
                return Fail(TEXT("the crouch jump did not crouch in the air"));
            }
            CrouchKey.Release();
            Phase = EPhase::PlaceBlocker;
        }
        break;
    case EPhase::PlaceBlocker:
        if (bGrounded && !Player.bIsCrouched)
        {
            UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
            const auto Blocker = Cube ? Abandoned::Blockout::SpawnBox(*GetWorld(), *Cube,
                FVector(Front + 58.0, Location.Y, Top + 150.0), FVector(70.0, 160.0, 40.0)) : Abandoned::Blockout::FBox{};
            if (!IsValid(Blocker.Collision))
            {
                return Fail(TEXT("could not place a landing obstruction"));
            }
            TestBlocker = Blocker.Collision->GetOwner();
            JumpKey.Press(*PC, EKeys::SpaceBar);
            Elapsed = 0.0f;
            Phase = EPhase::BlockedHold;
            UE_LOG(LogMantleVerification, Display, TEXT("Passage ledge stopped walking, a short Space jump and a crouch jump."));
        }
        break;
    case EPhase::BlockedHold:
        if (Player.IsMantling() || Location.X > Front)
        {
            return Fail(TEXT("the occupied landing was accepted"));
        }
        if (Elapsed > 0.8f && bGrounded && !Player.CanMantle())
        {
            ReleaseTestInputsAndBlocker();
            Phase = EPhase::Hold;
            UE_LOG(LogMantleVerification, Display, TEXT("Passage climb rejected an obstructed landing."));
        }
        break;
    case EPhase::Hold:
        if (bGrounded && Player.CanMantle())
        {
            Player.ToggleWristTerminal();
            if (!Player.IsWristTerminalOpen())
            {
                return Fail(TEXT("the wrist terminal could not open before the climb"));
            }
            JumpKey.Press(*PC, EKeys::SpaceBar);
            Phase = EPhase::PullUp;
        }
        break;
    case EPhase::PullUp:
        if (Player.IsMantling())
        {
            if (Player.IsWristTerminalOpen() || Player.WristCasing->IsVisible())
            {
                return Fail(TEXT("the wrist terminal did not lower for the climb"));
            }
            JumpKey.Release();
            Phase = EPhase::WaitForTop;
        }
        break;
    case EPhase::WaitForTop:
        if (Player.IsMantling() && Player.MantleSeconds >= 0.08 && Player.MantleSeconds <= 0.26 &&
            Player.ActiveMantleHandhold)
        {
            const auto& Handhold = *Player.ActiveMantleHandhold;
            const FVector Center = (Handhold.Edge[0] + Handhold.Edge[1]) * 0.5 + FVector(0, 0, 2.5);
            PC->SetControlRotation((Center - Player.Camera->GetComponentLocation()).Rotation());
        }
        if (Player.IsMantling() && Player.MantleSeconds >= 0.14 && Player.MantleSeconds <= 0.25)
        {
            if (!Player.ActiveMantleHandhold || !Player.IsFlashlightOn() || Player.ShoulderLight->Intensity >= FullLamp)
            {
                return Fail(TEXT("the handhold or its close-range lighting did not activate"));
            }
            const auto& Handhold = *Player.ActiveMantleHandhold;
            for (int Hand = 0; Hand < 2; ++Hand)
            {
                const UStaticMeshComponent& Glove = *Player.MantleGloves[Hand];
                const UStaticMeshComponent& Sleeve = *Player.MantleSleeves[Hand];
                if (!Glove.GetStaticMesh() || !Sleeve.GetStaticMesh() || !Glove.IsVisible() || !Sleeve.IsVisible() ||
                    !Glove.GetComponentLocation().Equals(Handhold.Edge[Hand], 0.1) ||
                    !FMath::IsNearlyEqual(Glove.GetComponentLocation().Z, Top, 0.1) ||
                    Glove.GetCollisionEnabled() != ECollisionEnabled::NoCollision ||
                    Sleeve.GetCollisionEnabled() != ECollisionEnabled::NoCollision)
                {
                    return Fail(TEXT("the gloves did not stay on the ledge during the lift"));
                }
            }
            if (!bSawHandhold)
            {
                if (bCapture)
                {
                    FScreenshotRequest::RequestScreenshot(TEXT("Vanguard-Climb-Handhold"), false, false);
                }
                bSawHandhold = true;
            }
            else if (Player.MantleSeconds >= 0.20)
            {
                bVerifiedHandhold = true;
            }
        }
        if (!Player.IsMantling() && bGrounded && Location.X >= Front + 34.0 && Location.Z > Top)
        {
            if (!bVerifiedHandhold || !Player.IsFlashlightOn() ||
                !FMath::IsNearlyEqual(Player.ShoulderLight->Intensity, FullLamp))
            {
                return Fail(TEXT("the handhold did not finish or restore the flashlight"));
            }
            if (bCapture)
            {
                FScreenshotRequest::RequestScreenshot(TEXT("Vanguard-Climb-Top"), false, false);
            }
            PC->SetControlRotation(FRotator::ZeroRotator);
            UE_LOG(LogMantleVerification, Display, TEXT("Passage handhold verified: both gloves fixed to the ledge, then hidden."));
            UE_LOG(LogMantleVerification, Display, TEXT("Passage climb verified: held Space and landed on top."));
            Phase = EPhase::Done;
        }
        break;
    case EPhase::Done:
        break;
    }
    return true;
}
#endif
