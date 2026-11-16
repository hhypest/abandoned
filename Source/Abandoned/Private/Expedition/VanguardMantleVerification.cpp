#include "Expedition/VanguardRoute.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Expedition/VanguardRouteLayout.h"
#include "Components/BoxComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "World/Blockout.h"

DEFINE_LOG_CATEGORY_STATIC(LogMantleVerification, Log, All);

void AVanguardRouteGameMode::VerifyMantle(AVanguardCharacter& Player, float DeltaSeconds)
{
    const auto* Passage = CastChecked<UVanguardPassageLayout>(Layout.Get());
    if (Passage->MantleLedges.IsEmpty() || MantleVerification == EMantleVerification::Done)
    {
        return;
    }
    const auto Fail = [this, &Player](const TCHAR* Why)
    {
        UE_LOG(LogMantleVerification, Error, TEXT("Passage climb verification failed: %s at %s (step %d)."),
            Why, *Player.GetActorLocation().ToCompactString(), static_cast<int>(MantleVerification));
        bVerifyRoute = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    auto* PC = Cast<APlayerController>(Player.GetController());
    if (!PC)
    {
        Fail(TEXT("no controller"));
        return;
    }
    const auto Space = [PC](EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::SpaceBar, Event, FPlatformTime::Cycles64()));
    };
    const auto Ctrl = [PC](EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::LeftControl, Event, FPlatformTime::Cycles64()));
    };
    const FSceneBlock& Ledge = Passage->MantleLedges[0];
    const double Front = Ledge.Center.X - Ledge.Size.X * 0.5;
    const double Top = Ledge.Center.Z + Ledge.Size.Z * 0.5;
    const bool bGrounded = Player.GetCharacterMovement()->IsMovingOnGround();
    const FVector Location = Player.GetActorLocation();
    MantleVerificationSeconds += DeltaSeconds;

    switch (MantleVerification)
    {
    case EMantleVerification::Approach:
        if (bCaptureRoute && !bCapturedMantleApproach && Player.CanMantle())
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Vanguard-Climb-Prompt"), false, false);
            bCapturedMantleApproach = true;
        }
        if (Location.X > Front)
        {
            Fail(TEXT("walking bypassed the ledge"));
        }
        else if (bGrounded && Player.CanMantle() && Location.X >= Front - 35.0)
        {
            Space(IE_Pressed);
            MantleVerification = EMantleVerification::ReleaseTap;
        }
        break;
    case EMantleVerification::ReleaseTap:
        Space(IE_Released);
        bTapLeftGround = !bGrounded;
        MantleVerificationSeconds = 0.0f;
        MantleVerification = EMantleVerification::WaitForLanding;
        break;
    case EMantleVerification::WaitForLanding:
        bTapLeftGround |= !bGrounded;
        if (Player.IsMantling() || Location.X > Front)
        {
            Fail(TEXT("a short jump bypassed the ledge"));
        }
        else if (MantleVerificationSeconds > 0.7f && bGrounded)
        {
            if (!bTapLeftGround)
            {
                Fail(TEXT("the short press should still jump"));
                return;
            }
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(Front - 280.0, Location.Y, Location.Z)));
            MantleVerification = EMantleVerification::DuckRunUp;
        }
        break;
    case EMantleVerification::DuckRunUp:
        if (bGrounded && Location.X >= Front - 110.0)
        {
            Space(IE_Pressed);
            MantleVerification = EMantleVerification::DuckJump;
        }
        break;
    case EMantleVerification::DuckJump:
        if (!bGrounded)
        {
            Ctrl(IE_Pressed);
            Space(IE_Released);
            MantleVerificationSeconds = 0.0f;
            MantleVerification = EMantleVerification::DuckLanding;
        }
        break;
    case EMantleVerification::DuckLanding:
        bDuckedInAir |= Player.bIsCrouched && !bGrounded;
        if (Player.IsMantling() || Location.X > Front)
        {
            Fail(TEXT("a crouch jump bypassed the ledge"));
        }
        else if (MantleVerificationSeconds > 0.7f && bGrounded)
        {
            if (!bDuckedInAir)
            {
                Fail(TEXT("the crouch jump did not crouch in the air"));
                return;
            }
            Ctrl(IE_Released);
            MantleVerification = EMantleVerification::PlaceBlocker;
        }
        break;
    case EMantleVerification::PlaceBlocker:
        if (bGrounded && !Player.bIsCrouched)
        {
            UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
            const auto Blocker = Cube ? Abandoned::Blockout::SpawnBox(*GetWorld(), *Cube,
                FVector(Front + 58.0, Location.Y, Top + 150.0), FVector(70.0, 160.0, 40.0)) : Abandoned::Blockout::FBox{};
            if (!IsValid(Blocker.Collision))
            {
                Fail(TEXT("could not place a landing obstruction"));
                return;
            }
            MantleTestBlocker = Blocker.Collision->GetOwner();
            Space(IE_Pressed);
            MantleVerificationSeconds = 0.0f;
            MantleVerification = EMantleVerification::BlockedHold;
            UE_LOG(LogMantleVerification, Display, TEXT("Passage ledge stopped walking, a short Space jump and a crouch jump."));
        }
        break;
    case EMantleVerification::BlockedHold:
        if (Player.IsMantling() || Location.X > Front)
        {
            Fail(TEXT("the occupied landing was accepted"));
        }
        else if (MantleVerificationSeconds > 0.8f && bGrounded && !Player.CanMantle())
        {
            Space(IE_Released);
            if (AActor* Blocker = MantleTestBlocker.Get())
            {
                Blocker->Destroy();
            }
            MantleVerification = EMantleVerification::Hold;
            UE_LOG(LogMantleVerification, Display, TEXT("Passage climb rejected an obstructed landing."));
        }
        break;
    case EMantleVerification::Hold:
        if (bGrounded && Player.CanMantle())
        {
            Space(IE_Pressed);
            MantleVerification = EMantleVerification::PullUp;
        }
        break;
    case EMantleVerification::PullUp:
        if (Player.IsMantling())
        {
            Space(IE_Released);
            MantleVerification = EMantleVerification::WaitForTop;
        }
        break;
    case EMantleVerification::WaitForTop:
        if (!Player.IsMantling() && bGrounded && Location.X >= Front + 34.0 && Location.Z > Top)
        {
            if (bCaptureRoute)
            {
                FScreenshotRequest::RequestScreenshot(TEXT("Vanguard-Climb-Top"), false, false);
            }
            UE_LOG(LogMantleVerification, Display, TEXT("Passage climb verified: held Space and landed on top."));
            MantleVerification = EMantleVerification::Done;
        }
        break;
    case EMantleVerification::Done:
        break;
    }
}
#endif
