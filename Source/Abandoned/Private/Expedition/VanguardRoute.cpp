#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#include "Application/Expedition/PassageProgress.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogVanguardRoute, Log, All);

namespace
{
using Abandoned::Domain::Expedition::Segment;
using Abandoned::Domain::Expedition::FallImpact;
using Abandoned::Domain::Expedition::RadiationPosition;
using Abandoned::Infrastructure::Expedition::TravelToRadiationPassage;
using Abandoned::Infrastructure::Expedition::ReadSegment;
using Abandoned::Infrastructure::Expedition::TravelToSegment;
constexpr double VanguardAirlockTravelCm = 280.0;
constexpr double SupplyReach = 240.0;

bool FitsMesh(UStaticMeshComponent* Visual, UStaticMesh* Mesh, FVector FitSize, FRotator Rotation)
{
    if (!Visual || !Mesh)
    {
        return false;
    }
    const FBox Bounds = Mesh->GetBoundingBox();
    const FVector OrientedSize = Bounds.TransformBy(FTransform(Rotation)).GetSize();
    if (!Bounds.IsValid || OrientedSize.GetMin() <= UE_KINDA_SMALL_NUMBER)
    {
        return false;
    }
    // Authored rotations are quarter turns. Fit the mesh on its local axes and align its bounds centre to the actor.
    const FVector Scale = Rotation.UnrotateVector(FitSize / OrientedSize).GetAbs();
    const FVector Offset = -Rotation.RotateVector(Bounds.GetCenter() * Scale);
    Visual->SetStaticMesh(Mesh);
    Visual->SetMaterial(0, Mesh->GetMaterial(0));
    Visual->SetRelativeTransform(FTransform(Rotation, Offset, Scale));
    return true;
}

FBox BoundsOf(const FSceneBlock& Data)
{
    return FBox(Data.Center - Data.Size * 0.5, Data.Center + Data.Size * 0.5);
}
}

AVanguardRouteGameMode::AVanguardRouteGameMode()
{
    DefaultPawnClass = AVanguardCharacter::StaticClass();
    HUDClass = AVanguardHUD::StaticClass();
    PlayerControllerClass = AAbandonedTestController::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface"));
    BaseMaterial = Material.Object;
}

void AVanguardRouteGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    if (!ErrorMessage.IsEmpty())
    {
        return;
    }
    if (!GetDefault<UVanguardFlashlightSettings>()->IsValid())
    {
        ErrorMessage = TEXT("Invalid Vanguard flashlight configuration.");
        return;
    }
    Current = ReadSegment(UGameplayStatics::ParseOption(Options, TEXT("Chamber"))) == Segment::Vanguard
        ? Segment::Vanguard : Segment::Passage;
    if (!AtStation() && UGameplayStatics::ParseOption(Options, TEXT("RadiationRun")) == TEXT("1"))
    {
        RadiationLayout = GetDefault<UVanguardRadiationLayout>();
    }
    if (!AtStation() && !RadiationLayout)
    {
        const Abandoned::Infrastructure::Expedition::UnrealCheckpointStore Store;
        const auto Origin = ReadSegment(UGameplayStatics::ParseOption(Options, TEXT("Checkpoint")));
        ReturnCheckpoint = Abandoned::Application::Expedition::PassageCheckpoint(
            Origin, GetWorld()->WorldType == EWorldType::Game ? &Store : nullptr);
    }
    const auto* Passage = RadiationLayout ? static_cast<const UVanguardPassageLayout*>(RadiationLayout.Get())
                                          : GetDefault<UVanguardPassageLayout>();
    const auto* Station = GetDefault<UVanguardStationLayout>();
    Layout = AtStation() ? static_cast<const UVanguardRouteLayout*>(Station) : Passage;
    UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
    if (!Cube || Layout->Room.IsEmpty() || Layout->Spawn.ContainsNaN() ||
        Layout->Room.ContainsByPredicate([](const FSceneBlock& Block) { return !Block.IsValid(); }) ||
        Layout->Lights.ContainsByPredicate([](const FSceneLight& Light) { return !Light.IsValid(); }) ||
        Layout->Finishes.ContainsByPredicate([](const FSceneFinish& Finish) { return !Finish.IsValid(); }) ||
        (AtStation() ? !Station->SupplyCabinet.IsValid() : (!Passage->Door.IsValid() || !Passage->ExitZone.IsValid() ||
            (Passage->bEnableMantle && (Passage->MantleLedges.IsEmpty() ||
                Passage->MantleLedges.ContainsByPredicate([](const FSceneBlock& Ledge) { return !Ledge.IsValid() || !Ledge.bCollision; }))) ||
            !Passage->FrameFit.IsValid() || Passage->FrameRotation.ContainsNaN() || Passage->LeafRotation.ContainsNaN())))
    {
        ErrorMessage = TEXT("Invalid Vanguard route geometry or device configuration.");
        return;
    }
    const UVanguardInjurySettings& Injury = *GetDefault<UVanguardInjurySettings>();
    if (!AtStation() && (!Injury.Recovery().IsValid() ||
        !FMath::IsFinite(Injury.BreathVolume) || Injury.BreathVolume < 0.0f || Injury.BreathVolume > 1.0f))
    {
        ErrorMessage = TEXT("Invalid Vanguard injury settings.");
        return;
    }
    if (RadiationLayout)
    {
        const Abandoned::Domain::Expedition::FlarePattern Pattern{RadiationLayout->QuietSeconds,
            RadiationLayout->FlareSeconds, RadiationLayout->QuietDosePerSecond, RadiationLayout->FlareDosePerSecond,
            RadiationLayout->RecoveryDosePerSecond};
        const FSceneBlock& Exposure = RadiationLayout->ExposureBounds;
        const FBox ExposureBox = BoundsOf(Exposure);
        const bool bInvalidShelter = RadiationLayout->ShelterBounds.ContainsByPredicate([&ExposureBox](const FSceneBlock& Shelter)
        {
            const FBox ShelterBox = BoundsOf(Shelter);
            return !Shelter.IsValid() || !ExposureBox.IsInsideOrOn(ShelterBox.Min) || !ExposureBox.IsInsideOrOn(ShelterBox.Max);
        });
        if (!Pattern.IsValid() || !Exposure.IsValid() || RadiationLayout->ShelterBounds.IsEmpty() || bInvalidShelter ||
            ExposureBox.IsInsideOrOn(Layout->Spawn) || ExposureBox.Intersect(BoundsOf(Passage->ExitZone)))
        {
            ErrorMessage = TEXT("Invalid radiation cycle, exposure bounds or shelters.");
            return;
        }
        Radiation.emplace(Pattern);
        if (!GetSession())
        {
            ErrorMessage = TEXT("Radiation passage requires an expedition session.");
            return;
        }
    }
    UWorld& World = *GetWorld();
    Abandoned::Blockout::ApplyGravity(World);
    const auto Finishes = Abandoned::Scene::MakeFinishes(*this, BaseMaterial, Layout->Finishes);
    const auto SpawnBlock = [&](const FSceneBlock& Block, EComponentMobility::Type Mobility)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *Cube, Block.Center, Block.Size, Mobility,
            Block.bCollision ? FName(TEXT("BlockAllDynamic")) : FName(TEXT("NoCollision")));
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
        return Parts;
    };
    for (const auto& Block : Layout->Room)
    {
        SpawnBlock(Block, EComponentMobility::Static);
    }
    for (const auto& Light : Layout->Lights)
    {
        Abandoned::Scene::AddLight(World, Light);
    }
    if (AtStation())
    {
        SupplyCabinet = SpawnBlock(Station->SupplyCabinet, EComponentMobility::Static).Collision;
    }
    else
    {
        if (Passage->bEnableMantle)
        {
            for (const auto& Ledge : Passage->MantleLedges)
            {
                UBoxComponent* Collision = SpawnBlock(Ledge, EComponentMobility::Static).Collision;
                if (!IsValid(Collision))
                {
                    ErrorMessage = TEXT("Could not spawn a passage mantle ledge.");
                    return;
                }
                Abandoned::Infrastructure::Expedition::MarkMantleLedge(*Collision);
            }
        }
        if (UVanguardSession* Session = GetSession())
        {
            Session->Suit.Refill();
            Session->Dose.Clear();
            Session->bFlashlightOn = false;
#if WITH_DEV_AUTOMATION_TESTS
            if (!Radiation && FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguardOxygen")))
            {
                Session->Suit.SpendInPassage(555.0f);
            }
#endif
        }
        const auto DoorVisual = SpawnBlock(Passage->Door, EComponentMobility::Movable);
        Door = DoorVisual.Collision;
        const bool bLeaf = FitsMesh(DoorVisual.Visual, Passage->LeafMesh.LoadSynchronous(), Passage->Door.Size,
            Passage->LeafRotation);

        bool bFrame = false;
        if (UStaticMesh* Frame = Passage->FrameMesh.LoadSynchronous())
        {
            const auto FrameParts = SpawnBlock(Passage->FrameFit, EComponentMobility::Static);
            bFrame = FitsMesh(FrameParts.Visual, Frame, Passage->FrameFit.Size, Passage->FrameRotation);
        }
        const bool bFloor = Layout->Finishes.ContainsByPredicate([](const FSceneFinish& Finish)
        {
            return Finish.Name == TEXT("FabFloor") && Finish.Material.Get() != nullptr;
        });
        const bool bWall = Layout->Finishes.ContainsByPredicate([](const FSceneFinish& Finish)
        {
            return Finish.Name == TEXT("FabWall") && Finish.Material.Get() != nullptr;
        });
        UE_LOG(LogVanguardRoute, Display, TEXT("Passage artwork: frame=%s, leaf=%s, Fab floor=%s, Fab walls=%s."),
            bFrame ? TEXT("loaded") : TEXT("fallback"), bLeaf ? TEXT("loaded") : TEXT("fallback"),
            bFloor ? TEXT("loaded") : TEXT("fallback"), bWall ? TEXT("loaded") : TEXT("fallback"));
    }
    if (AtStation() ? !SupplyCabinet : !Door)
    {
        ErrorMessage = TEXT("Could not spawn the Vanguard route device.");
        return;
    }
    if (UVanguardSession* Session = GetSession(); AtStation() && Session && Session->Dose.Percent() > 0.0)
    {
        UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard station cleared remaining radiation dose: %.2f%%."),
            Session->Dose.Percent());
        Session->Dose.Clear();
        bClearedRadiation = true;
    }
#if WITH_DEV_AUTOMATION_TESTS
    bVerifyRoute = FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCourse")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCheckpoint")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguard"));
    bCaptureRoute = FParse::Param(FCommandLine::Get(), TEXT("CaptureVanguard"));
    bVerifyFalls = !AtStation() && !Radiation && FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguardFalls"));
    bVerifyInjuryAudio = bVerifyFalls && FParse::Param(FCommandLine::Get(), TEXT("VerifyInjuryAudio"));
    bVerifyRadiation = (Radiation || AtStation()) && FParse::Param(FCommandLine::Get(), TEXT("VerifyRadiation"));
    bCaptureRadiation = FParse::Param(FCommandLine::Get(), TEXT("CaptureRadiation"));
#endif
    UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard segment ready: %s."),
        AtStation() ? TEXT("station") : (Radiation ? TEXT("radiation passage") : TEXT("passage")));
}

void AVanguardRouteGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
    if (Player && (Door || SupplyCabinet))
    {
        LastGroundHeight.reset();
        LandingImpact = FallImpact::None;
        InjuryRecovery.Reset();
        RestartPlayerAtTransform(Player, FTransform(FRotator::ZeroRotator, Layout->Spawn));
        Player->SetInputMode(FInputModeGameOnly());
        const UVanguardSession* Session = GetSession();
        if (AVanguardCharacter* Pawn = Cast<AVanguardCharacter>(Player->GetPawn()))
        {
            const UVanguardFlashlightSettings& Settings = *GetDefault<UVanguardFlashlightSettings>();
            USpotLightComponent& Light = *Pawn->ShoulderLight;
            Light.SetRelativeLocation(Settings.ShoulderOffset);
            Light.SetIntensity(Settings.IntensityLumens);
            Light.SetAttenuationRadius(Settings.RangeCm);
            Light.SetOuterConeAngle(Settings.OuterConeDegrees);
            Light.SetInnerConeAngle(Settings.InnerConeDegrees);
            Light.SetVisibility(Session && Session->bFlashlightOn);
            if (!AtStation())
            {
                if (CastChecked<UVanguardPassageLayout>(Layout.Get())->bEnableMantle)
                {
                    Pawn->LoadMantleVisuals();
                }
                const UVanguardInjurySettings& Injury = *GetDefault<UVanguardInjurySettings>();
                if (USoundBase* Sound = Injury.BreathSound.LoadSynchronous())
                {
                    Pawn->InjuryBreath->SetSound(Sound);
                    Pawn->InjuryBreath->SetVolumeMultiplier(Injury.BreathVolume);
                }
                else if (!Injury.BreathSound.IsNull())
                {
                    UE_LOG(LogVanguardRoute, Warning, TEXT("Could not load the suit injury breath."));
                }
#if WITH_DEV_AUTOMATION_TESTS
                if (bVerifyInjuryAudio)
                {
                    Pawn->InjuryBreath->OnAudioPlaybackPercentNative.AddUObject(Pawn, &AVanguardCharacter::ObserveInjuryBreath);
                }
#endif
            }
        }
    }
}

UVanguardSession* AVanguardRouteGameMode::GetSession() const
{
    UGameInstance* Instance = GetGameInstance();
    return Instance ? Instance->GetSubsystem<UVanguardSession>() : nullptr;
}

bool AVanguardRouteGameMode::AtStation() const
{
    return Current == Segment::Vanguard;
}

float AVanguardRouteGameMode::GetOxygenPercent() const
{
    const UVanguardSession* Session = GetSession();
    return Session ? Session->Suit.Percent() : 100.0f;
}

std::optional<Abandoned::Domain::Expedition::DosimeterReading> AVanguardRouteGameMode::GetDosimeter() const
{
    if (const UVanguardSession* Session = GetSession(); Radiation && Session)
    {
        return Radiation->Read(RadiationPosition, Session->Dose);
    }
    return std::nullopt;
}

bool AVanguardRouteGameMode::TickRadiation(const FVector& Position, float DeltaSeconds,
    Abandoned::Domain::Expedition::RadiationDose& Dose)
{
    if (!Radiation)
    {
        return false;
    }
    RadiationPosition = Abandoned::Domain::Expedition::RadiationPosition::Outside;
    if (BoundsOf(RadiationLayout->ExposureBounds).IsInsideOrOn(Position))
    {
        const bool bSheltered = RadiationLayout->ShelterBounds.ContainsByPredicate([&Position](const FSceneBlock& Shelter)
        {
            return BoundsOf(Shelter).IsInsideOrOn(Position);
        });
        RadiationPosition = bSheltered ? Abandoned::Domain::Expedition::RadiationPosition::Sheltered
                                      : Abandoned::Domain::Expedition::RadiationPosition::Exposed;
    }
    return Radiation->Advance(DeltaSeconds, RadiationPosition, Dose);
}

void AVanguardRouteGameMode::RestartRadiationPassage()
{
    if (Radiation && !bTravelling)
    {
        bTravelling = true;
        TravelToRadiationPassage(this);
    }
}

void AVanguardRouteGameMode::Resupply()
{
    if (UVanguardSession* Session = GetSession(); AtStation() && Session)
    {
        Session->Suit.Refill();
        bResupplied = true;
        UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard oxygen supply restored."));
    }
}

void AVanguardRouteGameMode::RecordLanding(double SurfaceHeight)
{
    if (AtStation() || !LastGroundHeight)
    {
        return;
    }
    // Round contact measurements to millimeters so physics noise does not turn a 3 m drop fatal.
    const double DropCm = FMath::RoundToDouble((*LastGroundHeight - SurfaceHeight) * 10.0) / 10.0;
    LastGroundHeight.reset();
    LandingImpact = Abandoned::Domain::Expedition::AssessFall(DropCm);
    if (LandingImpact != FallImpact::None)
    {
        UE_LOG(LogVanguardRoute, Display, TEXT("Passage fall %.1f cm: %s."), DropCm,
            LandingImpact == FallImpact::Fatal ? TEXT("fatal") : TEXT("minor injury"));
    }
}

void AVanguardRouteGameMode::TickPassage(AVanguardCharacter& Player, float DeltaSeconds)
{
    const UCharacterMovementComponent* Movement = Player.GetCharacterMovement();
    if (Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor())
    {
        LastGroundHeight = Movement->CurrentFloor.HitResult.ImpactPoint.Z;
    }
    InjuryRecovery.Advance(DeltaSeconds);
    if (LandingImpact == FallImpact::MinorInjury)
    {
        InjuryRecovery.Start(GetDefault<UVanguardInjurySettings>()->Recovery());
        Player.PlayInjuryBreath();
        LandingImpact = FallImpact::None;
    }
    Player.SetGroundSpeedScale(InjuryRecovery.GroundSpeedScale());
    const auto& Passage = *CastChecked<UVanguardPassageLayout>(Layout.Get());
    if (Passage.bEnableMantle)
    {
        Player.UpdateMantle(DeltaSeconds, LastGroundHeight);
    }
    const FVector Position = Player.GetActorLocation();
    const bool bNearDoor = FMath::Abs(Position.X - Passage.Door.Center.X) < 385.0 &&
        FMath::Abs(Position.Y - Passage.Door.Center.Y) < 135.0;
    FVector DoorPosition = Door->GetComponentLocation();
    const double TargetZ = Passage.Door.Center.Z + (bNearDoor ? VanguardAirlockTravelCm : 0.0);
    const double DoorStep = 340.0 * DeltaSeconds;
    DoorPosition.Z += FMath::Clamp(TargetZ - DoorPosition.Z, -DoorStep, DoorStep);
    Door->SetWorldLocation(DoorPosition, true);

    UVanguardSession* Session = GetSession();
    const bool bRadiationLimit = Session && TickRadiation(Position, DeltaSeconds, Session->Dose);
    const bool bOxygenDepleted = Session && Session->Suit.SpendInPassage(DeltaSeconds);
    using namespace Abandoned::Application::Expedition;
    const auto Outcome = ResolvePassage({
        .ReachedExit = BoundsOf(Passage.ExitZone).IsInside(Position),
        .Impact = LandingImpact,
        .OutOfBounds = Position.Z < -600.0,
        .OxygenDepleted = bOxygenDepleted,
        .RadiationLimitReached = bRadiationLimit});
    if (Outcome == PassageOutcome::ReturnToCheckpoint)
    {
        if (bRadiationLimit)
        {
            UE_LOG(LogVanguardRoute, Display, TEXT("Radiation dose limit reached. Returning to the passage checkpoint."));
#if WITH_DEV_AUTOMATION_TESTS
            if (bVerifyRadiation && Session)
            {
                Session->bRecoveredFromRadiation = true;
            }
#endif
        }
        if (Radiation)
        {
            RestartRadiationPassage();
        }
        else
        {
            bTravelling = true;
            UE_LOG(LogVanguardRoute, Display, TEXT("Passage checkpoint recovery: %s (%s)."),
                *Abandoned::Infrastructure::Expedition::WriteSegment(ReturnCheckpoint),
                bOxygenDepleted ? TEXT("oxygen depleted") : TEXT("fall"));
            TravelToSegment(this, ReturnCheckpoint);
        }
        return;
    }
    if (Outcome == PassageOutcome::EnterSanctuary)
    {
        if (const auto Next = Abandoned::Domain::Expedition::NextSegment(Current))
        {
            bTravelling = true;
            TravelToSegment(this, *Next);
        }
    }
}

void AVanguardRouteGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    AVanguardCharacter* Player = Controller ? Cast<AVanguardCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || bTravelling)
    {
        return;
    }
    if (!AtStation() && IsValid(Door))
    {
        TickPassage(*Player, DeltaSeconds);
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerifyRadiation && !bTravelling)
    {
        VerifyRadiation(*Player, DeltaSeconds);
    }
    else if (bVerifyFalls && !bTravelling)
    {
        VerifyFalls(*Player, DeltaSeconds);
    }
    else if (bVerifyRoute && !bTravelling)
    {
        Verify(*Player, DeltaSeconds);
    }
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void AVanguardRouteGameMode::VerifyFalls(AVanguardCharacter& Player, float DeltaSeconds)
{
    using namespace Abandoned::MovementProfile;
    constexpr double DropHeights[] = {250.0, 300.0, 325.0};
    constexpr double TestPlatformX = 1550.0;
    UCharacterMovementComponent& Movement = *Player.GetCharacterMovement();
    const auto FinishDrop = [&]
    {
        UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard fall verified at %.0f cm."), DropHeights[FallTestStep]);
        ++FallTestStep;
        bPlacedAtTestPlatform = false;
        bWalkedOffTestPlatform = false;
    };
    const auto Fail = [&](const TCHAR* Reason)
    {
        UE_LOG(LogVanguardRoute, Error, TEXT("Vanguard fall verification failed at %.0f cm: %s."),
            DropHeights[FallTestStep], Reason);
        bVerifyFalls = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    VerificationSeconds += DeltaSeconds;
    if (VerificationSeconds > 15.0f)
    {
        Fail(bWaitingForInjuryRecovery ? TEXT("recovery or audio playback timed out") : TEXT("test timed out"));
        return;
    }
    if (bWaitingForInjuryRecovery)
    {
        if (HasMinorInjuryNotice() || (bVerifyInjuryAudio && !Player.bInjuryBreathDecoded))
        {
            return;
        }
        if (!FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed))
        {
            Fail(TEXT("normal ground speed did not return"));
            return;
        }
        UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard minor injury slowed ground movement, then restored normal speed."));
        if (bVerifyInjuryAudio)
        {
            UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard injury breath decoded in audio mixer."));
        }
        bWaitingForInjuryRecovery = false;
        FinishDrop();
        return;
    }
    if (!bPlacedAtTestPlatform)
    {
        const double X = TestPlatformX + FallTestStep * 500.0;
        const FVector FloorProbe(X, 0.0, 0.0);
        FHitResult Floor;
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(VanguardTestFloor), false, &Player);
        UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
        if (!Cube || !GetWorld()->LineTraceSingleByChannel(Floor, FloorProbe + FVector(0, 0, 50),
            FloorProbe - FVector(0, 0, 50), ECC_Visibility, Query))
        {
            Fail(TEXT("could not place a test platform"));
            return;
        }
        const double Top = Floor.ImpactPoint.Z + DropHeights[FallTestStep];
        const auto Platform = Abandoned::Blockout::SpawnBox(*GetWorld(), *Cube, FVector(X, 0, Top - 10.0),
            FVector(120.0, 140.0, 20.0));
        if (!IsValid(Platform.Collision))
        {
            Fail(TEXT("test platform has no collision"));
            return;
        }
        LastGroundHeight.reset();
        Player.PlaceAt(FTransform(FRotator::ZeroRotator,
            FVector(X, 0, Top + StandingHalfHeight + 2.0)));
        bPlacedAtTestPlatform = true;
        return;
    }
    if (Movement.IsFalling())
    {
        const double X = TestPlatformX + FallTestStep * 500.0;
        bWalkedOffTestPlatform |= Player.GetActorLocation().X > X + 60.0;
    }
    else if (bWalkedOffTestPlatform)
    {
        if (HasMinorInjuryNotice() != (FallTestStep == 1) || FallTestStep >= 2)
        {
            Fail(TEXT("unexpected injury outcome"));
            return;
        }
        if (FallTestStep == 0)
        {
            if (!FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed) || Player.InjuryBreath->IsPlaying())
            {
                Fail(TEXT("a safe fall triggered injury feedback"));
                return;
            }
            FinishDrop();
            return;
        }
        const float InitialScale = GetDefault<UVanguardInjurySettings>()->InitialGroundSpeedScale;
        const bool bGroundSlowed = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed * InitialScale) &&
            FMath::IsNearlyEqual(Movement.MaxWalkSpeedCrouched, SlowSpeed * InitialScale);
        Player.SetSprint(true);
        const bool bSprintSlowed = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, SprintSpeed * InitialScale);
        Player.SetSprint(false);
        Movement.SetMovementMode(MOVE_Falling);
        const bool bAirSpeedUnchanged = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed);
        Movement.SetMovementMode(MOVE_Walking);
        const bool bGroundSlowedAgain = FMath::IsNearlyEqual(Movement.MaxWalkSpeed, WalkSpeed * InitialScale);
        if (!bGroundSlowed || !bSprintSlowed || !bAirSpeedUnchanged || !bGroundSlowedAgain)
        {
            Fail(TEXT("unexpected ground or air speed after injury"));
            return;
        }
        bWaitingForInjuryRecovery = true;
        return;
    }
    Player.AddMovementInput(FVector::ForwardVector);
}

void AVanguardRouteGameMode::Verify(AVanguardCharacter& Player, float DeltaSeconds)
{
    VerificationSeconds += DeltaSeconds;
    if (VerificationSeconds > 35.0f)
    {
        UE_LOG(LogVanguardRoute, Error, TEXT("Vanguard verification timed out at %s."),
            *Player.GetActorLocation().ToCompactString());
        bVerifyRoute = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    if (bCaptureRoute && !bCaptured && VerificationSeconds >= 0.55f)
    {
        FScreenshotRequest::RequestScreenshot(AtStation() ? TEXT("Vanguard-Station") : TEXT("Vanguard-Airlock"), false, false);
        bCaptured = true;
    }
    if (!AtStation())
    {
        if (VerificationSeconds < 0.9f)
        {
            return;
        }
        if (FlashlightInputStep < 7)
        {
            constexpr bool ExpectedOn[] = {false, true, true, false, false, true, true};
            auto* PC = Cast<APlayerController>(Player.GetController());
            if (!PC || Player.IsFlashlightOn() != ExpectedOn[FlashlightInputStep])
            {
                UE_LOG(LogVanguardRoute, Error, TEXT("Vanguard shoulder flashlight failed to toggle."));
                FPlatformMisc::RequestExitWithStatus(false, 1);
                bVerifyRoute = false;
                return;
            }
            if (FlashlightInputStep == 6)
            {
                UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard shoulder flashlight switched on, off and on with F."));
            }
            else
            {
                const EInputEvent Event = FlashlightInputStep % 2 == 0 ? IE_Pressed : IE_Released;
                PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::F, Event, FPlatformTime::Cycles64()));
            }
            ++FlashlightInputStep;
        }
        VerifyMantle(Player, DeltaSeconds);
        Player.AddMovementInput(FVector::ForwardVector);
        const FVector Closed = GetDefault<UVanguardPassageLayout>()->Door.Center;
        if (bCaptureRoute && !bCapturedOpenDoor && Player.GetActorLocation().X < Closed.X - 70.0 &&
            Door->GetComponentLocation().Z > Closed.Z + 185.0)
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Vanguard-Airlock-Open"), false, false);
            bCapturedOpenDoor = true;
        }
        if (!bDoorPassed && Player.GetActorLocation().X > Closed.X + 70.0)
        {
            bDoorPassed = true;
            UE_LOG(LogVanguardRoute, Display, TEXT("Verification crossed the moving passage airlock."));
        }
        return;
    }
    if (VerificationSeconds >= 0.95f && !bMovedToSupply)
    {
        const FVector Cabinet = SupplyCabinet->GetComponentLocation();
        const FVector From(Cabinet.X - 170.0, Cabinet.Y, Layout->Spawn.Z);
        const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Cabinet - Eye).Rotation(), From));
        const float Prior = GetOxygenPercent();
        Player.Interact();
        bMovedToSupply = true;
        if (Prior < 100.0f && HasResupplied() && GetOxygenPercent() == 100.0f && Player.IsFlashlightOn())
        {
            UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard shoulder flashlight stayed on across world travel."));
            UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard verification passed: passage transit and station resupply."));
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        else
        {
            UE_LOG(LogVanguardRoute, Error, TEXT("Vanguard verification failed: prior oxygen %.2f, current %.2f."),
                Prior, GetOxygenPercent());
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }
        bVerifyRoute = false;
    }
}
#endif

AVanguardCharacter::AVanguardCharacter()
{
    ShoulderLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("ShoulderFlashlight"));
    ShoulderLight->SetupAttachment(Camera);
    ShoulderLight->SetMobility(EComponentMobility::Movable);
    ShoulderLight->SetIntensityUnits(ELightUnits::Lumens);
    ShoulderLight->SetVisibility(false);

    InjuryBreath = CreateDefaultSubobject<UAudioComponent>(TEXT("SuitInjuryBreath"));
    InjuryBreath->SetupAttachment(Camera);
    InjuryBreath->bAutoActivate = false;
    InjuryBreath->bAllowSpatialization = false;

    CreateMantleVisuals();
}

void AVanguardCharacter::PlayInjuryBreath()
{
    if (InjuryBreath->Sound)
    {
        InjuryBreath->Stop();
#if WITH_DEV_AUTOMATION_TESTS
        bInjuryBreathDecoded = false;
#endif
        InjuryBreath->Play();
    }
}

#if WITH_DEV_AUTOMATION_TESTS
void AVanguardCharacter::ObserveInjuryBreath(const UAudioComponent*, const USoundWave*, float PlayedFraction)
{
    bInjuryBreathDecoded |= PlayedFraction > 0.02f;
}
#endif

void AVanguardCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Input.BindAction(MapAction(EKeys::E), ETriggerEvent::Started, this, &AVanguardCharacter::Interact);
    Input.BindAction(MapAction(EKeys::F), ETriggerEvent::Started, this, &AVanguardCharacter::ToggleFlashlight);
    Input.BindAction(MapAction(EKeys::R), ETriggerEvent::Started, this, &AVanguardCharacter::ResetRadiation);
}

void AVanguardCharacter::ResetRadiation()
{
    if (AVanguardRouteGameMode* Mode = GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>())
    {
        Mode->RestartRadiationPassage();
    }
}

bool AVanguardCharacter::IsFlashlightOn() const
{
    return ShoulderLight->IsVisible();
}

void AVanguardCharacter::ToggleFlashlight()
{
    const bool bOn = !IsFlashlightOn();
    ShoulderLight->SetVisibility(bOn);
    if (const auto* Mode = GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>())
    {
        if (UVanguardSession* Session = Mode->GetSession())
        {
            Session->bFlashlightOn = bOn;
        }
    }
}

void AVanguardCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    if (AVanguardRouteGameMode* Mode = GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>())
    {
        Mode->RecordLanding(Hit.ImpactPoint.Z);
    }
}

bool AVanguardCharacter::CanReachSupply() const
{
    const AVanguardRouteGameMode* Mode = GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>();
    if (!Mode || !IsValid(Mode->GetSupplyCabinet()))
    {
        return false;
    }
    const FVector Origin = Camera->GetComponentLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(VanguardSupply), false, this);
    FHitResult Hit;
    return GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + GetViewRotation().Vector() * SupplyReach,
        ECC_Visibility, Query) && Hit.GetComponent() == Mode->GetSupplyCabinet();
}

void AVanguardCharacter::Interact()
{
    if (CanReachSupply())
    {
        GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>()->Resupply();
    }
}

void AVanguardHUD::DrawHUD()
{
    Super::DrawHUD();
    const AVanguardRouteGameMode* Mode = GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>();
    const AVanguardCharacter* Player = Cast<AVanguardCharacter>(GetOwningPawn());
    if (!Mode || !Player)
    {
        return;
    }
    const auto Dosimeter = Mode->GetDosimeter();
    const TCHAR* Location = Mode->AtStation() ? TEXT("VANGUARD / OUTPOST 01") : TEXT("PASSAGE / VANGUARD OUTPOST 01");
    DrawText(Dosimeter ? TEXT("ENERGY TIER / EXTERIOR GALLERY") : Location,
        FLinearColor::White, 24.0f, 24.0f, nullptr, 1.2f);
    DrawText(FString::Printf(TEXT("Suit oxygen: %.0f%%"), Mode->GetOxygenPercent()), FLinearColor::White, 24.0f, 54.0f);
    if (Player->CanReachSupply())
    {
        DrawText(TEXT("[E] Connect oxygen supply"), FLinearColor(1.0, 0.68, 0.23), 24.0f, 80.0f);
    }
    else if (Mode->HasResupplied())
    {
        DrawText(TEXT("Supply restored"), FLinearColor(1.0, 0.68, 0.23), 24.0f, 80.0f);
    }
    DrawText(Player->IsFlashlightOn() ? TEXT("[F] Shoulder light on") : TEXT("[F] Shoulder light off"),
        FLinearColor::White, 24.0f, 106.0f);
    if (Mode->HasMinorInjuryNotice())
    {
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 129.0f, 330.0f, 23.0f);
        DrawText(TEXT("Suit: minor impact injury. Recovering gait."), FLinearColor(1.0, 0.68, 0.23), 24.0f, 132.0f);
    }
    if (Mode->HasClearedRadiation())
    {
        DrawText(TEXT("Station decontamination complete"), FLinearColor::White, 24.0f, 132.0f);
    }
    if (Player->CanMantle() && !Player->IsMantling())
    {
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 152.0f, 194.0f, 26.0f);
        DrawText(TEXT("[Hold Space] Climb ledge"), FLinearColor::White, 24.0f, 158.0f);
    }
    if (Dosimeter)
    {
        using Abandoned::Domain::Expedition::RadiationPosition;
        const TCHAR* Zone = Dosimeter->Position == RadiationPosition::Sheltered ? TEXT("SHELTERED")
            : Dosimeter->Position == RadiationPosition::Exposed ? TEXT("EXPOSED") : TEXT("SAFE APPROACH");
        const TCHAR* Phase = Dosimeter->FlareActive ? TEXT("FLARE ENDS IN") : TEXT("NEXT FLARE IN");
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 152.0f, 310.0f, 88.0f);
        DrawText(FString::Printf(TEXT("DOSIMETER / %s"), Zone), FLinearColor::White, 24.0f, 158.0f);
        DrawText(FString::Printf(TEXT("%s %d s"), Phase, FMath::CeilToInt(Dosimeter->SecondsToChange)),
            FLinearColor::White, 24.0f, 178.0f);
        DrawText(FString::Printf(TEXT("DOSE %.1f%% OF LIMIT  |  %.1f%%/s"), Dosimeter->DosePercent, Dosimeter->DosePerSecond),
            FLinearColor::White, 24.0f, 198.0f);
        DrawText(TEXT("[R] Restart passage"), FLinearColor::White, 24.0f, 218.0f);
    }
}
