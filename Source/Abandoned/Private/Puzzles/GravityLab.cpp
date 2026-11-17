#include "Puzzles/GravityLab.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "Puzzles/GravityLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "World/Blockout.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogGravityLab, Log, All);

namespace
{
using Abandoned::Domain::Puzzles::GravityDirection;
using Abandoned::Domain::Puzzles::GravityField;
constexpr float FieldScale = 0.82f;
constexpr float FieldForce = 9000.0f;

FQuat GravityFrame(GravityDirection Direction)
{
    return FQuat(FVector::ForwardVector, FMath::DegreesToRadians(
        static_cast<double>(Abandoned::Domain::Puzzles::GravityRollDegrees(Direction))));
}

FBox Bounds(const FSceneBlock& Volume)
{
    return FBox::BuildAABB(Volume.Center, Volume.Size * 0.5);
}
}

AGravityLabGameMode::AGravityLabGameMode()
{
    DefaultPawnClass = AGravityLabCharacter::StaticClass();
    PlayerControllerClass = AAbandonedTestController::StaticClass();
    HUDClass = AGravityLabHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface"));
    BaseMaterial = Material.Object;
}

void AGravityLabGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
    Super::InitGame(Map, Options, Error);
    using namespace Abandoned::Infrastructure::Expedition;
    if (!Error.IsEmpty() || !ReadWorldDestination(Options, EntryWorld::Gravity, Error))
    {
        return;
    }
    SetActorTickEnabled(false);
    Layout = GetDefault<UGravityLabLayout>();
    UWorld& World = *GetWorld();
    UStaticMesh* CubeMesh = Abandoned::Blockout::LoadCube();
    if (!CubeMesh || !BaseMaterial || !Layout->Field.IsValid() || !Layout->Goal.IsValid() || Layout->Room.IsEmpty() ||
        Layout->Spawn.ContainsNaN() || Layout->Terminal.ContainsNaN() || Layout->PowerSwitch.ContainsNaN() ||
        Layout->SmallStart.ContainsNaN() || Layout->LargeStart.ContainsNaN())
    {
        Error = TEXT("Invalid gravity lab layout or missing assets.");
        return;
    }
    Abandoned::Blockout::ApplyGravity(World);
    const auto Finishes = Abandoned::Scene::MakeFinishes(*this, BaseMaterial, Layout->Finishes);
    Violet = Finishes.FindRef(TEXT("Violet"));
    Inactive = Finishes.FindRef(TEXT("Inactive"));
    UMaterialInterface* Ballast = Finishes.FindRef(TEXT("Ballast"));
    if (!Violet || !Inactive || !Ballast || !Finishes.Contains(TEXT("Steel")))
    {
        Error = TEXT("Missing gravity lab materials.");
        return;
    }
    const auto Spawn = [&](const FSceneBlock& Block)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *CubeMesh, Block.Center, Block.Size, EComponentMobility::Static,
            Block.bCollision ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
        return Parts;
    };
    for (const FSceneBlock& Block : Layout->Room)
    {
        if (!Block.IsValid() || !Finishes.Contains(Block.Finish) || !Spawn(Block).Collision)
        {
            Error = TEXT("Invalid gravity lab room block.");
            return;
        }
    }
    for (const FSceneLight& Light : Layout->Lights)
    {
        if (!Light.IsValid() || !Abandoned::Scene::AddLight(World, Light))
        {
            Error = TEXT("Invalid gravity lab light.");
            return;
        }
    }
    PowerSwitch = Spawn({Layout->PowerSwitch, FVector(50, 26, 54), true, TEXT("Steel")}).Collision;
    Terminal = Spawn({Layout->Terminal, FVector(98, 32, 78), true, TEXT("Steel")}).Collision;
    Small = World.SpawnActor<APuzzleCube>();
    Large = World.SpawnActor<APuzzleCube>();
    if (!PowerSwitch || !Terminal || !Small || !Large)
    {
        Error = TEXT("Could not assemble gravity lab controls and ballast.");
        return;
    }
    Small->Initialize(EPuzzleCubeType::Small, *CubeMesh);
    Large->Initialize(EPuzzleCubeType::Large, *CubeMesh);
    Small->SetVisualMaterial(*Ballast);
    Large->SetVisualMaterial(*Ballast);
    Small->ResetPosition(Layout->SmallStart);
    Large->ResetPosition(Layout->LargeStart);

    const FBox Field = Bounds(Layout->Field);
    for (const double X : {Field.Min.X, Field.Max.X})
    {
        for (const double Y : {Field.Min.Y + 8.0, Field.Max.Y - 8.0})
        {
            UStaticMeshComponent* Marker =
                Spawn({FVector(X, Y, Layout->Field.Center.Z), FVector(12, 12, Layout->Field.Size.Z),
                    false, TEXT("Inactive")}).Visual;
            if (!Marker)
            {
                Error = TEXT("Could not assemble gravity boundary markers.");
                return;
            }
            FieldMarkers.Add(Marker);
        }
    }
#if WITH_DEV_AUTOMATION_TESTS
    bVerify = FParse::Param(FCommandLine::Get(), TEXT("VerifyGravityLab"));
    bCapture = FParse::Param(FCommandLine::Get(), TEXT("CaptureGravityLab"));
#endif
    SetActorTickEnabled(true);
}

void AGravityLabGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Controller)
{
    if (Controller && Layout && IsActorTickEnabled())
    {
        RestartPlayerAtTransform(Controller, FTransform(FRotator::ZeroRotator, Layout->Spawn));
        if (AGravityLabCharacter* Player = Cast<AGravityLabCharacter>(Controller->GetPawn()))
        {
            Player->AddTickPrerequisiteActor(this);
            Player->GetCharacterMovement()->AddTickPrerequisiteActor(this);
        }
    }
}

void AGravityLabGameMode::ResetLab(AGravityLabCharacter& Player)
{
    Power.Reset();
    Selected = Direction::Down;
    bReachedGoal = false;
    UpdateFieldMarkers();
    Player.ReleaseHeldObject();
    Player.PlaceAtLab(Layout->Spawn, Layout->Spawn + FVector(700, 0, Abandoned::MovementProfile::CameraHeight));
    Small->GetBody()->SetEnableGravity(true);
    Large->GetBody()->SetEnableGravity(true);
    Small->ResetPosition(Layout->SmallStart);
    Large->ResetPosition(Layout->LargeStart);
}

void AGravityLabGameMode::UpdateFieldMarkers()
{
    for (UStaticMeshComponent* Marker : FieldMarkers)
    {
        Marker->SetMaterial(0, IsPowered() ? Violet : Inactive);
    }
}

void AGravityLabGameMode::ApplyField(APuzzleCube& Cube, const GravityField* Field)
{
    UBoxComponent& Body = *Cube.GetBody();
    const bool bActive = Field && Bounds(Layout->Field).IsInsideOrOn(Body.GetComponentLocation());
    if (Body.IsGravityEnabled() == bActive)
    {
        Body.SetEnableGravity(!bActive);
    }
    if (bActive && Body.IsSimulatingPhysics())
    {
        const FVector Down = GravityFrame(Field->Direction).RotateVector(FVector::DownVector);
        Body.AddForce(Down * Abandoned::Domain::Puzzles::GravityForceForBody(*Field, Body.GetMass()));
    }
}

void AGravityLabGameMode::Tick(float Delta)
{
    Super::Tick(Delta);
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    AGravityLabCharacter* Player = Controller ? Cast<AGravityLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player)
    {
        return;
    }
    const GravityField Field{Selected, FieldScale, FieldForce, 0, IsPowered()};
    const GravityField* Active = Abandoned::Domain::Puzzles::SelectGravityField(std::span(&Field, 1));
    const FVector Here = Player->GetActorLocation();
    const bool bInField = Active && Bounds(Layout->Field).IsInsideOrOn(Here);
    Player->ChangeGravity(bInField ? Selected : Direction::Down, bInField ? Field.CharacterScale : 1.0f);
    // Snap constraints represent support along world Z. Clear them for the entire stack
    // before applying forces, including when the active volume only contains one cube.
    if (Active && Active->Direction != Direction::Down)
    {
        for (APuzzleCube* Cube : {Small.Get(), Large.Get()})
        {
            if (Cube->HasSnapConstraints() && Bounds(Layout->Field).IsInsideOrOn(Cube->GetActorLocation()))
            {
                Cube->ReleaseSnapConstraints();
            }
        }
    }
    ApplyField(*Small, Active);
    ApplyField(*Large, Active);
    if (bInField && Selected == Direction::Left && Player->GetCharacterMovement()->IsMovingOnGround() &&
        Bounds(Layout->Goal).IsInsideOrOn(Here))
    {
        bReachedGoal = true;
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerify)
    {
        Verify(*Player, Delta);
    }
#endif
}

const TCHAR* AGravityLabGameMode::GetDirectionLabel() const
{
    switch (Selected)
    {
    case Direction::Left: return TEXT("left wall");
    case Direction::Right: return TEXT("right wall");
    case Direction::Up: return TEXT("ceiling");
    case Direction::Down: return TEXT("floor");
    }
    return TEXT("floor");
}

#if WITH_DEV_AUTOMATION_TESTS
void AGravityLabGameMode::Verify(AGravityLabCharacter& Player, float Delta)
{
    StepTime += Delta;
    const auto Fail = [this, &Player](const TCHAR* Reason)
    {
        UE_LOG(LogGravityLab, Error, TEXT("Gravity lab verification failed at %d: %s. Player %s, gravity %s."),
            Step, Reason, *Player.GetActorLocation().ToCompactString(),
            *Player.GetCharacterMovement()->GetGravityDirection().ToCompactString());
        bVerify = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto Next = [this](const TCHAR* Message)
    {
        ++Step;
        StepTime = 0.0f;
        UE_LOG(LogGravityLab, Display, TEXT("Gravity lab: %s"), Message);
    };
    const auto SetDirectionAtTerminal = [this, &Player](Direction NewDirection)
    {
        Player.PlaceAtLab(FVector(Layout->Terminal.X, Layout->Terminal.Y + 142.0, 92.0), Layout->Terminal);
        Player.SelectDirection(NewDirection);
    };
    const auto UsePowerSwitch = [this, &Player]
    {
        Player.PlaceAtLab(FVector(Layout->PowerSwitch.X, Layout->PowerSwitch.Y + 142.0, 92.0), Layout->PowerSwitch);
        Player.Interact();
    };
    if (StepTime > 7.0f)
    {
        Fail(TEXT("Timed out waiting for the physical field transition"));
        return;
    }
    switch (Step)
    {
    case 0:
        if (StepTime >= 0.15f)
        {
            const double ContactZ = Large->GetHalfSize() + 0.2;
            const FVector Base(Layout->LargeStart.X, Layout->LargeStart.Y, ContactZ);
            Large->ResetPosition(Base);
            const FVector Top = Base + FVector(0, 0, Large->GetHalfSize() + Small->GetHalfSize() + 0.2);
            if (!Small->SnapTo(*Large->GetBody(), Top, FRotator::ZeroRotator) ||
                Small->GetBody()->IsSimulatingPhysics() || Large->GetBody()->IsSimulatingPhysics())
            {
                Fail(TEXT("Could not set up a snapped stack under ordinary gravity"));
                return;
            }
            SetDirectionAtTerminal(Direction::Left);
            UsePowerSwitch();
            if (!IsPowered() || Selected != Direction::Left)
            {
                Fail(TEXT("The terminal and power switch were unreachable"));
                return;
            }
            Next(TEXT("Set left at the terminal and powered the field."));
        }
        break;
    case 1:
        if (Small->HasSnapConstraints() || Large->HasSnapConstraints() ||
            !Small->GetBody()->IsSimulatingPhysics() || !Large->GetBody()->IsSimulatingPhysics())
        {
            Fail(TEXT("Sideways gravity did not release the snapped stack"));
            return;
        }
        Small->ResetPosition(Layout->SmallStart + FVector(0, 0, 250));
        Large->ResetPosition(Layout->LargeStart + FVector(0, 0, 250));
        Next(TEXT("Sideways gravity released the stack and restored physics to both cubes."));
        break;
    case 2:
        if (StepTime >= 0.26f)
        {
            const double LightSpeed = -Small->GetVelocity().Y;
            const double HeavySpeed = -Large->GetVelocity().Y;
            if (!Player.GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector) ||
                HeavySpeed < 100.0 || LightSpeed <= HeavySpeed * 1.6 || LightSpeed >= HeavySpeed * 2.4)
            {
                Fail(TEXT("Gravity escaped its boundary or did not respect body mass"));
                return;
            }
            UE_LOG(LogGravityLab, Display, TEXT("Gravity lab: lateral speeds %.1f cm/s (5 kg), %.1f cm/s (10 kg)."),
                LightSpeed, HeavySpeed);
            Player.PlaceAtLab(FVector(Layout->Field.Center.X - 330, 10, Layout->Goal.Center.Z),
                FVector(Layout->Goal.Center.X, 10, Layout->Goal.Center.Z + 75));
            Next(TEXT("Crossed the field boundary with the character."));
        }
        break;
    case 3:
        if (Player.GetCharacterMovement()->IsMovingOnGround() &&
            Player.GetActorLocation().Y < Layout->Goal.Center.Y + 110.0)
        {
            if (Player.GetCharacterMovement()->GetGravityDirection().Y > -0.99 ||
                Player.GetActorUpVector().Y < 0.99)
            {
                Fail(TEXT("The movement frame or capsule did not land on the left wall"));
                return;
            }
            Next(TEXT("Landed on the wall with the capsule aligned."));
        }
        break;
    case 4:
        if (Player.GetActorLocation().X < Layout->Goal.Center.X - 20.0)
        {
            Player.Move(FVector::ForwardVector);
        }
        if (const double Offset = Player.GetActorLocation().Z - Layout->Goal.Center.Z; FMath::Abs(Offset) > 22.0)
        {
            Player.Move(FVector::RightVector * FMath::Clamp(Offset / 130.0, -0.6, 0.6));
        }
        if (HasReachedGoal())
        {
            const FVector ViewUp = FVector::VectorPlaneProject(FVector::RightVector,
                Player.Camera->GetForwardVector()).GetSafeNormal();
            if (Player.Camera->GetUpVector().Dot(ViewUp) < 0.97)
            {
                UE_LOG(LogGravityLab, Display, TEXT("Camera up %s, capsule up %s, camera rotation %s, visual field up %s, pitch %.2f yaw %.2f."),
                    *Player.Camera->GetUpVector().ToCompactString(), *Player.GetActorUpVector().ToCompactString(),
                    *Player.Camera->GetComponentRotation().ToCompactString(),
                    *Player.ViewFrame.GetUpVector().ToCompactString(), Player.Pitch, Player.Yaw);
                Fail(TEXT("The eye horizon did not follow the new vertical"));
                return;
            }
            if (bCapture)
            {
                Player.Yaw = 175.0;
                Player.Pitch = -9.0;
                Player.UpdateView(1.0f);
                FScreenshotRequest::RequestScreenshot(TEXT("GravityLab-Walk"), false, false);
            }
            Next(TEXT("Reached the marked target using gravity-relative movement."));
        }
        break;
    case 5:
        if (StepTime >= 0.15f)
        {
            Player.ResetChamber();
            if (IsPowered() || HasReachedGoal() || !Player.GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector))
            {
                Fail(TEXT("Reset did not restore normal gravity, power and objective"));
                return;
            }
            SetDirectionAtTerminal(Direction::Up);
            UsePowerSwitch();
            Player.PlaceAtLab(Layout->Field.Center, Layout->Field.Center + FVector(500, 0, 75));
            Player.ToggleHorizon();
            Next(TEXT("Reset and selected ceiling gravity with instant horizon."));
        }
        break;
    case 6:
        if (Player.GetCharacterMovement()->IsMovingOnGround() && Player.GetActorLocation().Z > 490.0)
        {
            if (Player.GetCharacterMovement()->GetGravityDirection().Z < 0.99 || Player.Camera->GetUpVector().Z > -0.99)
            {
                Fail(TEXT("Ceiling gravity or instant horizon did not apply"));
                return;
            }
            Player.ResetChamber();
            SetDirectionAtTerminal(Direction::Right);
            UsePowerSwitch();
            Player.PlaceAtLab(Layout->Field.Center, Layout->Field.Center + FVector(500, 0, 75));
            Next(TEXT("Ceiling gravity and instant horizon passed. Entered the right-wall field."));
        }
        break;
    case 7:
        if (Player.GetCharacterMovement()->IsMovingOnGround() && Player.GetActorLocation().Y > 240.0)
        {
            if (Player.GetCharacterMovement()->GetGravityDirection().Y < 0.99)
            {
                Fail(TEXT("The right-wall setting did not become active"));
                return;
            }
            Next(TEXT("Landed on the right wall."));
        }
        break;
    case 8:
        Player.Move(FVector::ForwardVector);
        if (Player.GetActorLocation().X > Bounds(Layout->Field).Max.X + 10.0)
        {
            if (!Player.GetCharacterMovement()->GetGravityDirection().Equals(FVector::DownVector) ||
                Player.Camera->GetUpVector().Z < 0.99)
            {
                Fail(TEXT("Leaving the field did not restore ordinary gravity and horizon"));
                return;
            }
            UE_LOG(LogGravityLab, Display, TEXT("Gravity lab verification passed: boundaries, mass, wall movement, ceiling and reset."));
            bVerify = false;
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif

AGravityLabCharacter::AGravityLabCharacter()
{
    bUseControllerRotationYaw = false;
    Camera->bUsePawnControlRotation = false;
    Camera->SetAbsolute(false, true, false);
}

FRotator AGravityLabCharacter::GetViewRotation() const
{
    return Camera->GetComponentRotation();
}

void AGravityLabCharacter::AddControllerYawInput(float Value)
{
    Yaw = FMath::UnwindDegrees(Yaw + Value);
}

void AGravityLabCharacter::AddControllerPitchInput(float Value)
{
    Pitch = FMath::Clamp(Pitch + Value, -87.0, 87.0);
}

void AGravityLabCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Super::BindAdditionalInput(Input);
    Input.BindAction(MapAction(EKeys::One), ETriggerEvent::Started, this, &AGravityLabCharacter::SelectDirection, Direction::Left);
    Input.BindAction(MapAction(EKeys::Two), ETriggerEvent::Started, this, &AGravityLabCharacter::SelectDirection, Direction::Right);
    Input.BindAction(MapAction(EKeys::Three), ETriggerEvent::Started, this, &AGravityLabCharacter::SelectDirection, Direction::Down);
    Input.BindAction(MapAction(EKeys::Four), ETriggerEvent::Started, this, &AGravityLabCharacter::SelectDirection, Direction::Up);
    Input.BindAction(MapAction(EKeys::V), ETriggerEvent::Started, this, &AGravityLabCharacter::ToggleHorizon);
}

void AGravityLabCharacter::Move(FVector LocalDirection)
{
    AddMovementInput(FieldFrame.RotateVector(FRotator(0.0, Yaw, 0.0).RotateVector(LocalDirection)));
}

void AGravityLabCharacter::ChangeGravity(Direction NewDirection, float Scale)
{
    UCharacterMovementComponent& Movement = *GetCharacterMovement();
    Movement.GravityScale = Scale;
    if (Current == NewDirection)
    {
        return;
    }
    Current = NewDirection;
    FieldFrame = GravityFrame(Current);
    SetBase(nullptr);
    Movement.SetGravityDirection(FieldFrame.RotateVector(FVector::DownVector));
    SetActorRotation(FieldFrame * FRotator(0.0, Yaw, 0.0).Quaternion(), ETeleportType::TeleportPhysics);
    Movement.SetMovementMode(MOVE_Falling);
}

void AGravityLabCharacter::UpdateView(float Blend)
{
    ViewFrame = FQuat::Slerp(ViewFrame, FieldFrame, Blend).GetNormalized();
    Camera->SetWorldRotation(ViewFrame * FRotator(Pitch, Yaw, 0.0).Quaternion());
}

void AGravityLabCharacter::Tick(float Delta)
{
    SetActorRotation(FieldFrame * FRotator(0.0, Yaw, 0.0).Quaternion(), ETeleportType::TeleportPhysics);
    UpdateView(bSmoothHorizon ? 1.0f - FMath::Exp(-6.0f * Delta) : 1.0f);
    Super::Tick(Delta);
}

void AGravityLabCharacter::PlaceAtLab(FVector Start, FVector LookAt)
{
    ChangeGravity(Direction::Down, 1.0f);
    const FVector Eye = Start + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
    const FRotator View = (LookAt - Eye).Rotation();
    Pitch = View.Pitch;
    Yaw = View.Yaw;
    PlaceAt(FTransform(View, Start));
    UpdateView(1.0f);
}

void AGravityLabCharacter::SelectDirection(Direction Value)
{
    if (AGravityLabGameMode* Lab = GetWorld()->GetAuthGameMode<AGravityLabGameMode>();
        Lab && Lab->Terminal && TraceInteractable() == Lab->Terminal)
    {
        Lab->Selected = Value;
    }
}

void AGravityLabCharacter::ResetChamber()
{
    if (AGravityLabGameMode* Lab = GetWorld()->GetAuthGameMode<AGravityLabGameMode>())
    {
        Lab->ResetLab(*this);
    }
}

bool AGravityLabCharacter::InteractWithDevice(UPrimitiveComponent& Target)
{
    AGravityLabGameMode* Lab = GetWorld()->GetAuthGameMode<AGravityLabGameMode>();
    if (!Lab || &Target != Lab->PowerSwitch)
    {
        return false;
    }
    Lab->Power.Observe(true, 0.0f);
    Lab->Power.Observe(false, 0.0f);
    Lab->UpdateFieldMarkers();
    return true;
}

void AGravityLabHUD::DrawHUD()
{
    Super::DrawHUD();
    const AGravityLabGameMode* Lab = GetWorld()->GetAuthGameMode<AGravityLabGameMode>();
    const AGravityLabCharacter* Player = Cast<AGravityLabCharacter>(GetOwningPawn());
    if (!Canvas || !Lab || !Player)
    {
        return;
    }
    const FString State = FString::Printf(TEXT("GRAVITY TERMINAL  /  %s  /  %s"), Lab->IsPowered() ? TEXT("ON") : TEXT("OFF"),
        Lab->GetDirectionLabel());
    DrawText(State, FLinearColor::White, 24, 24, nullptr, 1.05f);
    DrawText(Lab->HasReachedGoal() ? TEXT("Wall platform reached.") :
        TEXT("Reach the marked platform on the left wall. Strafe [A/D] to gain height."), FLinearColor::White, 24, 52, nullptr, 1.0f);
    DrawText(TEXT("Aim at terminal [1] Left [2] Right [3] Floor [4] Ceiling. Power switch [E]."),
        FLinearColor::White, 24, 76, nullptr, 0.9f);
    DrawText(Player->HasSmoothHorizon() ? TEXT("Horizon [V] Smooth   Reset [R]") : TEXT("Horizon [V] Instant   Reset [R]"),
        FLinearColor::White, 24, 98, nullptr, 0.9f);
    const float X = Canvas->ClipX / 2.0f;
    const float Y = Canvas->ClipY / 2.0f;
    DrawLine(X - 6, Y, X + 6, Y, FLinearColor::White, 1);
    DrawLine(X, Y - 6, X, Y + 6, FLinearColor::White, 1);
}
