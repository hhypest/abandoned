#include "TrainingChamber.h"
#include "TrainingChamberLayout.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Blockout.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrainingChamber, Log, All);

namespace
{

constexpr float PlateSettleSeconds = 0.1f;
constexpr double DoorTravel = 280.0;
constexpr double Reach = 280.0;
constexpr double HoldDistance = 165.0;
constexpr double SnapRange = 75.0;
constexpr double SnapGap = 0.5;

UPointLightComponent* AddLight(UWorld& World, const FTrainingLight& Data)
{
    APointLight* Lamp = World.SpawnActor<APointLight>(Data.Position, FRotator::ZeroRotator);
    if (!Lamp)
    {
        return nullptr;
    }
    UPointLightComponent* Light = CastChecked<UPointLightComponent>(Lamp->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetCastShadows(false);
    Light->SetAttenuationRadius(Data.Radius);
    Light->SetIntensity(Data.Intensity);
    Light->SetLightColor(Data.Color);
    return Light;
}

void BuildRoom(UWorld& World, UStaticMesh& Mesh, const UTrainingChamberLayout& Data)
{
    for (const FTrainingBlock& Block : Data.Room)
    {
        Abandoned::Blockout::SpawnBox(World, Mesh, Block.Center, Block.Size, EComponentMobility::Static,
            Block.bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
    }
}
}

ATrainingChamberGameMode::ATrainingChamberGameMode()
{
    DefaultPawnClass = ATrainingChamberCharacter::StaticClass();
    HUDClass = ATrainingChamberHUD::StaticClass();
    PlayerControllerClass = AAbandonedTestController::StaticClass();
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

    const UTrainingChamberLayout* Layout = GetDefault<UTrainingChamberLayout>();
    if (!Layout->IsValid())
    {
        ErrorMessage = TEXT("Invalid training chamber data in DefaultGame.ini.");
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
    BuildRoom(World, *Mesh, *Layout);
    for (const FTrainingCubeSpawn& CubeSpawn : Layout->CubeSpawns)
    {
        APuzzleCube* Cube = World.SpawnActor<APuzzleCube>(CubeSpawn.Center, FRotator::ZeroRotator);
        if (!Cube)
        {
            ErrorMessage = TEXT("Could not create a training chamber cube.");
            return;
        }
        Cube->Initialize(CubeSpawn.Type, *Mesh);
        Cubes.Add(Cube);
    }
    Door = Abandoned::Blockout::SpawnBox(World, *Mesh, Layout->Door.Center, Layout->Door.Size,
        EComponentMobility::Movable, TEXT("BlockAllDynamic")).Collision;
    const Abandoned::Blockout::FBox PlateParts = Abandoned::Blockout::SpawnBox(
        World, *Mesh, Layout->Plate.Center, Layout->Plate.Size, EComponentMobility::Movable);
    Plate = PlateParts.Collision;
    PlateVisual = PlateParts.Visual;

    if (!Door || !Plate || !PlateVisual)
    {
        ErrorMessage = TEXT("Could not create the cube, pressure plate or door.");
        return;
    }

    for (const FTrainingLight& Light : Layout->Lights)
    {
        AddLight(World, Light);
    }
    PlateIndicator = AddLight(World, Layout->PlateLight);

#if WITH_DEV_AUTOMATION_TESTS
    bVerifyChamber = FParse::Param(FCommandLine::Get(), TEXT("VerifyTrainingChamber"));
#endif
    UE_LOG(LogTrainingChamber, Display, TEXT("Training chamber ready. %d cubes, plate released, exit closed."), Cubes.Num());
}

void ATrainingChamberGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    if (IsValid(NewPlayer) && IsValid(Door) && IsValid(Plate))
    {
        RestartPlayerAtTransform(NewPlayer, FTransform(FRotator::ZeroRotator, GetDefault<UTrainingChamberLayout>()->Spawn));
        NewPlayer->SetInputMode(FInputModeGameOnly());
    }
}

void ATrainingChamberGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    ATrainingChamberCharacter* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || !IsValid(Plate) || !IsValid(Door))
    {
        return;
    }

    const FVector Location = Player->GetActorLocation();
    if (Location.Z < -600.0)
    {
        ResetChamber(*Player);
        return;
    }
    const UTrainingChamberLayout* Layout = GetDefault<UTrainingChamberLayout>();
    const FBox CubeBounds = Layout->GetCubeBounds();
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        if (IsValid(Cubes[Index]) && !CubeBounds.IsInside(Cubes[Index]->GetActorLocation()))
        {
            ReturnCube(*Player, Index);
        }
    }

    PlateContactSeconds = GetPlateMass(*Player) >= Layout->PlateActivationMassKg
        ? FMath::Min(PlateContactSeconds + DeltaSeconds, PlateSettleSeconds) : 0.0f;
    const bool bPressed = PlateContactSeconds >= PlateSettleSeconds;
    if (bPlatePressed != bPressed)
    {
        SetPlatePressed(bPressed);
    }
    FVector DoorPosition = Door->GetComponentLocation();
    const double TargetZ = Layout->Door.Center.Z + (bPlatePressed ? DoorTravel : 0.0);
    const double Step = 350.0 * DeltaSeconds;
    DoorPosition.Z += FMath::Clamp(TargetZ - DoorPosition.Z, -Step, Step);
    Door->SetWorldLocation(DoorPosition, true);

    if (!bReachedExit && IsDoorOpen() && Layout->GetExitBounds().IsInside(Location))
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

float ATrainingChamberGameMode::GetPlateMass(const ATrainingChamberCharacter& Player) const
{
    const UTrainingChamberLayout* Layout = GetDefault<UTrainingChamberLayout>();
    const FVector AllowedOffset = Layout->Plate.Size * 0.5 - FVector(10.0);
    const APuzzleCube* Held = Player.GetHeldCube();
    const UPrimitiveComponent* PlayerSupport = Player.GetCharacterMovement()->IsMovingOnGround()
        ? Player.GetMovementBase() : nullptr;
    while (PlayerSupport)
    {
        const APuzzleCube* Cube = Cast<APuzzleCube>(PlayerSupport->GetOwner());
        if (!Cube || !Cube->GetSupport())
        {
            break;
        }
        PlayerSupport = Cube->GetSupport();
    }
    float MassKg = PlayerSupport == Plate ? Layout->PlayerMassKg : 0.0f;
    for (const APuzzleCube* Cube : Cubes)
    {
        if (!IsValid(Cube) || Cube == Held)
        {
            continue;
        }
        if (Cube->GetSupport() == Plate)
        {
            MassKg += Cube->GetStackMass();
            continue;
        }
        UBoxComponent* Body = Cube->GetBody();
        const FBoxSphereBounds& Bounds = Body->Bounds;
        const FVector Offset = Bounds.Origin - Layout->Plate.Center;
        const double BottomZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
        if (!Cube->GetSupport() && FMath::Abs(Offset.X) < AllowedOffset.X && FMath::Abs(Offset.Y) < AllowedOffset.Y &&
            FMath::IsNearlyEqual(BottomZ, Layout->GetPlateTop(), 4.0) &&
            FMath::Abs(Body->GetPhysicsLinearVelocity().Z) < 30.0)
        {
            MassKg += Cube->GetStackMass();
            if (PlayerSupport == Body)
            {
                MassKg += Layout->PlayerMassKg;
            }
        }
    }
    return MassKg;
}

bool ATrainingChamberGameMode::IsDoorOpen() const
{
    return IsValid(Door) && Door->GetComponentLocation().Z >= GetDefault<UTrainingChamberLayout>()->Door.Center.Z + DoorTravel - 2.0;
}

void ATrainingChamberGameMode::SetPlatePressed(bool bPressed)
{
    bPlatePressed = bPressed;
    PlateVisual->SetRelativeLocation(FVector(0.0, 0.0, bPlatePressed ? -4.0 : 0.0));
    if (PlateIndicator)
    {
        const UTrainingChamberLayout* Layout = GetDefault<UTrainingChamberLayout>();
        PlateIndicator->SetIntensity(bPlatePressed ? Layout->PressedLightIntensity : Layout->PlateLight.Intensity);
    }
    UE_LOG(LogTrainingChamber, Display, TEXT("Pressure plate %s."), bPlatePressed ? TEXT("pressed") : TEXT("released"));
}

void ATrainingChamberGameMode::ReturnCube(ATrainingChamberCharacter& Player, int32 Index)
{
    if (APuzzleCube* Cube = Cubes[Index]; IsValid(Cube))
    {
        if (Player.GetHeldCube() == Cube)
        {
            Player.ReleaseCube();
        }
        Cube->ResetPosition(GetDefault<UTrainingChamberLayout>()->CubeSpawns[Index].Center);
    }
    UE_LOG(LogTrainingChamber, Display, TEXT("Cube returned to its start."));
}

void ATrainingChamberGameMode::ResetChamber(ATrainingChamberCharacter& Player)
{
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, GetDefault<UTrainingChamberLayout>()->Spawn));
    Player.ReleaseCube();
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        ReturnCube(Player, Index);
    }
    PlateContactSeconds = 0.0f;
    if (bPlatePressed && IsValid(PlateVisual))
    {
        SetPlatePressed(false);
    }
    if (IsValid(Door))
    {
        Door->SetWorldLocation(GetDefault<UTrainingChamberLayout>()->Door.Center, false, nullptr, ETeleportType::TeleportPhysics);
    }
    bReachedExit = false;
}

#if WITH_DEV_AUTOMATION_TESTS
void ATrainingChamberGameMode::VerifyChamber(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    const UTrainingChamberLayout* Layout = GetDefault<UTrainingChamberLayout>();
    if (Cubes.Num() != 3 || !IsValid(Cubes[0]) || !IsValid(Cubes[1]) || !IsValid(Cubes[2]))
    {
        UE_LOG(LogTrainingChamber, Error, TEXT("Verification needs the three default training cubes."));
        bVerifyChamber = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    APuzzleCube& Small = *Cubes[0];
    APuzzleCube& Large = *Cubes[1];
    APuzzleCube& Step = *Cubes[2];
    UBoxComponent* Cube = Small.GetBody();
    VerificationElapsed += DeltaSeconds;
    const auto Advance = [this](EVerificationPhase Phase, const TCHAR* Message)
    {
        UE_LOG(LogTrainingChamber, Display, TEXT("Verification: %s"), Message);
        VerificationElapsed = 0.0f;
        VerificationPhase = Phase;
    };
    const auto Fail = [this, Cube, &Player](const TCHAR* Reason)
    {
        UE_LOG(LogTrainingChamber, Error, TEXT("Verification failed (phase %d): %s. Cube %s, door Z %.1f, plate %s, player %s."),
            static_cast<int32>(VerificationPhase), Reason, *Cube->GetComponentLocation().ToCompactString(),
            Door->GetComponentLocation().Z, bPlatePressed ? TEXT("pressed") : TEXT("released"),
            *Player.GetActorLocation().ToCompactString());
        bVerifyChamber = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto AimFrom = [&Player](FVector Here, FVector Target)
    {
        const FVector Eye = Here + FVector(0.0, 0.0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Target - Eye).Rotation(), Here));
    };
    const auto AimToHold = [Layout, &AimFrom](const FVector& Target, const FVector& Approach)
    {
        const double Height = Target.Z - (Layout->Spawn.Z + Abandoned::MovementProfile::CameraHeight);
        const double Forward = FMath::Sqrt(FMath::Max(0.0, FMath::Square(HoldDistance) - FMath::Square(Height)));
        const FVector Here = Target - Approach * Forward;
        AimFrom(FVector(Here.X, Here.Y, Layout->Spawn.Z), Target);
    };
    const FVector SmallStart = Layout->CubeSpawns[0].Center;
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
            if (bPlatePressed || Door->GetComponentLocation() != Layout->Door.Center)
            {
                Fail(TEXT("Door must start closed"));
                return;
            }
            if (Small.GetHalfSize() != 27.5 || Large.GetHalfSize() != 40.0 ||
                !FMath::IsNearlyEqual(Cube->GetMass(), 5.0f, 0.01f) ||
                !FMath::IsNearlyEqual(Large.GetBody()->GetMass(), 10.0f, 0.01f))
            {
                Fail(TEXT("Cube collision dimensions or Chaos masses are incorrect"));
                return;
            }
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(Layout->Plate.Center.X, Layout->Plate.Center.Y,
                Layout->GetPlateTop() + Abandoned::MovementProfile::StandingHalfHeight + 2.0)));
            Advance(EVerificationPhase::StandOnPlate, TEXT("Closed door confirmed. Standing on the plate."));
        }
        break;
    case EVerificationPhase::StandOnPlate:
        if (bPlatePressed)
        {
            if (Player.GetMovementBase() != Plate || !FMath::IsNearlyEqual(GetPlateMass(Player), Layout->PlayerMassKg))
            {
                Fail(TEXT("The grounded player should be the only load on the plate"));
                return;
            }
            Player.Jump();
            Advance(EVerificationPhase::JumpOffPlate, TEXT("Player weight pressed the plate."));
        }
        break;
    case EVerificationPhase::JumpOffPlate:
        if (Player.GetCharacterMovement()->IsFalling() && !bPlatePressed)
        {
            Player.StopJumping();
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
            Controller->ConsoleCommand(TEXT("Input.+key Mouse2D X=0 Y=1"), false);
            Advance(EVerificationPhase::LookUp, TEXT("Jumping unloaded the plate. Testing upward mouse movement."));
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
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Small)
            {
                Fail(TEXT("Pickup trace or physics handle missed the cube"));
                return;
            }
            const FVector Target(Layout->Plate.Center.X, Layout->Plate.Center.Y,
                Layout->GetPlateTop() + Small.GetHalfSize() + 42.0);
            AimToHold(Target, FVector::ForwardVector);
            Advance(EVerificationPhase::Carry, TEXT("Cube picked up with the gameplay trace and physics handle."));
        }
        break;
    case EVerificationPhase::Carry:
        if (Player.GetHeldCube() != &Small || bPlatePressed)
        {
            Fail(TEXT("The held cube must stay in the grip and leave the plate unpowered"));
            return;
        }
        if (FVector::DistSquared2D(Cube->GetComponentLocation(), Layout->Plate.Center) < FMath::Square(16.0) &&
            Cube->GetComponentLocation().Z > Layout->GetPlateTop() + Small.GetHalfSize() &&
            Cube->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(30.0))
        {
            if (!Player.PlacementGhost->IsVisible() || Player.PreviewSupport.Get() != Plate ||
                Player.PlacementGhost->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
            {
                Fail(TEXT("Expected a collision-free placement ghost on the plate"));
                return;
            }
            if (!bGhostScreenshotRequested && FParse::Param(FCommandLine::Get(), TEXT("CapturePlacementGhost")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("PlacementGhost"), false, false);
                bGhostScreenshotRequested = true;
                break;
            }
            const FVector Expected = Player.PlacementGhost->GetComponentLocation();
            Player.Interact();
            if (Small.GetSupport() != Plate || Cube->IsSimulatingPhysics() ||
                !Small.GetActorLocation().Equals(Expected, 0.1) || Player.PlacementGhost->IsVisible())
            {
                Fail(TEXT("Released cube did not snap to the plate"));
                return;
            }
            Advance(EVerificationPhase::Open, TEXT("Held cube snapped to the plate at the ghost position."));
        }
        break;
    case EVerificationPhase::Open:
        if (bPlatePressed && IsDoorOpen())
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator,
                FVector(Layout->Door.Center.X - 180.0, Layout->Door.Center.Y, Layout->Spawn.Z)));
            Advance(EVerificationPhase::Cross, TEXT("Settled cube pressed the plate and opened the door."));
        }
        break;
    case EVerificationPhase::Cross:
        if (bReachedExit)
        {
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Small || Small.GetSupport())
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
        if (!bPlatePressed && FMath::IsNearlyEqual(Door->GetComponentLocation().Z, Layout->Door.Center.Z, 0.1))
        {
            Controller->SetControlRotation(FRotator::ZeroRotator);
            Controller->ConsoleCommand(TEXT("Input.+key LeftMouseButton"), false);
            Advance(EVerificationPhase::Throw, TEXT("Removing the cube closed the door. Testing left mouse throw."));
        }
        break;
    case EVerificationPhase::Throw:
        if (!Player.GetHeldCube())
        {
            Controller->ConsoleCommand(TEXT("Input.-key LeftMouseButton"), false);
            if (Small.GetSupport() || Player.PlacementGhost->IsVisible() ||
                Cube->GetPhysicsLinearVelocity().X < Layout->ThrowImpulse / Small.GetMassKg() * 0.75)
            {
                Fail(TEXT("Left mouse did not throw the free cube forward"));
                return;
            }
            Player.ResetChamber();
            if (bReachedExit || Player.GetHeldCube() || FVector::DistSquared(SmallStart, Cube->GetComponentLocation()) > 1.0)
            {
                Fail(TEXT("Reset did not restore the initial chamber state"));
                return;
            }
            Small.SetActorLocation(FVector(SmallStart.X, SmallStart.Y, -420.0), false, nullptr, ETeleportType::TeleportPhysics);
            Advance(EVerificationPhase::Return, TEXT("Left mouse threw the cube forward. Reset restored player and cube."));
        }
        break;
    case EVerificationPhase::Return:
        if (FVector::DistSquared(SmallStart, Cube->GetComponentLocation()) < FMath::Square(10.0))
        {
            const FVector Current = Cube->GetComponentLocation();
            AimFrom(FVector(Current.X - 155.0, Current.Y, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Small)
            {
                Fail(TEXT("Could not pick up the returned small cube"));
                return;
            }
            const FVector Target = Large.GetActorLocation() + FVector(0.0, 0.0,
                Large.GetHalfSize() + Small.GetHalfSize() + 40.0);
            AimToHold(Target, FVector::RightVector);
            Advance(EVerificationPhase::CarryToStack, TEXT("Lost cube returned. Carrying it to the large cube."));
        }
        break;
    case EVerificationPhase::CarryToStack:
        if (FVector::DistSquared2D(Small.GetActorLocation(), Large.GetActorLocation()) < FMath::Square(14.0) &&
            Cube->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(25.0))
        {
            if (!Player.PlacementGhost->IsVisible() || Player.PreviewSupport.Get() != Large.GetBody())
            {
                Fail(TEXT("Expected a placement ghost on the stack"));
                return;
            }
            Player.Interact();
            if (Small.GetSupport() != Large.GetBody() || Large.GetStackMass() != 15.0f)
            {
                Fail(TEXT("Small cube did not snap to the large cube and add its mass"));
                return;
            }
            const FVector Start = Step.GetActorLocation() + FVector(-6.0, 0.0,
                Step.GetHalfSize() + Abandoned::MovementProfile::StandingHalfHeight + 1.0);
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Start));
            Advance(EVerificationPhase::StartAscent, TEXT("Small cube snapped to large cube, total mass 15 kg."));
        }
        break;
    case EVerificationPhase::StartAscent:
        if (Player.GetMovementBase() == Step.GetBody())
        {
            Player.AddMovementInput(FVector::ForwardVector);
            if (Player.GetActorLocation().X >= Step.GetActorLocation().X + 7.0)
            {
                Player.Jump();
                Advance(EVerificationPhase::JumpToStack, TEXT("Player jumped toward the stack after a short run-up."));
            }
        }
        break;
    case EVerificationPhase::JumpToStack:
        Player.AddMovementInput(FVector::ForwardVector * 0.35);
        if (Player.GetMovementBase() == Cube)
        {
            Player.StopJumping();
            Player.Jump();
            Advance(EVerificationPhase::JumpToLedge, TEXT("Player landed on the stack and jumped toward the ledge."));
        }
        break;
    case EVerificationPhase::JumpToLedge:
        Player.AddMovementInput(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Large.GetActorLocation().X + 140.0 &&
            Player.GetCharacterMovement()->IsMovingOnGround() &&
            Player.GetActorLocation().Z > Small.GetActorLocation().Z + 145.0)
        {
            Advance(EVerificationPhase::RemoveSupport, TEXT("Player climbed from the stack onto the practice ledge."));
        }
        break;
    case EVerificationPhase::RemoveSupport:
        {
            const FVector Current = Large.GetActorLocation();
            AimFrom(FVector(Current.X, Current.Y - 155.0, Layout->Spawn.Z), Current);
            Player.Interact();
            if (Player.GetHeldCube() != &Large || Small.GetSupport() || !Cube->IsSimulatingPhysics())
            {
                Fail(TEXT("Picking up the lower block did not release its upper block"));
                return;
            }
            ResetChamber(Player);
            for (int32 Index = 0; Index < Cubes.Num(); ++Index)
            {
                if (Cubes[Index]->GetSupport() || !Cubes[Index]->GetBody()->IsSimulatingPhysics() ||
                    !Cubes[Index]->GetActorLocation().Equals(Layout->CubeSpawns[Index].Center, 1.0))
                {
                    Fail(TEXT("Reset did not free and restore all cubes"));
                    return;
                }
            }
            UE_LOG(LogTrainingChamber, Display,
                TEXT("Verification passed: player weight, placement ghost, throw, staircase, detach and reset."));
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

    PlacementGhost = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlacementGhost"));
    PlacementGhost->SetupAttachment(GetRootComponent());
    PlacementGhost->SetAbsolute(true, true, true);
    PlacementGhost->SetCollisionProfileName(TEXT("NoCollision"));
    PlacementGhost->SetGenerateOverlapEvents(false);
    PlacementGhost->SetCanEverAffectNavigation(false);
    PlacementGhost->SetCastShadow(false);
    PlacementGhost->SetReceivesDecals(false);
    PlacementGhost->SetVisibility(false);

    static const ConstructorHelpers::FObjectFinder<UStaticMesh> PreviewMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> PreviewMaterial(
        TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent_OneSided.Widget3DPassThrough_Translucent_OneSided"));
    if (PreviewMesh.Succeeded() && PreviewMaterial.Succeeded())
    {
        PlacementGhost->SetStaticMesh(PreviewMesh.Object);
        PlacementGhost->SetMaterial(0, PreviewMaterial.Object);
    }
}

void ATrainingChamberCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (PlacementGhost->GetStaticMesh())
    {
        UMaterialInstanceDynamic* PreviewMaterial = PlacementGhost->CreateDynamicMaterialInstance(0);
        if (PreviewMaterial)
        {
            PreviewMaterial->SetVectorParameterValue(TEXT("TintColorAndOpacity"),
                GetDefault<UTrainingChamberLayout>()->PlacementGhostTint);
        }
    }
}

void ATrainingChamberCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Input.BindAction(MapAction(EKeys::E), ETriggerEvent::Started, this, &ATrainingChamberCharacter::Interact);
    Input.BindAction(MapAction(EKeys::LeftMouseButton), ETriggerEvent::Started, this, &ATrainingChamberCharacter::ThrowHeldObject);
    Input.BindAction(MapAction(EKeys::R), ETriggerEvent::Started, this, &ATrainingChamberCharacter::ResetChamber);
}

ATrainingChamberGameMode* ATrainingChamberCharacter::GetChamber() const
{
    return GetWorld()->GetAuthGameMode<ATrainingChamberGameMode>();
}

APuzzleCube* ATrainingChamberCharacter::FindReachableCube() const
{
    const FVector Origin = Camera->GetComponentLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(TrainingReach), false, this);
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + GetViewRotation().Vector() * Reach, ECC_Visibility, Query);
    return Cast<APuzzleCube>(Hit.GetActor());
}

bool ATrainingChamberCharacter::FindPlacement(const APuzzleCube& Cube, FPlacement& Placement) const
{
    const ATrainingChamberGameMode* Chamber = GetChamber();
    if (!Chamber)
    {
        return false;
    }

    const FVector Origin = Camera->GetComponentLocation();
    const FVector Current = Cube.GetActorLocation();
    const double Half = Cube.GetHalfSize();
    FCollisionQueryParams TraceQuery(SCENE_QUERY_STAT(CubePlacementSurface), false, this);
    TraceQuery.AddIgnoredActor(&Cube);
    const auto TrySurface = [&](const FHitResult& Hit)
    {
        UBoxComponent* Surface = Cast<UBoxComponent>(Hit.GetComponent());
        if (!Surface || Hit.ImpactNormal.Z < 0.9)
        {
            return false;
        }
        const APuzzleCube* LowerCube = Cast<APuzzleCube>(Surface->GetOwner());
        if (Surface != Chamber->GetPlate() && (!LowerCube || !LowerCube->CanSupportCube()))
        {
            return false;
        }
        const FVector SurfaceCenter = Surface->GetComponentLocation();
        const FVector Target = SurfaceCenter + FVector(0.0, 0.0, Surface->GetScaledBoxExtent().Z + Half + SnapGap);
        if (Current.Z < Target.Z - 8.0 || FVector::DistSquared(Current, Target) > FMath::Square(SnapRange))
        {
            return false;
        }
        const FRotator Rotation(0.0, Surface->GetComponentRotation().Yaw, 0.0);
        const FCollisionShape Shape = FCollisionShape::MakeBox(FVector(Half));
        FCollisionQueryParams Clearance(SCENE_QUERY_STAT(CubePlacementClearance), false, &Cube);
        Clearance.AddIgnoredComponent(Surface);
        if (GetWorld()->OverlapBlockingTestByProfile(Target, Rotation.Quaternion(), TEXT("PhysicsActor"), Shape, Clearance))
        {
            return false;
        }
        // Sweep a small inset so a cube already touching the floor can slide clear of it.
        FHitResult Obstruction;
        if (GetWorld()->SweepSingleByProfile(Obstruction, Current, Target, Rotation.Quaternion(),
            TEXT("PhysicsActor"), FCollisionShape::MakeBox(FVector(Half - 1.0)), Clearance))
        {
            return false;
        }
        Placement = {Surface, Target, Rotation};
        return true;
    };
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + GetViewRotation().Vector() * Reach,
        ECC_Visibility, TraceQuery) && TrySurface(Hit))
    {
        return true;
    }
    return GetWorld()->LineTraceSingleByChannel(Hit, Current, Current - FVector(0.0, 0.0, Half + SnapRange),
        ECC_Visibility, TraceQuery) && TrySurface(Hit);
}

const TCHAR* ATrainingChamberCharacter::GetPlacementPrompt() const
{
    if (GetHeldCube())
    {
        if (const UBoxComponent* Support = PreviewSupport.Get())
        {
            return Cast<APuzzleCube>(Support->GetOwner()) ? TEXT("[E] Snap to cube") : TEXT("[E] Snap to plate");
        }
        return TEXT("[E] Drop cube");
    }
    const APuzzleCube* Cube = FindReachableCube();
    if (!Cube)
    {
        return nullptr;
    }
    return Cube->GetMassKg() == 5.0f ? TEXT("[E] Pick up small cube (5 kg)") : TEXT("[E] Pick up large cube (10 kg)");
}

void ATrainingChamberCharacter::UpdatePlacementGhost(const APuzzleCube* Held)
{
    FPlacement Placement;
    if (IsValid(Held) && FindPlacement(*Held, Placement))
    {
        PreviewSupport = Placement.Support;
        PlacementGhost->SetWorldTransform(FTransform(Placement.Rotation, Placement.Center,
            FVector(Held->GetHalfSize() / 50.0)));
        PlacementGhost->SetVisibility(PlacementGhost->GetStaticMesh() != nullptr);
        return;
    }
    PreviewSupport.Reset();
    PlacementGhost->SetVisibility(false);
}

void ATrainingChamberCharacter::Interact()
{
    if (APuzzleCube* Cube = GetHeldCube())
    {
        FPlacement Placement;
        const bool bCanSnap = FindPlacement(*Cube, Placement);
        ReleaseCube();
        if (bCanSnap)
        {
            Cube->SnapTo(*Placement.Support, Placement.Center, Placement.Rotation);
        }
        return;
    }

    if (APuzzleCube* Candidate = FindReachableCube())
    {
        Candidate->DetachForPickup();
        UBoxComponent* Body = Candidate->GetBody();
        Grip->GrabComponentAtLocationWithRotation(Body, NAME_None, Body->GetComponentLocation(), Body->GetComponentRotation());
        if (GetHeldCube() == Candidate)
        {
            Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        }
    }
}

APuzzleCube* ATrainingChamberCharacter::GetHeldCube() const
{
    const UPrimitiveComponent* Held = Grip->GetGrabbedComponent();
    return IsValid(Held) ? Cast<APuzzleCube>(Held->GetOwner()) : nullptr;
}

void ATrainingChamberCharacter::ReleaseCube()
{
    UpdatePlacementGhost(nullptr);
    UPrimitiveComponent* Held = Grip->GetGrabbedComponent();
    if (IsValid(Held))
    {
        Grip->ReleaseComponent();
        Held->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
        Held->WakeRigidBody();
    }
}

void ATrainingChamberCharacter::ThrowHeldObject()
{
    UPrimitiveComponent* Held = Grip->GetGrabbedComponent();
    if (!IsValid(Held) || !Held->IsSimulatingPhysics())
    {
        return;
    }

    const FVector Direction = GetViewRotation().Vector();
    const FVector CarryVelocity = GetVelocity();
    ReleaseCube();
    Held->SetPhysicsLinearVelocity(CarryVelocity);
    Held->AddImpulse(Direction * GetDefault<UTrainingChamberLayout>()->ThrowImpulse);
}

void ATrainingChamberCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const APuzzleCube* Held = GetHeldCube();
    if (!IsValid(Held))
    {
        UpdatePlacementGhost(nullptr);
        return;
    }

    const FVector Origin = Camera->GetComponentLocation();
    const FVector Direction = GetViewRotation().Vector();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(TrainingHold), false, this);
    Query.AddIgnoredActor(Held);
    FHitResult Hit;
    const bool bWall = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * HoldDistance, ECC_Visibility, Query);
    const double Distance = bWall ? FMath::Clamp(Hit.Distance - Held->GetHalfSize(), 0.0, HoldDistance) : HoldDistance;
    if (Distance < 75.0 || FVector::DistSquared(Origin, Held->GetActorLocation()) > FMath::Square(2.0 * Reach))
    {
        ReleaseCube();
        return;
    }
    Grip->SetTargetLocationAndRotation(Origin + Direction * Distance, FRotator(0.0, GetViewRotation().Yaw, 0.0));
    UpdatePlacementGhost(Held);
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
    DrawText(TEXT("WASD / mouse   Space jump   E pick up / place   LMB throw   R reset   M test menu"), FLinearColor::White, 24.0f, 52.0f);
    const TCHAR* Status = Chamber->HasReachedExit() ? TEXT("Exit reached") :
        (Chamber->IsDoorOpen() ? TEXT("Exit open") :
        (Chamber->IsPlatePressed() ? TEXT("Exit opening") : TEXT("Weigh down the pressure plate")));
    DrawText(Status, FLinearColor(1.0, 0.68, 0.23), 24.0f, 76.0f);

    const float CenterX = Canvas->ClipX * 0.5f;
    const float CenterY = Canvas->ClipY * 0.5f;
    DrawRect(FLinearColor::White, CenterX - 1.0f, CenterY - 1.0f, 3.0f, 3.0f);
    const TCHAR* Prompt = Player->GetPlacementPrompt();
    if (Prompt)
    {
        float PromptWidth = 0.0f;
        float PromptHeight = 0.0f;
        GetTextSize(Prompt, PromptWidth, PromptHeight);
        DrawText(Prompt, FLinearColor::White, CenterX - PromptWidth * 0.5f, CenterY + 22.0f);
    }
}
