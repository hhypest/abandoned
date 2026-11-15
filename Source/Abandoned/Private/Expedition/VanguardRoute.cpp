#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogVanguardRoute, Log, All);

namespace
{
using Abandoned::Domain::Expedition::Segment;
using Abandoned::Infrastructure::Expedition::ReadSegment;
using Abandoned::Infrastructure::Expedition::TravelToSegment;
constexpr double DoorTravel = 280.0;
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
    Current = ReadSegment(UGameplayStatics::ParseOption(Options, TEXT("Chamber"))) == Segment::Vanguard
        ? Segment::Vanguard : Segment::Passage;
    const auto* Passage = GetDefault<UVanguardPassageLayout>();
    const auto* Station = GetDefault<UVanguardStationLayout>();
    Layout = AtStation() ? static_cast<const UVanguardRouteLayout*>(Station) : Passage;
    UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
    if (!Cube || Layout->Room.IsEmpty() || Layout->Spawn.ContainsNaN() ||
        Layout->Room.ContainsByPredicate([](const FSceneBlock& Block) { return !Block.IsValid(); }) ||
        Layout->Lights.ContainsByPredicate([](const FSceneLight& Light) { return !Light.IsValid(); }) ||
        Layout->Finishes.ContainsByPredicate([](const FSceneFinish& Finish) { return !Finish.IsValid(); }) ||
        (AtStation() ? !Station->SupplyCabinet.IsValid() : (!Passage->Door.IsValid() || !Passage->ExitZone.IsValid() ||
            !Passage->FrameFit.IsValid() || Passage->FrameRotation.ContainsNaN() || Passage->LeafRotation.ContainsNaN())))
    {
        ErrorMessage = TEXT("Invalid Vanguard route geometry or device configuration.");
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
    if (AtStation())
    {
        SupplyCabinet = SpawnBlock(Station->SupplyCabinet, EComponentMobility::Static).Collision;
    }
    else
    {
        if (UVanguardSession* Session = GetSession())
        {
            Session->Suit.Refill();
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
#if WITH_DEV_AUTOMATION_TESTS
    bVerifyRoute = FParse::Param(FCommandLine::Get(), TEXT("VerifyEntryCourse")) ||
        FParse::Param(FCommandLine::Get(), TEXT("VerifyVanguard"));
    bCaptureRoute = FParse::Param(FCommandLine::Get(), TEXT("CaptureVanguard"));
#endif
    UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard segment ready: %s."), AtStation() ? TEXT("station") : TEXT("passage"));
}

void AVanguardRouteGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Player)
{
    if (Player && (Door || SupplyCabinet))
    {
        RestartPlayerAtTransform(Player, FTransform(FRotator::ZeroRotator, Layout->Spawn));
        Player->SetInputMode(FInputModeGameOnly());
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

void AVanguardRouteGameMode::Resupply()
{
    if (UVanguardSession* Session = GetSession(); AtStation() && Session)
    {
        Session->Suit.Refill();
        bResupplied = true;
        UE_LOG(LogVanguardRoute, Display, TEXT("Vanguard oxygen supply restored."));
    }
}

void AVanguardRouteGameMode::TickPassage(AVanguardCharacter& Player, float DeltaSeconds)
{
    const auto* Passage = GetDefault<UVanguardPassageLayout>();
    const FVector Position = Player.GetActorLocation();
    const bool bNearDoor = FMath::Abs(Position.X - Passage->Door.Center.X) < 385.0 &&
        FMath::Abs(Position.Y - Passage->Door.Center.Y) < 135.0;
    FVector DoorPosition = Door->GetComponentLocation();
    const double TargetZ = Passage->Door.Center.Z + (bNearDoor ? DoorTravel : 0.0);
    const double DoorStep = 340.0 * DeltaSeconds;
    DoorPosition.Z += FMath::Clamp(TargetZ - DoorPosition.Z, -DoorStep, DoorStep);
    Door->SetWorldLocation(DoorPosition, true);

    UVanguardSession* Session = GetSession();
    if ((Session && Session->Suit.SpendInPassage(DeltaSeconds)) || Position.Z < -600.0)
    {
        bTravelling = true;
        TravelToSegment(this, Segment::Weight);
        return;
    }
    if (BoundsOf(Passage->ExitZone).IsInside(Position))
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
    if (bVerifyRoute)
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
        if (Prior < 100.0f && HasResupplied() && GetOxygenPercent() == 100.0f)
        {
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

void AVanguardCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Input.BindAction(MapAction(EKeys::E), ETriggerEvent::Started, this, &AVanguardCharacter::Interact);
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
    DrawText(Mode->AtStation() ? TEXT("VANGUARD / OUTPOST 01") : TEXT("PASSAGE / VANGUARD OUTPOST 01"),
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
}
