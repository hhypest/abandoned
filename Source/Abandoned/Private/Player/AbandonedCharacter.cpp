#include "AbandonedCharacter.h"
#include "Domain/Puzzles/BallastRules.h"
#include "MovementProfile.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"

using namespace Abandoned::MovementProfile;

AAbandonedCharacter::AAbandonedCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    bUseControllerRotationYaw = true;
    GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, StandingHalfHeight);

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    Camera->SetupAttachment(GetCapsuleComponent());
    Camera->SetRelativeLocation(FVector(0.0, 0.0, CameraHeight));
    Camera->bUsePawnControlRotation = true;

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxWalkSpeed = WalkSpeed;
    Movement->MaxWalkSpeedCrouched = SlowSpeed;
    Movement->MaxStepHeight = StepHeight;
    Movement->JumpZVelocity = JumpSpeed;
    Movement->AirControl = 0.25f;
    Movement->SetCrouchedHalfHeight(CrouchedHalfHeight);
    Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
    Movement->bOrientRotationToMovement = false;
}

void AAbandonedCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    APlayerController* PC = Cast<APlayerController>(GetController());
    ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!ensure(Input && Subsystem))
    {
        return;
    }

    RemoveInputMapping();
    InputMapping = NewObject<UInputMappingContext>(this);
    using EEvent = ETriggerEvent;
    Input->BindAction(MapAction(EKeys::W), EEvent::Triggered, this, &AAbandonedCharacter::Move, FVector::ForwardVector);
    Input->BindAction(MapAction(EKeys::S), EEvent::Triggered, this, &AAbandonedCharacter::Move, -FVector::ForwardVector);
    Input->BindAction(MapAction(EKeys::D), EEvent::Triggered, this, &AAbandonedCharacter::Move, FVector::RightVector);
    Input->BindAction(MapAction(EKeys::A), EEvent::Triggered, this, &AAbandonedCharacter::Move, -FVector::RightVector);
    Input->BindAction(MapAction(EKeys::Mouse2D, EInputActionValueType::Axis2D), EEvent::Triggered, this, &AAbandonedCharacter::Look);

    const UInputAction* JumpAction = MapAction(EKeys::SpaceBar);
    Input->BindAction(JumpAction, EEvent::Started, this, &AAbandonedCharacter::Jump);
    Input->BindAction(JumpAction, EEvent::Completed, this, &AAbandonedCharacter::StopJumping);
    Input->BindAction(JumpAction, EEvent::Canceled, this, &AAbandonedCharacter::StopJumping);

    const UInputAction* SprintAction = MapAction(EKeys::LeftShift);
    Input->BindAction(SprintAction, EEvent::Started, this, &AAbandonedCharacter::SetSprint, true);
    Input->BindAction(SprintAction, EEvent::Completed, this, &AAbandonedCharacter::SetSprint, false);
    Input->BindAction(SprintAction, EEvent::Canceled, this, &AAbandonedCharacter::SetSprint, false);

    const UInputAction* SlowAction = MapAction(EKeys::LeftAlt);
    Input->BindAction(SlowAction, EEvent::Started, this, &AAbandonedCharacter::SetSlow, true);
    Input->BindAction(SlowAction, EEvent::Completed, this, &AAbandonedCharacter::SetSlow, false);
    Input->BindAction(SlowAction, EEvent::Canceled, this, &AAbandonedCharacter::SetSlow, false);

    const UInputAction* CrouchAction = MapAction(EKeys::LeftControl);
    Input->BindAction(CrouchAction, EEvent::Started, this, &AAbandonedCharacter::StartCrouch);
    Input->BindAction(CrouchAction, EEvent::Completed, this, &AAbandonedCharacter::StopCrouch);
    Input->BindAction(CrouchAction, EEvent::Canceled, this, &AAbandonedCharacter::StopCrouch);

    BindAdditionalInput(*Input);
    Subsystem->AddMappingContext(InputMapping, 0);
    InputSubsystem = Subsystem;
}

UInputAction* AAbandonedCharacter::MapAction(const FKey& Key, EInputActionValueType Type)
{
    UInputAction* Action = NewObject<UInputAction>(InputMapping.Get());
    Action->ValueType = Type;
    InputMapping->MapKey(Action, Key);
    return Action;
}

void AAbandonedCharacter::UnPossessed()
{
    RemoveInputMapping();
    Super::UnPossessed();
}

void AAbandonedCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemoveInputMapping();
    Super::EndPlay(EndPlayReason);
}

void AAbandonedCharacter::RemoveInputMapping()
{
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InputSubsystem.Get(); Subsystem && InputMapping)
    {
        Subsystem->RemoveMappingContext(InputMapping);
    }
    InputSubsystem.Reset();
    InputMapping = nullptr;
}

void AAbandonedCharacter::Move(FVector LocalDirection)
{
    const FRotator Yaw(0.0, GetControlRotation().Yaw, 0.0);
    AddMovementInput(Yaw.RotateVector(LocalDirection));
}

void AAbandonedCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D Delta = Value.Get<FVector2D>();
    // UE's Mouse2D Y axis is already positive when the mouse moves up.
    constexpr double LookScale = 2.5;
    AddControllerYawInput(Delta.X * LookScale);
    AddControllerPitchInput(Delta.Y * LookScale);
}

void AAbandonedCharacter::SetSprint(bool bHeld)
{
    bSprintHeld = bHeld;
    UpdateSpeed();
}

void AAbandonedCharacter::SetSlow(bool bHeld)
{
    bSlowHeld = bHeld;
    UpdateSpeed();
}

void AAbandonedCharacter::UpdateSpeed()
{
    const float NormalSpeed = bSprintHeld ? SprintSpeed : WalkSpeed;
    GetCharacterMovement()->MaxWalkSpeed = (bSlowHeld ? SlowSpeed : NormalSpeed) * LoadSpeedScale;
    GetCharacterMovement()->MaxWalkSpeedCrouched = SlowSpeed * LoadSpeedScale;
}

void AAbandonedCharacter::SetMovementLoad(float MassKg)
{
    LoadSpeedScale = Abandoned::Domain::Puzzles::CarrySpeedScale(MassKg);
    UpdateSpeed();
}

void AAbandonedCharacter::StartCrouch()
{
    Crouch();
}

void AAbandonedCharacter::StopCrouch()
{
    UnCrouch();
}

void AAbandonedCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    Camera->SetRelativeLocation(FVector(0.0, 0.0, CameraHeight - HalfHeightAdjust));
}

void AAbandonedCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    Camera->SetRelativeLocation(FVector(0.0, 0.0, CameraHeight));
}

void AAbandonedCharacter::PlaceAt(const FTransform& Start)
{
    StopJumping();
    ConsumeMovementInputVector();
    SetBase(nullptr);

    const FRotator View = Start.Rotator();
    const FRotator Body(0.0, View.Yaw, 0.0);
    SetActorLocationAndRotation(Start.GetLocation(), Body, false, nullptr, ETeleportType::TeleportPhysics);
    if (AController* PC = GetController())
    {
        PC->SetControlRotation(View);
    }

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->StopMovementImmediately();
    Movement->SetMovementMode(MOVE_Falling);
    UnCrouch();
    Movement->UnCrouch(false);
}
