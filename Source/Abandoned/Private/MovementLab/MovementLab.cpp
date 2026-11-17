#include "MovementLab.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Player/AbandonedTestController.h"

#include "Player/MovementProfile.h"
#include "World/Blockout.h"

#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

using namespace Abandoned::MovementProfile;

DEFINE_LOG_CATEGORY_STATIC(LogMovementLab, Log, All);

namespace
{
constexpr double RunwayEnd = 1200.0;
constexpr double LaneSpacing = 2200.0;
constexpr double FallResetZ = -600.0;
constexpr double DiagonalDegrees = 12.0;

enum class ETrialKind { Flat, Step, Ledge, Gap };
struct FTrial
{
    const TCHAR* Title;
    int32 GapCm = 0;
    int32 LandingHeightCm = 0;
    ETrialKind Kind = ETrialKind::Ledge;
};

constexpr FTrial Trials[] = {
    {TEXT("Flat ground / jump height"), 0, 0, ETrialKind::Flat},
    {TEXT("Step 25 cm"), 0, 25, ETrialKind::Step},
    {TEXT("Step 30 cm"), 0, 30, ETrialKind::Step},
    {TEXT("Step 35 cm"), 0, 35, ETrialKind::Step},
    {TEXT("Ledge 50 cm"), 0, 50},
    {TEXT("Ledge 70 cm"), 0, 70},
    {TEXT("Ledge 80 cm"), 0, 80},
    {TEXT("Ledge 90 cm"), 0, 90},
    {TEXT("Ledge 100 cm"), 0, 100},
    {TEXT("Ledge 110 cm"), 0, 110},
    {TEXT("Ledge 120 cm"), 0, 120},
    {TEXT("Ledge 135 cm"), 0, 135},
    {TEXT("Ledge 150 cm"), 0, 150},
    {TEXT("Ledge 160 cm"), 0, 160},
    {TEXT("Ledge 170 cm"), 0, 170},
    {TEXT("Ledge 180 cm"), 0, 180},
    {TEXT("Gap 400 cm"), 400, 0, ETrialKind::Gap},
    {TEXT("Gap 450 cm"), 450, 0, ETrialKind::Gap},
    {TEXT("Gap 500 cm"), 500, 0, ETrialKind::Gap},
    {TEXT("Gap 550 cm"), 550, 0, ETrialKind::Gap},
    {TEXT("Gap 600 cm"), 600, 0, ETrialKind::Gap},
    {TEXT("Gap 650 cm"), 650, 0, ETrialKind::Gap},
    {TEXT("Gap 700 cm"), 700, 0, ETrialKind::Gap},
    {TEXT("Gap 600 cm / landing -100 cm"), 600, -100, ETrialKind::Gap},
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

    const Abandoned::Blockout::FBox Platform = Abandoned::Blockout::SpawnBox(World, Cube, Center, Size);
    if (bLanding && Platform.Collision)
    {
        Platform.Collision->GetOwner()->Tags.Add(LandingTag);
    }
}
}

AMovementLabGameMode::AMovementLabGameMode()
{
    PlayerControllerClass = AAbandonedTestController::StaticClass();
    DefaultPawnClass = AMovementLabCharacter::StaticClass();
    HUDClass = AMovementLabHUD::StaticClass();
}

void AMovementLabGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    using namespace Abandoned::Infrastructure::Expedition;
    if (!ErrorMessage.IsEmpty() || !ReadWorldDestination(Options, EntryWorld::Movement, ErrorMessage))
    {
        return;
    }

    UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
    if (!Cube)
    {
        ErrorMessage = TEXT("Movement Lab requires the Engine BasicShapes cube.");
        return;
    }

    UWorld& World = *GetWorld();
    Abandoned::Blockout::ApplyGravity(World);

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

void AMovementLabCharacter::BeginPlay()
{
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING
    bMeasuring = FParse::Param(FCommandLine::Get(), TEXT("MeasureMovementLab"));
#endif
}

void AMovementLabCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    using EEvent = ETriggerEvent;
    Input.BindAction(MapAction(EKeys::Q), EEvent::Started, this, &AMovementLabCharacter::ChangeTrial, int32{-1});
    Input.BindAction(MapAction(EKeys::E), EEvent::Started, this, &AMovementLabCharacter::ChangeTrial, int32{1});
    Input.BindAction(MapAction(EKeys::R), EEvent::Started, this, &AMovementLabCharacter::ResetTrial);
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
    FTransform Start = TrialStart(TrialIndex);
    if (bMeasuring && Variant >= 2)
    {
        Start.SetRotation(FRotator(0.0, DiagonalDegrees, 0.0).Quaternion());
        Start.AddToTranslation(FVector(0.0, -RunwayEnd * FMath::Tan(FMath::DegreesToRadians(DiagonalDegrees)), 0.0));
    }
    PlaceAt(Start);
}

void AMovementLabCharacter::OnJumped_Implementation()
{
    Super::OnJumped_Implementation();
    bJumpInProgress = true;
    bMeasuredTakeoff = bMeasuring;
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
    if (bMeasuring)
    {
        MeasureTrial(DeltaSeconds);
    }
}

void AMovementLabCharacter::MeasureTrial(float DeltaSeconds)
{
    const ETrialKind Kind = Trials[TrialIndex].Kind;
    Elapsed += DeltaSeconds;
    if (!Result.IsEmpty() || Elapsed >= 5.0f)
    {
        static constexpr const TCHAR* Variants[] = {
            TEXT("straight"), TEXT("duck"), TEXT("diagonal"), TEXT("diagonal-duck")
        };
        const TCHAR* Technique = Kind == ETrialKind::Step ? TEXT("walk") : Variants[Variant];
        const TCHAR* Outcome = Result.IsEmpty() ? TEXT("Blocked / no target within five seconds") : *Result;
        const FString EdgeMargin = bMeasuredTakeoff && Kind != ETrialKind::Flat
            ? FString::Printf(TEXT("%.1f cm"), RunwayEnd - LaunchPosition.X) : TEXT("n/a");
        UE_LOG(LogMovementLab, Display, TEXT("Measurement | trial %02d | %s | %s | edge margin %s | airborne crouch %s | step %.4f s"),
            TrialIndex + 1, Technique, Outcome, *EdgeMargin, bCrouchedInAir ? TEXT("yes") : TEXT("no"), DeltaSeconds);
        AdvanceMeasurement();
        return;
    }

    if (Kind != ETrialKind::Flat)
    {
        Move(FVector::ForwardVector);
    }
    const UCharacterMovementComponent* Movement = GetCharacterMovement();
    const double TakeoffX = RunwayEnd - (Kind == ETrialKind::Gap ? 14.0 : 115.0);
    if (Kind != ETrialKind::Step && !bRequestedJump && Movement->IsMovingOnGround() &&
        (Kind == ETrialKind::Flat || GetActorLocation().X >= TakeoffX))
    {
        Jump();
        bRequestedJump = true;
    }
    if (bJumpInProgress && Variant % 2 == 1 && Movement->IsFalling())
    {
        Crouch();
        bCrouchedInAir |= bIsCrouched;
    }
}

void AMovementLabCharacter::AdvanceMeasurement()
{
    const ETrialKind Kind = Trials[TrialIndex].Kind;
    const int32 VariantCount = Kind == ETrialKind::Gap ? 4 : (Kind == ETrialKind::Ledge ? 2 : 1);
    if (++Variant == VariantCount)
    {
        Variant = 0;
        ++TrialIndex;
    }
    if (TrialIndex == TrialCount)
    {
        --TrialIndex;
        bMeasuring = false;
        UE_LOG(LogMovementLab, Display, TEXT("Movement Lab measurements complete."));
        FPlatformMisc::RequestExit(false);
        return;
    }
    Elapsed = 0.0f;
    bRequestedJump = false;
    bMeasuredTakeoff = false;
    bCrouchedInAir = false;
    Result.Reset();
    SetSprint(Trials[TrialIndex].Kind == ETrialKind::Gap);
    ResetTrial();
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
    DrawText(TEXT("WASD / mouse   Space jump   Shift sprint   Alt slow   Ctrl crouch   Q / E trial   R reset   M test menu"),
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
