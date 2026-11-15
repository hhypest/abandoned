#include "TrainingChamber.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Player/MovementProfile.h"
#include "World/Blockout.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrainingChamber, Log, All);

namespace
{
using namespace Abandoned::Blockout;

constexpr double CubeSize = 75.0;
constexpr float CubeMass = 30.0f;
constexpr double PlateTop = 12.0;
constexpr float PlateSettleSeconds = 0.1f;
constexpr double DoorTravel = 280.0;
constexpr double ExitX = 1375.0;
constexpr double Reach = 280.0;
constexpr double HoldDistance = 165.0;
const FVector Spawn(0.0, 0.0, Abandoned::MovementProfile::StandingHalfHeight + 2.0);
const FVector CubeStart(475.0, 115.0, CubeSize * 0.5 + 3.0);
const FVector PlateCenter(845.0, -230.0, PlateTop * 0.5);
const FVector DoorClosed(1200.0, 0.0, 130.0);

UPointLightComponent* AddLight(UWorld& World, FVector Position, float Intensity, float Radius)
{
    APointLight* Lamp = World.SpawnActor<APointLight>(Position, FRotator::ZeroRotator);
    if (!Lamp)
    {
        return nullptr;
    }
    UPointLightComponent* Light = CastChecked<UPointLightComponent>(Lamp->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetCastShadows(false);
    Light->SetAttenuationRadius(Radius);
    Light->SetIntensity(Intensity);
    return Light;
}

void BuildRoom(UWorld& World, UStaticMesh& Mesh)
{
    struct FBlock { FVector Center; FVector Size; };
    const FBlock Room[] = {
        {{725.0,    0.0, -30.0}, {1950.0, 1200.0, 60.0}},
        {{725.0,    0.0, 580.0}, {1950.0, 1200.0, 40.0}},
        {{-225.0,   0.0, 280.0}, {  50.0, 1200.0, 560.0}},
        {{ 500.0, 625.0, 280.0}, {1500.0,   50.0, 560.0}},
        {{ 500.0,-625.0, 280.0}, {1500.0,   50.0, 560.0}},
        {{1200.0, 350.0, 280.0}, {  50.0,  500.0, 560.0}},
        {{1200.0,-350.0, 280.0}, {  50.0,  500.0, 560.0}},
        {{1200.0,   0.0, 550.0}, {  50.0,  200.0, 20.0}},
        {{1460.0, 125.0, 280.0}, { 550.0,   50.0, 560.0}},
        {{1460.0,-125.0, 280.0}, { 550.0,   50.0, 560.0}},
        {{1700.0,   0.0, 280.0}, {  50.0,  300.0, 560.0}},
    };
    for (const FBlock& Block : Room)
    {
        SpawnBox(World, Mesh, Block.Center, Block.Size);
    }

    SpawnBox(World, Mesh, FVector(1025.0, -230.0, 2.0), FVector(360.0, 16.0, 4.0),
        EComponentMobility::Static, TEXT("NoCollision"));
    SpawnBox(World, Mesh, FVector(1172.0, -170.0, 2.0), FVector(16.0, 120.0, 4.0),
        EComponentMobility::Static, TEXT("NoCollision"));
}
}

ATrainingChamberGameMode::ATrainingChamberGameMode()
{
    DefaultPawnClass = ATrainingChamberCharacter::StaticClass();
    HUDClass = ATrainingChamberHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void ATrainingChamberGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    if (!ErrorMessage.IsEmpty())
    {
        return;
    }

    UStaticMesh* Mesh = Abandoned::Blockout::LoadCube();
    if (!Mesh)
    {
        ErrorMessage = TEXT("Training chamber requires the Engine BasicShapes cube.");
        return;
    }

    UWorld& World = *GetWorld();
    Abandoned::Blockout::ApplyGravity(World);
    BuildRoom(World, *Mesh);
    Cube = Abandoned::Blockout::SpawnBox(World, *Mesh, CubeStart, FVector(CubeSize),
        EComponentMobility::Movable, TEXT("PhysicsActor")).Collision;
    Door = Abandoned::Blockout::SpawnBox(World, *Mesh, DoorClosed, FVector(40.0, 198.0, 260.0),
        EComponentMobility::Movable, TEXT("BlockAllDynamic")).Collision;
    PlateVisual = Abandoned::Blockout::SpawnBox(World, *Mesh, PlateCenter, FVector(150.0, 150.0, PlateTop),
        EComponentMobility::Movable).Visual;

    if (!Cube || !Door || !PlateVisual)
    {
        ErrorMessage = TEXT("Could not create the cube, pressure plate or door.");
        return;
    }

    Cube->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Cube->SetMassOverrideInKg(NAME_None, CubeMass, true);
    Cube->SetLinearDamping(0.5f);
    Cube->SetAngularDamping(2.0f);
    Cube->SetUseCCD(true);
    Cube->SetSimulatePhysics(true);

    AddLight(World, FVector(180.0, 0.0, 380.0), 12000.0f, 1250.0f);
    AddLight(World, FVector(840.0, 0.0, 380.0), 15000.0f, 1250.0f);
    AddLight(World, FVector(1550.0, 0.0, 330.0), 6000.0f, 425.0f);
    PlateIndicator = AddLight(World, FVector(PlateCenter.X, PlateCenter.Y, 52.0), 600.0f, 190.0f);
    if (PlateIndicator)
    {
        PlateIndicator->SetLightColor(FLinearColor(1.0, 0.58, 0.12));
    }

#if WITH_DEV_AUTOMATION_TESTS
    bVerifyChamber = FParse::Param(FCommandLine::Get(), TEXT("VerifyTrainingChamber"));
#endif
    UE_LOG(LogTrainingChamber, Display, TEXT("Training chamber ready. Cube free, plate released, exit closed."));
}

void ATrainingChamberGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    if (IsValid(NewPlayer))
    {
        RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, Spawn));
        NewPlayer->SetInputMode(FInputModeGameOnly());
    }
}

void ATrainingChamberGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ATrainingChamberCharacter* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || !IsValid(Cube) || !IsValid(Door))
    {
        return;
    }

    const FVector Location = Player->GetActorLocation();
    if (Location.Z < -600.0)
    {
        ResetChamber(*Player);
        return;
    }
    const FVector CubeLocation = Cube->GetComponentLocation();
    if (CubeLocation.Z < -250.0 || !FMath::IsWithin(CubeLocation.X, -290.0, 1770.0) ||
        FMath::Abs(CubeLocation.Y) > 700.0)
    {
        ReturnCube(*Player);
    }

    PlateContactSeconds = CubeIsOnPlate(*Player)
        ? FMath::Min(PlateContactSeconds + DeltaSeconds, PlateSettleSeconds) : 0.0f;
    const bool bPressed = PlateContactSeconds >= PlateSettleSeconds;
    if (bPlatePressed != bPressed)
    {
        SetPlatePressed(bPressed);
    }
    FVector DoorPosition = Door->GetComponentLocation();
    const double TargetZ = DoorClosed.Z + (bPlatePressed ? DoorTravel : 0.0);
    const double Step = 350.0 * DeltaSeconds;
    DoorPosition.Z += FMath::Clamp(TargetZ - DoorPosition.Z, -Step, Step);
    Door->SetWorldLocation(DoorPosition, true);

    if (!bReachedExit && IsDoorOpen() && Location.X > ExitX && FMath::Abs(Location.Y) < 100.0)
    {
        bReachedExit = true;
        UE_LOG(LogTrainingChamber, Display, TEXT("Player reached the exit."));
    }

#if WITH_DEV_AUTOMATION_TESTS
    if (bVerifyChamber)
    {
        VerifyChamber(*Player, DeltaSeconds);
    }
#endif
}

bool ATrainingChamberGameMode::CubeIsOnPlate(const ATrainingChamberCharacter& Player) const
{
    const FBoxSphereBounds& Bounds = Cube->Bounds;
    const FVector Offset = Bounds.Origin - PlateCenter;
    const double BottomZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
    return !Player.IsHoldingCube() && FMath::Abs(Offset.X) < 65.0 && FMath::Abs(Offset.Y) < 65.0 &&
        FMath::IsNearlyEqual(BottomZ, PlateTop, 4.0) && FMath::Abs(Cube->GetPhysicsLinearVelocity().Z) < 30.0;
}

bool ATrainingChamberGameMode::IsDoorOpen() const
{
    return IsValid(Door) && Door->GetComponentLocation().Z >= DoorClosed.Z + DoorTravel - 2.0;
}

void ATrainingChamberGameMode::SetPlatePressed(bool bPressed)
{
    bPlatePressed = bPressed;
    PlateVisual->SetRelativeLocation(FVector(0.0, 0.0, bPlatePressed ? -4.0 : 0.0));
    if (PlateIndicator)
    {
        PlateIndicator->SetIntensity(bPlatePressed ? 3000.0f : 600.0f);
    }
    UE_LOG(LogTrainingChamber, Display, TEXT("Pressure plate %s."), bPlatePressed ? TEXT("pressed") : TEXT("released"));
}

void ATrainingChamberGameMode::ReturnCube(ATrainingChamberCharacter& Player)
{
    Player.ReleaseCube();
    if (IsValid(Cube))
    {
        Cube->SetWorldLocationAndRotation(CubeStart, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
        Cube->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Cube->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        Cube->WakeRigidBody();
    }
    UE_LOG(LogTrainingChamber, Display, TEXT("Cube returned to its start."));
}

void ATrainingChamberGameMode::ResetChamber(ATrainingChamberCharacter& Player)
{
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Spawn));
    ReturnCube(Player);
    PlateContactSeconds = 0.0f;
    if (bPlatePressed && IsValid(PlateVisual))
    {
        SetPlatePressed(false);
    }
    if (IsValid(Door))
    {
        Door->SetWorldLocation(DoorClosed, false, nullptr, ETeleportType::TeleportPhysics);
    }
    bReachedExit = false;
}

#if WITH_DEV_AUTOMATION_TESTS
void ATrainingChamberGameMode::VerifyChamber(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    VerificationElapsed += DeltaSeconds;
    const auto Advance = [this](EVerificationPhase Phase, const TCHAR* Message)
    {
        UE_LOG(LogTrainingChamber, Display, TEXT("Verification: %s"), Message);
        VerificationElapsed = 0.0f;
        VerificationPhase = Phase;
    };
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogTrainingChamber, Error, TEXT("Verification failed (phase %d): %s. Cube %s, door Z %.1f, plate %s."),
            static_cast<int32>(VerificationPhase), Reason, *Cube->GetComponentLocation().ToCompactString(),
            Door->GetComponentLocation().Z, bPlatePressed ? TEXT("pressed") : TEXT("released"));
        bVerifyChamber = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto AimFrom = [&Player](FVector Here, FVector Target)
    {
        const FVector Eye = Here + FVector(0.0, 0.0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Target - Eye).Rotation(), Here));
    };
    APlayerController* Controller = Cast<APlayerController>(Player.GetController());
    if (!Controller)
    {
        Fail(TEXT("Missing player controller"));
        return;
    }

    if (VerificationElapsed > 8.0f)
    {
        Fail(TEXT("Timed out waiting for a physical transition"));
        return;
    }

    switch (VerificationPhase)
    {
    case EVerificationPhase::Initial:
        if (VerificationElapsed >= 0.35f)
        {
            if (bPlatePressed || Door->GetComponentLocation() != DoorClosed)
            {
                Fail(TEXT("Door must start closed"));
                return;
            }
            Controller->SetControlRotation(FRotator::ZeroRotator);
            Controller->ConsoleCommand(TEXT("Input.+key Mouse2D X=0 Y=1"), false);
            Advance(EVerificationPhase::LookUp, TEXT("Closed door confirmed. Testing upward mouse movement."));
        }
        break;
    case EVerificationPhase::LookUp:
        if (VerificationElapsed >= 0.15f)
        {
            if (Controller->GetControlRotation().GetNormalized().Pitch <= 0.0)
            {
                Controller->ConsoleCommand(TEXT("Input.-key Mouse2D"), false);
                Fail(TEXT("Upward mouse movement did not raise the view"));
                return;
            }
            Controller->SetControlRotation(FRotator::ZeroRotator);
            Controller->ConsoleCommand(TEXT("Input.+key Mouse2D X=0 Y=-1"), false);
            Advance(EVerificationPhase::LookDown, TEXT("Upward mouse movement raised the view."));
        }
        break;
    case EVerificationPhase::LookDown:
        if (VerificationElapsed >= 0.15f)
        {
            Controller->ConsoleCommand(TEXT("Input.-key Mouse2D"), false);
            if (Controller->GetControlRotation().GetNormalized().Pitch >= 0.0)
            {
                Fail(TEXT("Downward mouse movement did not lower the view"));
                return;
            }
            UE_LOG(LogTrainingChamber, Display, TEXT("Verification: Downward mouse movement lowered the view."));
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Spawn.Z), Current);
            Player.Interact();
            if (!Player.IsHoldingCube())
            {
                Fail(TEXT("Pickup trace or physics handle missed the cube"));
                return;
            }
            const FVector Target(PlateCenter.X, PlateCenter.Y, PlateTop + CubeSize * 0.5 + 70.0);
            const double Height = Target.Z - (Spawn.Z + Abandoned::MovementProfile::CameraHeight);
            const double Forward = FMath::Sqrt(FMath::Square(HoldDistance) - FMath::Square(Height));
            AimFrom(FVector(Target.X - Forward, Target.Y, Spawn.Z), Target);
            Advance(EVerificationPhase::Carry, TEXT("Cube picked up with the gameplay trace and physics handle."));
        }
        break;
    case EVerificationPhase::Carry:
        if (!Player.IsHoldingCube() || bPlatePressed)
        {
            Fail(TEXT("The held cube must stay in the grip and leave the plate unpowered"));
            return;
        }
        if (FVector::DistSquared2D(Cube->GetComponentLocation(), PlateCenter) < FMath::Square(16.0) &&
            Cube->GetComponentLocation().Z > PlateTop + CubeSize * 0.5 &&
            Cube->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(30.0))
        {
            Player.Interact();
            Advance(EVerificationPhase::Open, TEXT("Held cube reached the plate and was released."));
        }
        break;
    case EVerificationPhase::Open:
        if (bPlatePressed && IsDoorOpen())
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(1020.0, 0.0, Spawn.Z)));
            Advance(EVerificationPhase::Cross, TEXT("Settled cube pressed the plate and opened the door."));
        }
        break;
    case EVerificationPhase::Cross:
        if (bReachedExit)
        {
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Spawn.Z), Current);
            Player.Interact();
            if (!Player.IsHoldingCube())
            {
                Fail(TEXT("Could not remove the cube from the plate"));
                return;
            }
            Advance(EVerificationPhase::Close, TEXT("Player walked through the door and picked the cube off the plate."));
        }
        else
        {
            Player.AddMovementInput(FVector::ForwardVector);
        }
        break;
    case EVerificationPhase::Close:
        if (!bPlatePressed && FMath::IsNearlyEqual(Door->GetComponentLocation().Z, DoorClosed.Z, 0.1))
        {
            Player.ResetChamber();
            if (bReachedExit || Player.IsHoldingCube() || FVector::DistSquared(CubeStart, Cube->GetComponentLocation()) > 1.0)
            {
                Fail(TEXT("Reset did not restore the initial chamber state"));
                return;
            }
            Cube->SetWorldLocation(FVector(CubeStart.X, CubeStart.Y, -420.0), false, nullptr, ETeleportType::TeleportPhysics);
            Advance(EVerificationPhase::Return, TEXT("Removing the cube closed the door. Reset restored player and cube."));
        }
        break;
    case EVerificationPhase::Return:
        if (FVector::DistSquared(CubeStart, Cube->GetComponentLocation()) < FMath::Square(10.0))
        {
            UE_LOG(LogTrainingChamber, Display, TEXT("Verification passed: lost cube returned."));
            bVerifyChamber = false;
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif

ATrainingChamberCharacter::ATrainingChamberCharacter()
{
    Grip = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("CubeGrip"));
    Grip->SetLinearStiffness(1800.0f);
    Grip->SetLinearDamping(180.0f);
    Grip->SetInterpolationSpeed(18.0f);
}

void ATrainingChamberCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Input.BindAction(MapAction(EKeys::E), ETriggerEvent::Started, this, &ATrainingChamberCharacter::Interact);
    Input.BindAction(MapAction(EKeys::R), ETriggerEvent::Started, this, &ATrainingChamberCharacter::ResetChamber);
}

ATrainingChamberGameMode* ATrainingChamberCharacter::GetChamber() const
{
    return GetWorld()->GetAuthGameMode<ATrainingChamberGameMode>();
}

UBoxComponent* ATrainingChamberCharacter::FindReachableCube() const
{
    const ATrainingChamberGameMode* Chamber = GetChamber();
    UBoxComponent* Candidate = Chamber ? Chamber->GetCube() : nullptr;
    if (!IsValid(Candidate))
    {
        return nullptr;
    }
    const FVector Origin = Camera->GetComponentLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(TrainingReach), false, this);
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + GetViewRotation().Vector() * Reach, ECC_Visibility, Query);
    return Hit.GetComponent() == Candidate ? Candidate : nullptr;
}

void ATrainingChamberCharacter::Interact()
{
    if (IsHoldingCube())
    {
        ReleaseCube();
        return;
    }

    if (UBoxComponent* Candidate = FindReachableCube())
    {
        Grip->GrabComponentAtLocationWithRotation(Candidate, NAME_None, Candidate->GetComponentLocation(),
            FRotator(0.0, GetViewRotation().Yaw, 0.0));
        if (IsHoldingCube())
        {
            Candidate->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
            Candidate->WakeRigidBody();
        }
    }
}

bool ATrainingChamberCharacter::IsHoldingCube() const
{
    return IsValid(Grip->GetGrabbedComponent());
}

void ATrainingChamberCharacter::ReleaseCube()
{
    UPrimitiveComponent* Held = Grip->GetGrabbedComponent();
    if (IsValid(Held))
    {
        Grip->ReleaseComponent();
        Held->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Held->WakeRigidBody();
    }
}

void ATrainingChamberCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const UPrimitiveComponent* Held = Grip->GetGrabbedComponent();
    if (!IsValid(Held))
    {
        return;
    }

    const FVector Origin = Camera->GetComponentLocation();
    const FVector Direction = GetViewRotation().Vector();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(TrainingHold), false, this);
    Query.AddIgnoredActor(Held->GetOwner());
    FHitResult Hit;
    const bool bWall = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * HoldDistance, ECC_Visibility, Query);
    const double Distance = bWall ? FMath::Clamp(Hit.Distance - CubeSize * 0.5, 0.0, HoldDistance) : HoldDistance;
    if (Distance < 75.0 || FVector::DistSquared(Origin, Held->GetComponentLocation()) > FMath::Square(2.0 * Reach))
    {
        ReleaseCube();
        return;
    }
    Grip->SetTargetLocationAndRotation(Origin + Direction * Distance, FRotator(0.0, GetViewRotation().Yaw, 0.0));
}

void ATrainingChamberCharacter::ResetChamber()
{
    if (ATrainingChamberGameMode* Chamber = GetChamber())
    {
        Chamber->ResetChamber(*this);
    }
}

void ATrainingChamberCharacter::UnPossessed()
{
    ReleaseCube();
    Super::UnPossessed();
}

void ATrainingChamberCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseCube();
    Super::EndPlay(Reason);
}

void ATrainingChamberHUD::DrawHUD()
{
    Super::DrawHUD();
    const ATrainingChamberCharacter* Player = Cast<ATrainingChamberCharacter>(GetOwningPawn());
    const ATrainingChamberGameMode* Chamber = GetWorld()->GetAuthGameMode<ATrainingChamberGameMode>();
    if (!Player || !Chamber)
    {
        return;
    }

    DrawText(TEXT("TRAINING CHAMBER"), FLinearColor::White, 24.0f, 24.0f, nullptr, 1.2f);
    DrawText(TEXT("WASD / mouse   Space jump   E pick up / drop   R reset"), FLinearColor::White, 24.0f, 52.0f);
    const TCHAR* Status = Chamber->HasReachedExit() ? TEXT("Exit reached") :
        (Chamber->IsDoorOpen() ? TEXT("Exit open") :
        (Chamber->IsPlatePressed() ? TEXT("Exit opening") : TEXT("Put the cube on the pressure plate")));
    DrawText(Status, FLinearColor(1.0, 0.68, 0.23), 24.0f, 76.0f);

    const float CenterX = Canvas->ClipX * 0.5f;
    const float CenterY = Canvas->ClipY * 0.5f;
    DrawRect(FLinearColor::White, CenterX - 1.0f, CenterY - 1.0f, 3.0f, 3.0f);
    const TCHAR* Prompt = Player->IsHoldingCube() ? TEXT("[E] Drop cube") :
        (Player->FindReachableCube() ? TEXT("[E] Pick up cube") : nullptr);
    if (Prompt)
    {
        DrawText(Prompt, FLinearColor::White, CenterX - 54.0f, CenterY + 22.0f);
    }
}
