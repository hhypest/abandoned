#include "Player/PuzzleCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Domain/Expedition/Dev20.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "Expedition/Dev20Actor.h"
#include "GameFramework/GameModeBase.h"
#include "Infrastructure/Puzzles/CubePlacementComponent.h"
#include "Infrastructure/Puzzles/ObjectCarryComponent.h"
#include "Infrastructure/Puzzles/PuzzleDeviceCommands.h"
#include "Infrastructure/Puzzles/PuzzleInteractionSettings.h"
#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"
#include "Puzzles/PuzzleCube.h"
#include "Player/AbandonedTestController.h"
#include "Player/FosTechBody.h"

namespace
{
constexpr double Reach = 280.0;
}

APuzzleCharacter::APuzzleCharacter()
{
    Grip = CreateDefaultSubobject<UObjectCarryComponent>(TEXT("CubeGrip"));
    Grip->LoadChanged.BindUObject(this, &APuzzleCharacter::SetMovementLoad);
    Placement = CreateDefaultSubobject<UCubePlacementComponent>(TEXT("PlacementGhost"));
    Placement->SetupAttachment(GetRootComponent());
}

void APuzzleCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Input.BindAction(MapAction(EKeys::E), ETriggerEvent::Started, this, &APuzzleCharacter::Interact);
    Input.BindAction(MapAction(EKeys::LeftMouseButton), ETriggerEvent::Started, this, &APuzzleCharacter::ThrowHeldObject);
    Input.BindAction(MapAction(EKeys::R), ETriggerEvent::Started, this, &APuzzleCharacter::ResetChamber);
}

FVector APuzzleCharacter::InteractionRayEnd() const
{
    return Camera->GetComponentLocation() + GetViewRotation().Vector() * Reach;
}

UPrimitiveComponent* APuzzleCharacter::TraceInteractable() const
{
    const FVector Origin = Camera->GetComponentLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PuzzleReach), false, this);
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, Origin, InteractionRayEnd(), ECC_Visibility, Query);
    return Hit.GetComponent();
}

const APuzzleCube* APuzzleCharacter::GetSightedCube() const
{
    const FVector Origin = Camera->GetComponentLocation();
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + GetViewRotation().Vector() * 10000,
        ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(BallastReadout), false, this));
    return Cast<APuzzleCube>(Hit.GetActor());
}

const TCHAR* APuzzleCharacter::GetPlacementPrompt() const
{
    if (GetHeldDev20())
    {
        return TEXT("[E] Поставить DEV-20   [ЛКМ] Выбросить…");
    }
    if (GetHeldCube())
    {
        return Placement->PlacementPrompt();
    }
    const UPrimitiveComponent* Target = TraceInteractable();
    if (Abandoned::Infrastructure::Puzzles::IsDoorClamped(Target))
    {
        return TEXT("Object clamped");
    }
    const APuzzleCube* Cube = Target ? Cast<APuzzleCube>(Target->GetOwner()) : nullptr;
    if (!Cube)
    {
        if (Target && Cast<ADev20Actor>(Target->GetOwner()))
        {
            return TEXT("[E] Pick up DEV-20 (34 kg)");
        }
        const auto* Devices = Cast<IPuzzleDeviceCommands>(GetWorld()->GetAuthGameMode());
        return Target && Devices ? Devices->DevicePrompt(*Target) : nullptr;
    }
    return Cube->GetMassKg() == 5.0f ? TEXT("[E] Поднять малый куб") : TEXT("[E] Поднять большой куб");
}

void APuzzleCharacter::Interact()
{
    if (APuzzleCube* Cube = GetHeldCube())
    {
        Placement->PlaceOrDrop(*Cube, *Grip, Camera->GetComponentLocation(), InteractionRayEnd());
        return;
    }
    auto* Devices = Cast<IPuzzleDeviceCommands>(GetWorld()->GetAuthGameMode());
    if (UBoxComponent* Held = GetHeldBody())
    {
        ReleaseHeldObject();
        if (Devices)
        {
            Devices->PlaceDevice(*this, *Held);
        }
        return;
    }

    UPrimitiveComponent* Target = TraceInteractable();
    if (Target && Devices && Devices->UseDevice(*this, *Target))
    {
        return;
    }
    if (ADev20Actor* Unit = Target ? Cast<ADev20Actor>(Target->GetOwner()) : nullptr)
    {
        GrabBody(*Unit->GetBody(), Abandoned::Domain::Expedition::Dev20MassKg);
        return;
    }
    if (APuzzleCube* Candidate = Target ? Cast<APuzzleCube>(Target->GetOwner()) : nullptr;
        Candidate && Candidate->GetMassKg() <= Abandoned::Domain::Puzzles::MaximumCarryKg)
    {
        Candidate->ReleaseSnapConstraints();
        GrabBody(*Candidate->GetBody(), Candidate->GetMassKg());
    }
}

bool APuzzleCharacter::GrabBody(UBoxComponent& Body, float MassKg)
{
    return Grip->TryGrab(Body, MassKg);
}

UBoxComponent* APuzzleCharacter::GetHeldBody() const
{
    return Grip->HeldBody();
}

bool APuzzleCharacter::TransitHeldBody(const FTransform& Through)
{
    return Grip->Transit(Through);
}

APuzzleCube* APuzzleCharacter::GetHeldCube() const
{
    const UBoxComponent* Held = GetHeldBody();
    return IsValid(Held) ? Cast<APuzzleCube>(Held->GetOwner()) : nullptr;
}

ADev20Actor* APuzzleCharacter::GetHeldDev20() const
{
    const UBoxComponent* Held = GetHeldBody();
    return IsValid(Held) ? Cast<ADev20Actor>(Held->GetOwner()) : nullptr;
}

void APuzzleCharacter::ReleaseHeldObject()
{
    Placement->ClearPreview();
    Grip->ReleaseComponent();
}

void APuzzleCharacter::ThrowHeldObject()
{
    if (ADev20Actor* Unit = GetHeldDev20())
    {
        if (auto* Local = Cast<AAbandonedTestController>(GetController()))
        {
            Local->OfferDev20Discard(*Unit);
        }
        return;
    }
    Grip->Throw(GetVelocity(), GetViewRotation().Vector() * GetDefault<UPuzzleInteractionSettings>()->GetThrowImpulse());
    Placement->UpdatePreview(GetHeldCube(), Camera->GetComponentLocation(), InteractionRayEnd());
}

void APuzzleCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    FVector Origin = Camera->GetComponentLocation();
    double Distance = HoldDistanceCm;
    double MinimumDistance = 75.0;
    FRotator CarryView = GetViewRotation();
    const APuzzleCube* Cube = GetHeldCube();
    if (Cube && SuitBody->HasCubeGripPose())
    {
        // A downward glance should not send the cube through the wearer's legs.
        Origin = GetActorLocation() + FVector::UpVector * (Camera->GetRelativeLocation().Z - 35.0);
        CarryView.Pitch = FMath::Clamp(CarryView.GetNormalized().Pitch, -12.0, 8.0);
        Distance = 33.0 + Cube->GetHalfSize();
        MinimumDistance = 30.0 + Cube->GetHalfSize();
        Placement->AimDistance = HoldDistanceCm;
    }
    Grip->UpdateHold(Origin, CarryView, Distance, Reach, MinimumDistance);
    SuitBody->PoseCube(GetHeldCube() ? GetHeldBody() : nullptr);
    Placement->UpdatePreview(GetHeldCube(), Camera->GetComponentLocation(), InteractionRayEnd());
}

void APuzzleCharacter::UnPossessed()
{
    ReleaseHeldObject();
    Super::UnPossessed();
}

void APuzzleCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseHeldObject();
    Super::EndPlay(Reason);
}

void APuzzleCharacter::ResetChamber()
{
    if (auto* Devices = Cast<IPuzzleDeviceCommands>(GetWorld()->GetAuthGameMode()))
    {
        Devices->ResetPuzzle(*this);
    }
}
