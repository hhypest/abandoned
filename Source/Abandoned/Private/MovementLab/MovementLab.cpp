#include "MovementLab.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/DirectionalLight.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogMovementLab, Log, All);

namespace
{
constexpr float CapsuleRadius = 30.0f;
constexpr float StandingHalfHeight = 90.0f;
constexpr float CrouchedHalfHeight = 55.0f;
constexpr float CameraHeight = 75.0f;
constexpr float WalkSpeed = 510.0f;
constexpr float SprintSpeed = 800.0f;
constexpr float SlowSpeed = 250.0f;
constexpr float GravityZ = -2000.0f;
constexpr float JumpSpeed = 600.0f;
constexpr double RunwayEnd = 1200.0;
constexpr double LaneSpacing = 2200.0;
constexpr double FallResetZ = -600.0;

struct FTrial
{
    const TCHAR* Title;
    int32 GapCm = 0;
    int32 LandingHeightCm = 0;
};

constexpr FTrial Trials[] = {
    {TEXT("Flat ground / jump height")},
    {TEXT("Step 25 cm"), 0, 25},
    {TEXT("Step 30 cm"), 0, 30},
    {TEXT("Step 35 cm"), 0, 35},
    {TEXT("Ledge 50 cm"), 0, 50},
    {TEXT("Ledge 70 cm"), 0, 70},
    {TEXT("Ledge 80 cm"), 0, 80},
    {TEXT("Ledge 90 cm"), 0, 90},
    {TEXT("Ledge 100 cm"), 0, 100},
    {TEXT("Ledge 120 cm"), 0, 120},
    {TEXT("Ledge 135 cm"), 0, 135},
    {TEXT("Ledge 150 cm"), 0, 150},
    {TEXT("Gap 400 cm"), 400},
    {TEXT("Gap 450 cm"), 450},
    {TEXT("Gap 500 cm"), 500},
    {TEXT("Gap 550 cm"), 550},
    {TEXT("Gap 600 cm"), 600},
    {TEXT("Gap 650 cm"), 650},
    {TEXT("Gap 700 cm"), 700},
    {TEXT("Gap 600 cm / landing -100 cm"), 600, -100},
};
constexpr int32 TrialCount = UE_ARRAY_COUNT(Trials);

const FName LandingTag(TEXT("MovementLabLanding"));

FTransform TrialStart(int32 Index)
{
    return FTransform(FRotator::ZeroRotator, FVector(0.0, Index * LaneSpacing, StandingHalfHeight + 2.0));
}

void AddPlatform(UWorld& World, UStaticMesh& Cube, double StartX, double EndX, double Y, double TopZ, bool bLanding)
{
    constexpr double Width = 700.0;
    constexpr double Thickness = 60.0;
    const FVector Size(EndX - StartX, Width, Thickness);
    const FVector Center((StartX + EndX) * 0.5, Y, TopZ - Thickness * 0.5);

    AActor* Platform = World.SpawnActor<AActor>();
    if (!ensure(Platform))
    {
        return;
    }

    UBoxComponent* Box = NewObject<UBoxComponent>(Platform);
    Platform->SetRootComponent(Box);
    Platform->AddInstanceComponent(Box);
    Box->InitBoxExtent(Size * 0.5);
    Box->SetWorldLocation(Center);
    Box->SetMobility(EComponentMobility::Static);
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();

    UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Platform);
    Platform->AddInstanceComponent(Visual);
    Visual->SetupAttachment(Box);
    Visual->SetMobility(EComponentMobility::Static);
    Visual->SetStaticMesh(&Cube);
    Visual->SetCollisionProfileName(TEXT("NoCollision"));
    Visual->SetRelativeScale3D(Size / 100.0);
    Visual->RegisterComponent();

    if (bLanding)
    {
        Platform->Tags.Add(LandingTag);
    }
}
}

AMovementLabGameMode::AMovementLabGameMode()
{
    DefaultPawnClass = AMovementLabCharacter::StaticClass();
    HUDClass = AMovementLabHUD::StaticClass();
}

void AMovementLabGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    if (!ErrorMessage.IsEmpty())
    {
        return;
    }

    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Cube)
    {
        ErrorMessage = TEXT("Movement Lab requires the Engine BasicShapes cube.");
        return;
    }

    UWorld& World = *GetWorld();
    AWorldSettings* Settings = World.GetWorldSettings();
    Settings->bGlobalGravitySet = true;
    Settings->GlobalGravityZ = GravityZ;
    Settings->bWorldGravitySet = false;

    for (int32 Index = 0; Index < TrialCount; ++Index)
    {
        const FTrial& Trial = Trials[Index];
        const double Y = TrialStart(Index).GetLocation().Y;
        if (Index == 0)
        {
            AddPlatform(World, *Cube, -500.0, 2800.0, Y, 0.0, false);
            continue;
        }

        AddPlatform(World, *Cube, -500.0, RunwayEnd, Y, 0.0, false);
        AddPlatform(World, *Cube, RunwayEnd + Trial.GapCm, 2800.0, Y, Trial.LandingHeightCm, true);
    }

    ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-50.0, -35.0, 0.0));
    if (Sun)
    {
        ULightComponent* Light = Sun->GetLightComponent();
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetIntensity(4.0f);
    }

    UE_LOG(LogMovementLab, Display, TEXT("Movement Lab ready: %d trials, gravity %.0f cm/s^2."), TrialCount, World.GetGravityZ());
}

void AMovementLabGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    if (IsValid(NewPlayer))
    {
        RestartPlayerAtTransform(NewPlayer, TrialStart(0));
        NewPlayer->SetInputMode(FInputModeGameOnly());
    }
}

AMovementLabCharacter::AMovementLabCharacter()
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
    Movement->MaxStepHeight = 30.0f;
    Movement->JumpZVelocity = JumpSpeed;
    Movement->AirControl = 0.25f;
    Movement->SetCrouchedHalfHeight(CrouchedHalfHeight);
    Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
    Movement->bOrientRotationToMovement = false;
}

void AMovementLabCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
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
    const auto MapAction = [this](FKey Key, EInputActionValueType Type = EInputActionValueType::Boolean)
    {
        UInputAction* Action = NewObject<UInputAction>(InputMapping.Get());
        Action->ValueType = Type;
        InputMapping->MapKey(Action, Key);
        return Action;
    };

    using EEvent = ETriggerEvent;
    Input->BindAction(MapAction(EKeys::W), EEvent::Triggered, this, &AMovementLabCharacter::Move, FVector::ForwardVector);
    Input->BindAction(MapAction(EKeys::S), EEvent::Triggered, this, &AMovementLabCharacter::Move, -FVector::ForwardVector);
    Input->BindAction(MapAction(EKeys::D), EEvent::Triggered, this, &AMovementLabCharacter::Move, FVector::RightVector);
    Input->BindAction(MapAction(EKeys::A), EEvent::Triggered, this, &AMovementLabCharacter::Move, -FVector::RightVector);
    Input->BindAction(MapAction(EKeys::Mouse2D, EInputActionValueType::Axis2D), EEvent::Triggered, this, &AMovementLabCharacter::Look);

    const UInputAction* JumpAction = MapAction(EKeys::SpaceBar);
    Input->BindAction(JumpAction, EEvent::Started, this, &AMovementLabCharacter::Jump);
    Input->BindAction(JumpAction, EEvent::Completed, this, &AMovementLabCharacter::StopJumping);
    Input->BindAction(JumpAction, EEvent::Canceled, this, &AMovementLabCharacter::StopJumping);

    const UInputAction* SprintAction = MapAction(EKeys::LeftShift);
    Input->BindAction(SprintAction, EEvent::Started, this, &AMovementLabCharacter::SetSprint, true);
    Input->BindAction(SprintAction, EEvent::Completed, this, &AMovementLabCharacter::SetSprint, false);
    Input->BindAction(SprintAction, EEvent::Canceled, this, &AMovementLabCharacter::SetSprint, false);

    const UInputAction* SlowAction = MapAction(EKeys::LeftAlt);
    Input->BindAction(SlowAction, EEvent::Started, this, &AMovementLabCharacter::SetSlow, true);
    Input->BindAction(SlowAction, EEvent::Completed, this, &AMovementLabCharacter::SetSlow, false);
    Input->BindAction(SlowAction, EEvent::Canceled, this, &AMovementLabCharacter::SetSlow, false);

    const UInputAction* CrouchAction = MapAction(EKeys::LeftControl);
    Input->BindAction(CrouchAction, EEvent::Started, this, &AMovementLabCharacter::StartCrouch);
    Input->BindAction(CrouchAction, EEvent::Completed, this, &AMovementLabCharacter::StopCrouch);
    Input->BindAction(CrouchAction, EEvent::Canceled, this, &AMovementLabCharacter::StopCrouch);

    Input->BindAction(MapAction(EKeys::Q), EEvent::Started, this, &AMovementLabCharacter::ChangeTrial, int32{-1});
    Input->BindAction(MapAction(EKeys::E), EEvent::Started, this, &AMovementLabCharacter::ChangeTrial, int32{1});
    Input->BindAction(MapAction(EKeys::R), EEvent::Started, this, &AMovementLabCharacter::ResetTrial);

    Subsystem->AddMappingContext(InputMapping, 0);
    InputSubsystem = Subsystem;
}

void AMovementLabCharacter::UnPossessed()
{
    RemoveInputMapping();
    Super::UnPossessed();
}

void AMovementLabCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemoveInputMapping();
    Super::EndPlay(EndPlayReason);
}

void AMovementLabCharacter::RemoveInputMapping()
{
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem = InputSubsystem.Get(); Subsystem && InputMapping)
    {
        Subsystem->RemoveMappingContext(InputMapping);
    }
    InputSubsystem.Reset();
    InputMapping = nullptr;
}

void AMovementLabCharacter::Move(FVector LocalDirection)
{
    const FRotator Yaw(0.0, GetControlRotation().Yaw, 0.0);
    AddMovementInput(Yaw.RotateVector(LocalDirection));
}

void AMovementLabCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D Delta = Value.Get<FVector2D>();
    AddControllerYawInput(Delta.X);
    AddControllerPitchInput(Delta.Y);
}

void AMovementLabCharacter::SetSprint(bool bHeld)
{
    bSprintHeld = bHeld;
    UpdateSpeed();
}

void AMovementLabCharacter::SetSlow(bool bHeld)
{
    bSlowHeld = bHeld;
    UpdateSpeed();
}

void AMovementLabCharacter::UpdateSpeed()
{
    const float NormalSpeed = bSprintHeld ? SprintSpeed : WalkSpeed;
    GetCharacterMovement()->MaxWalkSpeed = bSlowHeld ? SlowSpeed : NormalSpeed;
}

void AMovementLabCharacter::StartCrouch()
{
    Crouch();
}

void AMovementLabCharacter::StopCrouch()
{
    UnCrouch();
}

void AMovementLabCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    Camera->SetRelativeLocation(FVector(0.0, 0.0, CameraHeight - HalfHeightAdjust));
}

void AMovementLabCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
    Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
    Camera->SetRelativeLocation(FVector(0.0, 0.0, CameraHeight));
}

void AMovementLabCharacter::ChangeTrial(int32 Offset)
{
    TrialIndex = (TrialIndex + TrialCount + Offset) % TrialCount;
    Result.Reset();
    ResetTrial();
}

void AMovementLabCharacter::ResetTrial()
{
    bJumpInProgress = false;
    bReachedTarget = false;
    StopJumping();
    ConsumeMovementInputVector();
    SetBase(nullptr);

    const FTransform Start = TrialStart(TrialIndex);
    SetActorLocationAndRotation(Start.GetLocation(), Start.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
    if (AController* PC = GetController())
    {
        PC->SetControlRotation(Start.Rotator());
    }

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->StopMovementImmediately();
    Movement->SetMovementMode(MOVE_Falling);
    UnCrouch();
    Movement->UnCrouch(false);
}

void AMovementLabCharacter::OnJumped_Implementation()
{
    Super::OnJumped_Implementation();
    bJumpInProgress = true;
    LaunchPosition = GetActorLocation();
    LaunchSpeed = GetVelocity().Size2D();
    PeakZ = LaunchPosition.Z;
}

void AMovementLabCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    if (bJumpInProgress)
    {
        const AActor* Surface = Hit.GetActor();
        bReachedTarget = Surface && Surface->ActorHasTag(LandingTag);
        ReportJump(bReachedTarget ? TEXT("Landed on target") : TEXT("Landed on runway"));
    }
}

void AMovementLabCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FVector Location = GetActorLocation();
    if (bJumpInProgress)
    {
        PeakZ = FMath::Max(PeakZ, Location.Z);
    }
    if (!bReachedTarget && !bJumpInProgress && GetCharacterMovement()->IsMovingOnGround())
    {
        const UPrimitiveComponent* Base = GetMovementBase();
        const AActor* Surface = Base ? Base->GetOwner() : nullptr;
        if (Surface && Surface->ActorHasTag(LandingTag))
        {
            bReachedTarget = true;
            Result = TEXT("Reached target on foot");
            UE_LOG(LogMovementLab, Display, TEXT("%s: %s"), Trials[TrialIndex].Title, *Result);
        }
    }
    if (Location.Z < FallResetZ)
    {
        if (bJumpInProgress)
        {
            ReportJump(TEXT("Fell"));
        }
        else
        {
            Result = TEXT("Fell from platform");
        }
        ResetTrial();
    }
}

void AMovementLabCharacter::ReportJump(const TCHAR* Outcome)
{
    Result = FString::Printf(TEXT("%s | apex +%.1f cm | horizontal %.1f cm | takeoff %.0f cm/s"),
        Outcome, PeakZ - LaunchPosition.Z, FVector::Dist2D(LaunchPosition, GetActorLocation()), LaunchSpeed);
    UE_LOG(LogMovementLab, Display, TEXT("%s: %s"), Trials[TrialIndex].Title, *Result);
    bJumpInProgress = false;
}

void AMovementLabHUD::DrawHUD()
{
    Super::DrawHUD();
    const AMovementLabCharacter* Player = Cast<AMovementLabCharacter>(GetOwningPawn());
    if (!Player)
    {
        return;
    }

    const int32 Index = Player->GetTrialIndex();
    const FVector Location = Player->GetActorLocation();
    DrawText(FString::Printf(TEXT("Movement Lab   %d/%d   %s"), Index + 1, TrialCount, Trials[Index].Title),
        FLinearColor::White, 24.0f, 24.0f, nullptr, 1.4f);
    DrawText(TEXT("WASD / mouse   Space jump   Shift sprint   Alt slow   Ctrl crouch   Q / E trial   R reset"),
        FLinearColor::White, 24.0f, 56.0f);
    FString Measurements = FString::Printf(TEXT("Speed %.0f cm/s   Capsule center Z %.1f cm"),
        Player->GetVelocity().Size2D(), Location.Z);
    if (Index != 0)
    {
        Measurements += FString::Printf(TEXT("   Runway edge in %.0f cm"), FMath::Max(0.0, RunwayEnd - Location.X));
    }
    DrawText(Measurements, FLinearColor::White, 24.0f, 80.0f);
    DrawText(Player->GetResult(), FLinearColor::White, 24.0f, 104.0f);
}
