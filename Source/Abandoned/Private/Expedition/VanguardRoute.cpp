#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Verification/VanguardFallVerification.h"
#include "Verification/VanguardRouteVerification.h"
#include "Verification/VanguardRadiationVerification.h"
#endif

#include "Expedition/VanguardSession.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/AbandonedTestController.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogVanguardRoute, Log, All);

namespace
{
using Abandoned::Application::Expedition::RouteEntry;
using Abandoned::Application::Expedition::VanguardDestination;
using Abandoned::Application::Expedition::VanguardStop;
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

void MovePassageAirlock(UBoxComponent& Door, const UVanguardPassageLayout& Passage, const FVector& Position, float DeltaSeconds)
{
    const bool bNearDoor = FMath::Abs(Position.X - Passage.Door.Center.X) < 385.0 &&
        FMath::Abs(Position.Y - Passage.Door.Center.Y) < 135.0;
    FVector DoorPosition = Door.GetComponentLocation();
    const double TargetZ = Passage.Door.Center.Z + (bNearDoor ? VanguardAirlockTravelCm : 0.0);
    const double DoorStep = 340.0 * DeltaSeconds;
    DoorPosition.Z += FMath::Clamp(TargetZ - DoorPosition.Z, -DoorStep, DoorStep);
    Door.SetWorldLocation(DoorPosition, true);
}
}

AVanguardRouteGameMode::AVanguardRouteGameMode()
{
    RouteState = CreateDefaultSubobject<UVanguardRouteStateComponent>(TEXT("RouteState"));
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
    auto Destination = std::get<VanguardDestination>(*Route);
    if (GetWorld()->WorldType != EWorldType::Game)
    {
        Destination.Entry = RouteEntry::Direct;
    }
    const bool bAtStation = Destination.Stop == VanguardStop::Station;
    const bool bRadiation = Destination.Stop == VanguardStop::RadiationPassage;
    const UVanguardPassageLayout* Passage = bRadiation ? GetDefault<UVanguardRadiationLayout>() : GetDefault<UVanguardPassageLayout>();
    const auto* Station = GetDefault<UVanguardStationLayout>();
    Layout = bAtStation ? static_cast<const UVanguardRouteLayout*>(Station) : Passage;
    UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
    if (!Cube || Layout->Room.IsEmpty() || Layout->Spawn.ContainsNaN() ||
        Layout->Room.ContainsByPredicate([](const FSceneBlock& Block) { return !Block.IsValid(); }) ||
        Layout->Lights.ContainsByPredicate([](const FSceneLight& Light) { return !Light.IsValid(); }) ||
        Layout->Finishes.ContainsByPredicate([](const FSceneFinish& Finish) { return !Finish.IsValid(); }) ||
        (bAtStation ? !Station->SupplyCabinet.IsValid() : (!Passage->Door.IsValid() || !Passage->ExitZone.IsValid() ||
            (Passage->bEnableMantle && (Passage->MantleLedges.IsEmpty() ||
                Passage->MantleLedges.ContainsByPredicate([](const FSceneBlock& Ledge) { return !Ledge.IsValid() || !Ledge.bCollision; }))) ||
            !Passage->FrameFit.IsValid() || Passage->FrameRotation.ContainsNaN() || Passage->LeafRotation.ContainsNaN())))
    {
        ErrorMessage = TEXT("Invalid Vanguard route geometry or device configuration.");
        return;
    }
    const UVanguardInjurySettings& Injury = *GetDefault<UVanguardInjurySettings>();
    if (!bAtStation && (!Injury.Recovery().IsValid() ||
        !FMath::IsFinite(Injury.BreathVolume) || Injury.BreathVolume < 0.0f || Injury.BreathVolume > 1.0f))
    {
        ErrorMessage = TEXT("Invalid Vanguard injury settings.");
        return;
    }
    if (!RouteState->Configure(Destination, bAtStation ? nullptr : Passage, ErrorMessage))
    {
        return;
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
    if (bAtStation)
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
    if (bAtStation ? !SupplyCabinet : !Door)
    {
        ErrorMessage = TEXT("Could not spawn the Vanguard route device.");
        return;
    }
    RouteState->EnterSegment();
#if WITH_DEV_AUTOMATION_TESTS
    if (!bAtStation && !bRadiation && FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguardOxygen")))
    {
        if (UVanguardSession* Session = UVanguardSession::Find(*this))
        {
            Session->Suit.SpendInPassage(555.0f);
        }
    }
    if (!bAtStation && !bRadiation && FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguardFalls")))
    {
        FallCheck = UVanguardFallVerification::Attach(*this);
    }
    RadiationCheck = UVanguardRadiationVerification::AttachIfRequested(*this, *Layout);
    if (!FallCheck.IsValid() && !RadiationCheck.IsValid())
    {
        RouteCheck = UVanguardRouteVerification::AttachIfRequested(*this, *Layout, Door.Get());
    }
#endif
    UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard segment ready: %s."),
        bAtStation ? TEXT("station") : (bRadiation ? TEXT("radiation passage") : TEXT("passage")));
}

void AVanguardRouteGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
    if (Player && (Door || SupplyCabinet))
    {
        RouteState->ResetMovement();
        RestartPlayerAtTransform(Player, FTransform(FRotator::ZeroRotator, Layout->Spawn));
        Player->SetInputMode(FInputModeGameOnly());
        const UVanguardSession* Session = UVanguardSession::Find(*this);
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
            if (!RouteState->AtStation())
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

void AVanguardRouteGameMode::Resupply()
{
#if WITH_DEV_AUTOMATION_TESTS
    RouteState->Resupply(UVanguardRouteVerification::AllowsResupplySave());
#else
    RouteState->Resupply(true);
#endif
}

void AVanguardCharacter::UpdatePassageMovement(UVanguardRouteStateComponent& Route, bool bEnableMantle, float DeltaSeconds)
{
    const UCharacterMovementComponent* Movement = GetCharacterMovement();
    std::optional<double> WalkableHeight;
    if (Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor())
    {
        WalkableHeight = Movement->CurrentFloor.HitResult.ImpactPoint.Z;
    }
    if (Route.AdvanceMovement(WalkableHeight, DeltaSeconds))
    {
        PlayInjuryBreath();
    }
    SetGroundSpeedScale(Route.GroundSpeedScale());
    if (bEnableMantle)
    {
        UpdateMantle(DeltaSeconds, Route.SupportHeight());
    }
}

void AVanguardRouteGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    AVanguardCharacter* Player = Controller ? Cast<AVanguardCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || RouteState->IsTravelling())
    {
        return;
    }
    if (!RouteState->AtStation() && IsValid(Door))
    {
        const auto& Passage = *CastChecked<UVanguardPassageLayout>(Layout.Get());
        Player->UpdatePassageMovement(*RouteState, Passage.bEnableMantle, DeltaSeconds);
        const FVector Position = Player->GetActorLocation();
        MovePassageAirlock(*Door, Passage, Position, DeltaSeconds);
        if (const auto Transition = RouteState->AdvancePassage(Position, DeltaSeconds))
        {
#if WITH_DEV_AUTOMATION_TESTS
            if (Transition->bDoseLimitReached && RadiationCheck.IsValid())
            {
                if (UVanguardSession* Session = UVanguardSession::Find(*this))
                {
                    RadiationCheck->ObserveDoseLimit(*Session);
                }
            }
#endif
            Player->CloseWristTerminal();
            TravelToWorld(this, Transition->Destination);
            return;
        }
    }
    Player->UpdateWristTerminal(*RouteState);
#if WITH_DEV_AUTOMATION_TESTS
    if (auto* RadiationTest = RadiationCheck.Get())
    {
        RadiationTest->CheckStep(*this, *Player, DeltaSeconds);
    }
    else if (auto* Check = FallCheck.Get())
    {
        Check->CheckStep(*this, *Player, DeltaSeconds);
    }
    else if (auto* RouteTest = RouteCheck.Get())
    {
        RouteTest->CheckStep(*this, *Player, DeltaSeconds);
    }
#endif
}

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
        if (const auto Recovery = Mode->RouteState->RestartRadiationPassage())
        {
            CloseWristTerminal();
            TravelToWorld(this, *Recovery);
        }
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
    if (UVanguardSession* Session = UVanguardSession::Find(*this))
    {
        Session->Suit.SetFlashlight(bOn);
    }
}

void AVanguardCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    if (AVanguardRouteGameMode* Mode = GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>())
    {
        Mode->RouteState->RecordLanding(Hit.ImpactPoint.Z);
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
