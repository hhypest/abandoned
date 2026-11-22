#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "Player/FosTechBody.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
constexpr double ReachSeconds = 0.12;
constexpr double ReleaseStartSeconds = 0.27;
constexpr double ReleaseEndSeconds = 0.51;
constexpr float GripLightFraction = 0.001f;
}

void AVanguardCharacter::UpdateSuitPose(float DeltaSeconds)
{
    constexpr float RaiseSeconds = 0.18f;
    WristRaiseWeight = FMath::FInterpConstantTo(WristRaiseWeight, IsWristTerminalOpen() ? 1.f : 0.f,
        DeltaSeconds, 1.f / RaiseSeconds);
    const float GripWeight = ActiveMantleHandhold ? static_cast<float>(MantleSeconds < ReleaseStartSeconds
        ? FMath::SmoothStep(0.0, ReachSeconds, MantleSeconds)
        : 1.0 - FMath::SmoothStep(ReleaseStartSeconds, ReleaseEndSeconds, MantleSeconds)) : 0.f;
    const float Flashlight = GetDefault<UVanguardFlashlightSettings>()->IntensityLumens;
    ShoulderLight->SetIntensity(Flashlight * FMath::Lerp(1.f, GripLightFraction, GripWeight));
    const FTransform Eye(GetViewRotation(), Camera->GetComponentLocation());
    SuitBody->PoseArms(Eye, WristRaiseWeight, ActiveMantleHandhold ? ActiveMantleHandhold->Edge : nullptr,
        ActiveMantleHandhold ? ActiveMantleHandhold->Orientation : FQuat::Identity, GripWeight);
}

void AVanguardCharacter::Jump()
{
    if (!IsMantling())
    {
        MantleRequest.Press();
        Super::Jump();
    }
}

void AVanguardCharacter::StopJumping()
{
    MantleRequest.Release();
    Super::StopJumping();
}

void AVanguardCharacter::UnPossessed()
{
    CloseWristTerminal();
    StopJumping();
    if (IsMantling())
    {
        FinishMantle();
    }
    Super::UnPossessed();
}

void AVanguardCharacter::UpdateMantle(float DeltaSeconds, std::optional<double> SupportHeight)
{
    if (ActiveMantle)
    {
        if (!ActiveMantle->Ledge.IsValid())
        {
            FinishMantle();
            return;
        }
        constexpr double LiftSeconds = 0.30;
        constexpr double SlideSeconds = 0.24;
        const double Reach = ActiveMantleHandhold ? ReachSeconds : 0.0;
        const double PreviousMotion = MantleSeconds - Reach;
        MantleSeconds += DeltaSeconds;
        const double MotionSeconds = MantleSeconds - Reach;
        if (MotionSeconds <= 0.0)
        {
            return;
        }
        const auto MoveTo = [this](const FVector& Next)
        {
            FHitResult Hit;
            SetActorLocation(Next, true, &Hit);
            return !Hit.bBlockingHit && GetActorLocation().Equals(Next, 0.1);
        };
        if (PreviousMotion < LiftSeconds && MotionSeconds >= LiftSeconds && !MoveTo(ActiveMantle->Raised))
        {
            FinishMantle();
            return;
        }
        const double LiftAlpha = FMath::Clamp(MotionSeconds / LiftSeconds, 0.0, 1.0);
        const double SlideAlpha = FMath::Clamp((MotionSeconds - LiftSeconds) / SlideSeconds, 0.0, 1.0);
        const FVector Next = LiftAlpha < 1.0
            ? FMath::Lerp(ActiveMantle->Start, ActiveMantle->Raised, FMath::SmoothStep(0.0, 1.0, LiftAlpha))
            : FMath::Lerp(ActiveMantle->Raised, ActiveMantle->Landing, FMath::SmoothStep(0.0, 1.0, SlideAlpha));
        if (!MoveTo(Next) || SlideAlpha >= 1.0)
        {
            FinishMantle();
            return;
        }
        return;
    }

    MantleRequest.Advance(DeltaSeconds);
    const auto Path = SupportHeight ? Abandoned::Infrastructure::Expedition::FindMantlePath(*this, *SupportHeight)
                                    : std::nullopt;
    bMantleAvailable = Path.has_value();
    if (!Path || !MantleRequest.IsReady())
    {
        return;
    }
    MantleRequest.Consume();
    CloseWristTerminal();
    MantleSeconds = 0.0;
    ActiveMantle = Path;
    ActiveMantleHandhold = Abandoned::Infrastructure::Expedition::FindMantleHandhold(*this, *Path);
    UCharacterMovementComponent& Movement = *GetCharacterMovement();
    Movement.StopMovementImmediately();
    Movement.DisableMovement();
}

void AVanguardCharacter::FinishMantle()
{
    ActiveMantle.reset();
    ActiveMantleHandhold.reset();
    bMantleAvailable = false;
    ConsumeMovementInputVector();
    UCharacterMovementComponent& Movement = *GetCharacterMovement();
    Movement.StopMovementImmediately();
    Movement.SetMovementMode(MOVE_Falling);
}
