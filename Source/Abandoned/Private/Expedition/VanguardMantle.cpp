#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
constexpr double ReachPeakSeconds = 0.055;
constexpr double ReachSeconds = 0.12;
constexpr double ReleaseStartSeconds = 0.27;
constexpr double ReleaseEndSeconds = 0.51;
constexpr float GripLightFraction = 0.001f;
const FVector GloveWrist(-7.5, 0.0, 2.5);
constexpr double SleeveMeshLength = 26.0;
}

void AVanguardCharacter::CreateMantleVisuals()
{
    const auto MakePart = [this](const FName& Name)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(GetRootComponent());
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->SetOnlyOwnerSee(true);
        Part->SetCastShadow(false);
        Part->SetVisibility(false);
        return Part;
    };
    MantleGloves[0] = MakePart(TEXT("MantleGloveL"));
    MantleGloves[1] = MakePart(TEXT("MantleGloveR"));
    MantleSleeves[0] = MakePart(TEXT("MantleSleeveL"));
    MantleSleeves[1] = MakePart(TEXT("MantleSleeveR"));
}

void AVanguardCharacter::LoadMantleVisuals()
{
    const UVanguardMantleVisuals& Visuals = *GetDefault<UVanguardMantleVisuals>();
    UMaterialInterface* Finish = Visuals.Finish.LoadSynchronous();
    const auto SetPart = [Finish](UStaticMeshComponent& Component, UStaticMesh* PartMesh)
    {
        Component.SetStaticMesh(PartMesh);
        if (Finish)
        {
            Component.SetMaterial(0, Finish);
        }
    };
    SetPart(*MantleGloves[0], Visuals.LeftGlove.LoadSynchronous());
    SetPart(*MantleGloves[1], Visuals.RightGlove.LoadSynchronous());
    UStaticMesh* Sleeve = Visuals.Sleeve.LoadSynchronous();
    SetPart(*MantleSleeves[0], Sleeve);
    SetPart(*MantleSleeves[1], Sleeve);
}

void AVanguardCharacter::UpdateMantleHands()
{
    const float FlashlightIntensity = GetDefault<UVanguardFlashlightSettings>()->IntensityLumens;
    if (!ActiveMantleHandhold || MantleSeconds >= ReleaseEndSeconds)
    {
        ShoulderLight->SetIntensity(FlashlightIntensity);
        for (int Hand = 0; Hand < 2; ++Hand)
        {
            MantleGloves[Hand]->SetVisibility(false);
            MantleSleeves[Hand]->SetVisibility(false);
        }
        return;
    }

    const FQuat View = GetViewRotation().Quaternion();
    const FVector Eye = Camera->GetComponentLocation();
    const double GripWeight = MantleSeconds < ReleaseStartSeconds
        ? FMath::SmoothStep(ReachPeakSeconds, ReachSeconds, MantleSeconds)
        : 1.0 - FMath::SmoothStep(ReleaseStartSeconds, ReleaseEndSeconds, MantleSeconds);
    const double RaiseWeight = MantleSeconds <= ReachSeconds
        ? FMath::SmoothStep(0.0, ReachPeakSeconds, MantleSeconds)
        : 0.0;
    ShoulderLight->SetIntensity(FlashlightIntensity * FMath::Lerp(1.0f, GripLightFraction,
        static_cast<float>(GripWeight)));
    const auto& Handhold = *ActiveMantleHandhold;
    for (int Hand = 0; Hand < 2; ++Hand)
    {
        const double Side = Hand == 0 ? -1.0 : 1.0;
        const FVector Rest(33.0, Side * 24.0, -25.0);
        const FVector Ready(39.0, Side * 18.0, -12.0);
        const FVector Reach = Eye + View.RotateVector(FMath::Lerp(Rest, Ready, RaiseWeight));
        const FVector Pose = FMath::Lerp(Reach, Handhold.Edge[Hand], GripWeight);
        const FQuat Rotation = FQuat::Slerp(View, Handhold.Orientation, GripWeight);
        MantleGloves[Hand]->SetWorldLocationAndRotation(Pose, Rotation);
        MantleGloves[Hand]->SetVisibility(true);

        const FVector Wrist = Pose + Rotation.RotateVector(GloveWrist);
        const FVector Elbow = Eye + View.RotateVector(FVector(6.0, Side * 23.0, -44.0));
        const FVector SleeveDirection = Elbow - Wrist;
        const FQuat SleeveRotation = FRotationMatrix::MakeFromXZ(SleeveDirection, Rotation.GetUpVector()).ToQuat();
        const double LengthScale = FMath::Clamp(SleeveDirection.Size() / SleeveMeshLength, 0.65, 1.3);
        MantleSleeves[Hand]->SetWorldTransform(FTransform(SleeveRotation, Wrist, FVector(LengthScale, 1.0, 1.0)));
        MantleSleeves[Hand]->SetVisibility(true);
    }
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
            UpdateMantleHands();
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
        UpdateMantleHands();
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
    if (MantleGloves[0]->GetStaticMesh() && MantleGloves[1]->GetStaticMesh() && MantleSleeves[0]->GetStaticMesh())
    {
        ActiveMantleHandhold = Abandoned::Infrastructure::Expedition::FindMantleHandhold(*this, *Path);
    }
    UCharacterMovementComponent& Movement = *GetCharacterMovement();
    Movement.StopMovementImmediately();
    Movement.DisableMovement();
    UpdateMantleHands();
}

void AVanguardCharacter::FinishMantle()
{
    ActiveMantle.reset();
    ActiveMantleHandhold.reset();
    UpdateMantleHands();
    bMantleAvailable = false;
    ConsumeMovementInputVector();
    UCharacterMovementComponent& Movement = *GetCharacterMovement();
    Movement.StopMovementImmediately();
    Movement.SetMovementMode(MOVE_Falling);
}
