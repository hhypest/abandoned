#include "Expedition/VanguardRoute.h"

#include "GameFramework/CharacterMovementComponent.h"

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
        constexpr double LiftSeconds = 0.30;
        constexpr double SlideSeconds = 0.24;
        const double PreviousSeconds = MantleSeconds;
        MantleSeconds += DeltaSeconds;
        const auto MoveTo = [this](const FVector& Next)
        {
            FHitResult Hit;
            SetActorLocation(Next, true, &Hit);
            return !Hit.bBlockingHit && GetActorLocation().Equals(Next, 0.1);
        };
        if (PreviousSeconds < LiftSeconds && MantleSeconds >= LiftSeconds && !MoveTo(ActiveMantle->Raised))
        {
            FinishMantle();
            return;
        }
        const double LiftAlpha = FMath::Clamp(MantleSeconds / LiftSeconds, 0.0, 1.0);
        const double SlideAlpha = FMath::Clamp((MantleSeconds - LiftSeconds) / SlideSeconds, 0.0, 1.0);
        const FVector Next = LiftAlpha < 1.0
            ? FMath::Lerp(ActiveMantle->Start, ActiveMantle->Raised, FMath::SmoothStep(0.0, 1.0, LiftAlpha))
            : FMath::Lerp(ActiveMantle->Raised, ActiveMantle->Landing, FMath::SmoothStep(0.0, 1.0, SlideAlpha));
        if (!MoveTo(Next) || SlideAlpha >= 1.0)
        {
            FinishMantle();
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
    MantleSeconds = 0.0;
    ActiveMantle = Path;
    UCharacterMovementComponent& Movement = *GetCharacterMovement();
    Movement.StopMovementImmediately();
    Movement.DisableMovement();
}

void AVanguardCharacter::FinishMantle()
{
    ActiveMantle.reset();
    bMantleAvailable = false;
    ConsumeMovementInputVector();
    UCharacterMovementComponent& Movement = *GetCharacterMovement();
    Movement.StopMovementImmediately();
    Movement.SetMovementMode(MOVE_Falling);
}
