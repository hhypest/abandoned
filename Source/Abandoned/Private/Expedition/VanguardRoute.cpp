#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Verification/VanguardFallVerification.h"
#include "Verification/VanguardMantleVerification.h"
#endif

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
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
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
using Abandoned::Application::Expedition::RouteEntry;
using Abandoned::Application::Expedition::VanguardDestination;
using Abandoned::Application::Expedition::VanguardStop;
using Abandoned::Domain::Expedition::FallImpact;
using Abandoned::Domain::Expedition::RadiationPosition;
using Abandoned::Infrastructure::Expedition::TravelToWorld;
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
    using namespace Abandoned::Infrastructure::Expedition;
    const auto Route = ReadWorldDestination(Options, EntryWorld::Vanguard, ErrorMessage);
    if (!Route)
    {
        return;
    }
    Destination = std::get<VanguardDestination>(*Route);
    if (Destination.Stop == VanguardStop::RadiationPassage)
    {
        RadiationLayout = GetDefault<UVanguardRadiationLayout>();
    }
    if (GetWorld()->WorldType != EWorldType::Game)
    {
        Destination.Entry = RouteEntry::Direct;
    }
    if (!AtStation())
    {
        const UnrealCheckpointStore Store;
        RecoveryDestination = Abandoned::Application::Expedition::PassageRecovery(
            Destination, GetWorld()->WorldType == EWorldType::Game ? &Store : nullptr);
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
    if (UVanguardSession* Session = GetSession())
    {
        if (Destination.Entry == RouteEntry::Campaign)
        {
            const auto Saved = Abandoned::Infrastructure::Expedition::UnrealCheckpointStore{}.Read();
            Session->Suit.RestoreRecords(Saved.Archive);
        }
        if (AtStation())
        {
            const double ClearedDose = Session->Suit.EnterStation();
            bClearedRadiation = ClearedDose > 0.0;
            if (bClearedRadiation)
            {
                UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard station cleared remaining radiation dose: %.2f%%."), ClearedDose);
            }
        }
        else
        {
            Session->Suit.EnterPassage(Radiation.has_value());
#if WITH_DEV_AUTOMATION_TESTS
            if (!Radiation && FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguardOxygen")))
            {
                Session->Suit.SpendInPassage(555.0f);
            }
#endif
        }
    }
#if WITH_DEV_AUTOMATION_TESTS
    bVerifyRoute = FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCourse")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCheckpoint")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguard"));
    bCaptureRoute = FParse::Param(FCommandLine::Get(), TEXT("CaptureVanguard"));
    if (!AtStation() && !Radiation && FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguardFalls")))
    {
        FallCheck = UVanguardFallVerification::Attach(*this);
    }
    bVerifyWristTerminal = (Radiation || AtStation()) && FParse::Param(FCommandLine::Get(), TEXT("VerifyWristTerminal"));
    bCaptureWristTerminal = bVerifyWristTerminal && FParse::Param(FCommandLine::Get(), TEXT("CaptureWristTerminal"));
    bVerifyRadiation = (Radiation || AtStation()) &&
        (bVerifyWristTerminal || FParse::Param(FCommandLine::Get(), TEXT("VerifyRadiation")));
    bCaptureRadiation = FParse::Param(FCommandLine::Get(), TEXT("CaptureRadiation"));
    if (bVerifyRoute && !FallCheck.IsValid() && !bVerifyRadiation && !AtStation() && Passage->bEnableMantle)
    {
        MantleCheck = UVanguardMantleVerification::Attach(*this, Passage->MantleLedges[0], bCaptureRoute);
    }
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
            Pawn->ConfigureWristTerminal(BaseMaterial);
            const UVanguardFlashlightSettings& Settings = *GetDefault<UVanguardFlashlightSettings>();
            USpotLightComponent& Light = *Pawn->ShoulderLight;
            Light.SetRelativeLocation(Settings.ShoulderOffset);
            Light.SetIntensity(Settings.IntensityLumens);
            Light.SetAttenuationRadius(Settings.RangeCm);
            Light.SetOuterConeAngle(Settings.OuterConeDegrees);
            Light.SetInnerConeAngle(Settings.InnerConeDegrees);
            Light.SetVisibility(Session && Session->Suit.IsFlashlightOn());
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
    return Destination.Stop == VanguardStop::Station;
}

float AVanguardRouteGameMode::GetOxygenPercent() const
{
    const UVanguardSession* Session = GetSession();
    return Session ? Session->Suit.OxygenPercent() : 100.0f;
}

std::optional<Abandoned::Domain::Expedition::DosimeterReading> AVanguardRouteGameMode::GetDosimeter() const
{
    if (const UVanguardSession* Session = GetSession(); Radiation && Session)
    {
        return Radiation->Read(RadiationPosition, Session->Suit.RadiationDose());
    }
    return std::nullopt;
}

Abandoned::Application::Expedition::SuitTelemetry AVanguardRouteGameMode::GetSuitTelemetry() const
{
    const UVanguardSession* Session = GetSession();
    return {GetOxygenPercent(), Session ? Session->Suit.RadiationDose().Percent() : 0.0, GetDosimeter(), AtStation()};
}

bool AVanguardRouteGameMode::TickRadiation(const FVector& Position, float DeltaSeconds,
    Abandoned::Application::Expedition::SuitSession& Suit)
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
    return Suit.AdvanceExposure(*Radiation, RadiationPosition, DeltaSeconds);
}

void AVanguardRouteGameMode::RestartRadiationPassage()
{
    if (Destination.Stop == VanguardStop::RadiationPassage && RecoveryDestination && !bTravelling)
    {
        bTravelling = true;
        TravelToWorld(this, *RecoveryDestination);
    }
}

void AVanguardRouteGameMode::Resupply()
{
    if (UVanguardSession* Session = GetSession(); AtStation() && Session)
    {
        Session->Suit.Resupply();
        bResupplied = true;
        UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard oxygen supply restored."));
        if (Destination.Entry == RouteEntry::Campaign
#if WITH_DEV_AUTOMATION_TESTS
            && (!bVerifyRoute || FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCheckpoint")))
#endif
        )
        {
            bCheckpointWriteFailed = !Abandoned::Infrastructure::Expedition::UnrealCheckpointStore{}.RecordResupply();
            if (bCheckpointWriteFailed)
            {
                UE_LOG(LogVanguardRoute, Warning, TEXT("Could not save the resupply record. Previous data preserved."));
            }
        }
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
    const bool bRadiationLimit = Session && TickRadiation(Position, DeltaSeconds, Session->Suit);
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
        if (const auto* Chamber = std::get_if<ChamberDestination>(&*RecoveryDestination); Chamber && Chamber->Chamber)
        {
            UE_LOG(LogVanguardRoute, Display, TEXT("Passage checkpoint recovery: %s (%s)."),
                *Abandoned::Infrastructure::Expedition::WriteSegment(*Chamber->Chamber),
                bOxygenDepleted ? TEXT("oxygen depleted") : TEXT("fall"));
        }
        bTravelling = true;
        TravelToWorld(this, *RecoveryDestination);
        return;
    }
    if (Outcome == PassageOutcome::EnterSanctuary)
    {
        bTravelling = true;
        TravelToWorld(this, StationAfterPassage(Destination, *RecoveryDestination));
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
    if (!AtStation() && RecoveryDestination && IsValid(Door))
    {
        TickPassage(*Player, DeltaSeconds);
    }
    if (bTravelling)
    {
        Player->CloseWristTerminal();
        return;
    }
    Player->UpdateWristTerminal(*this);
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerifyRadiation)
    {
        if (!bVerifyWristTerminal || VerifyWristTerminal(*Player, DeltaSeconds))
        {
            VerifyRadiation(*Player, DeltaSeconds);
        }
    }
    else if (auto* Check = FallCheck.Get())
    {
        Check->CheckStep(*this, *Player, DeltaSeconds);
    }
    else if (bVerifyRoute)
    {
        Verify(*Player, DeltaSeconds);
    }
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
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
        if (auto* Check = MantleCheck.Get(); Check && !Check->CheckStep(Player, DeltaSeconds))
        {
            bVerifyRoute = false;
            return;
        }
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
    CreateWristTerminal();
}

void AVanguardCharacter::PlayInjuryBreath()
{
    if (InjuryBreath->Sound)
    {
        InjuryBreath->Stop();
        InjuryBreath->Play();
    }
}

void AVanguardCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Input.BindAction(MapAction(EKeys::E), ETriggerEvent::Started, this, &AVanguardCharacter::Interact);
    Input.BindAction(MapAction(EKeys::F), ETriggerEvent::Started, this, &AVanguardCharacter::ToggleFlashlight);
    Input.BindAction(MapAction(EKeys::R), ETriggerEvent::Started, this, &AVanguardCharacter::ResetRadiation);
    BindWristTerminalInput(Input);
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
            Session->Suit.SetFlashlight(bOn);
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
    if (!Player->IsMantling())
    {
        const float Y = Dosimeter ? 244.0f : 183.0f;
        const TCHAR* Tip = Player->IsWristTerminalOpen()
            ? TEXT("[T] Lower wrist  [Tab] Page  [PgUp/PgDn] Archive")
            : TEXT("[T] Wrist terminal");
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, Y, Player->IsWristTerminalOpen() ? 370.0f : 145.0f, 23.0f);
        DrawText(Tip, FLinearColor::White, 24.0f, Y + 3.0f);
    }
    if (Mode->HasCheckpointWriteFailed())
    {
        DrawText(TEXT("Could not save resupply record. Previous data preserved."), FLinearColor::White, 24.0f, 270.0f);
    }
    DrawWristReadout(*Player);
}
