#include "Puzzles/PortalLab.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Verification/PortalLabVerification.h"
#include "Verification/PortalDev20Verification.h"
#endif
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Expedition/Dev20Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Infrastructure/Puzzles/UnrealPortalShape.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/AbandonedTestController.h"
#include "Puzzles/PortalLabLayout.h"
#include "Puzzles/PuzzleCube.h"
#include "RenderCommandFence.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"
#include "World/Blockout.h"
#include "World/SceneAssembly.h"

DEFINE_LOG_CATEGORY_STATIC(LogPortalLab, Log, All);

namespace
{
using Abandoned::Domain::Puzzles::PortalEvent;
constexpr double HalfWidth = 118.0;
constexpr double HalfHeight = 145.0;
constexpr double Clearance = 6.0;

FTransform Link(const FTransform& Source, const FTransform& Destination)
{
    const FQuat Turn(FVector::UpVector, UE_PI);
    const FQuat Rotation = Destination.GetRotation() * Turn * Source.GetRotation().Inverse();
    return FTransform(Rotation, Destination.GetLocation() - Rotation.RotateVector(Source.GetLocation()));
}

bool Fits(const FVector& Local, const FVector& Extent)
{
    return Abandoned::Domain::Puzzles::FitsPortal(Local.Y, Local.Z, HalfWidth, HalfHeight, Extent.Y, Extent.Z);
}

bool IntersectsAperture(const FVector& Local, const FVector& Extent)
{
    return FMath::Abs(Local.Y) < HalfWidth + Extent.Y && FMath::Abs(Local.Z) < HalfHeight + Extent.Z;
}
}

APortalLabGameMode::APortalLabGameMode()
{
    DefaultPawnClass = APortalLabCharacter::StaticClass();
    PlayerControllerClass = AAbandonedTestController::StaticClass();
    HUDClass = APortalLabHUD::StaticClass();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> Surface(
        TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface"));
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> View(
        TEXT("/Game/Abandoned/Materials/M_PortalView.M_PortalView"));
    BaseMaterial = Surface.Object;
    WindowMaterial = View.Object;
}

void APortalLabGameMode::InitGame(const FString& Map, const FString& Options, FString& Error)
{
    Super::InitGame(Map, Options, Error);
    if (!Error.IsEmpty())
    {
        return;
    }
    using namespace Abandoned::Infrastructure::Expedition;
    const auto Destination = ReadWorldDestination(Options, EntryWorld::Portal, Error);
    if (!Destination)
    {
        return;
    }
    SetActorTickEnabled(false);
    Layout = GetDefault<UPortalLabLayout>();
    const auto* Dev20Route = std::get_if<Dev20Destination>(&*Destination);
    bDev20Run = Dev20Route != nullptr;
    bDev20Recovered = Dev20Route && Dev20Route->Entry == Dev20Entry::Recovery;
    CubeMesh = Abandoned::Blockout::LoadCube();
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    const auto MouthValid = [](const FPortalMouthLayout& Mouth)
    {
        return !Mouth.Center.ContainsNaN() && !Mouth.Console.ContainsNaN() && FMath::IsFinite(Mouth.Yaw);
    };
    if (!CubeMesh || !Plane || !BaseMaterial || !WindowMaterial || Layout->Room.IsEmpty() ||
        !MouthValid(Layout->Arrival) || !MouthValid(Layout->Departure) || Layout->Spawn.ContainsNaN() ||
        Layout->SmallCube.ContainsNaN() || Layout->LargeCube.ContainsNaN() ||
        (bDev20Run && (Layout->Dev20Station.ContainsNaN() || !Layout->Dev20Exit.IsValid())) ||
        !FMath::IsNearlyEqual(Layout->Arrival.Center.Z, Layout->Departure.Center.Z) ||
        FVector::DistSquared(Layout->Arrival.Center, Layout->Departure.Center) < FMath::Square(500.0))
    {
        Error = TEXT("Invalid portal lab layout or missing assets.");
        return;
    }

    UWorld& World = *GetWorld();
    Abandoned::Blockout::ApplyGravity(World);
    const auto Finishes = Abandoned::Scene::MakeFinishes(*this, BaseMaterial, Layout->Finishes);
    Lit = Finishes.FindRef(TEXT("Amber"));
    Unlit = Finishes.FindRef(TEXT("Unpowered"));
    Ballast = Finishes.FindRef(TEXT("Ballast"));
    if (!Lit || !Unlit || !Ballast || !Finishes.Contains(TEXT("Frame")))
    {
        Error = TEXT("Missing portal finishes.");
        return;
    }
    const auto Spawn = [&](const FSceneBlock& Block, EComponentMobility::Type Mobility = EComponentMobility::Static)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(World, *CubeMesh, Block.Center, Block.Size, Mobility,
            Block.bCollision ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
        Abandoned::Scene::SetFinish(Parts, Block, Finishes);
        return Parts;
    };
    for (const FSceneBlock& Block : Layout->Room)
    {
        if (!Block.IsValid() || !Finishes.Contains(Block.Finish) || !Spawn(Block).Collision)
        {
            Error = TEXT("Invalid portal lab room block.");
            return;
        }
    }
    for (const FSceneLight& Light : Layout->Lights)
    {
        if (!Light.IsValid() || !Abandoned::Scene::AddLight(World, Light))
        {
            Error = TEXT("Invalid portal lab light.");
            return;
        }
    }

    ViewTexture = NewObject<UTextureRenderTarget2D>(this);
    ViewTexture->RenderTargetFormat = RTF_RGBA16f;
    ViewTexture->AddressX = TA_Clamp;
    ViewTexture->AddressY = TA_Clamp;
    ViewTexture->InitAutoFormat(960, 540);
    // UpdateResourceImmediate queues render work. Complete the texture reference update
    // before the MID caches its bindless descriptor. This wait runs once during map load.
    ViewTexture->UpdateResourceImmediate();
    WindowMaterial->EnsureIsComplete();
    FRenderCommandFence TextureReady;
    TextureReady.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
    TextureReady.Wait();
    UMaterialInstanceDynamic* View = UMaterialInstanceDynamic::Create(WindowMaterial, this);
    View->SetTextureParameterValue(TEXT("PortalView"), ViewTexture);

    for (const auto* Data : {&Layout->Arrival, &Layout->Departure})
    {
        FPortalMouth& Mouth = Mouths.AddDefaulted_GetRef();
        const FRotator Rotation(0.0, Data->Yaw, 0.0);
        Mouth.Pose = FTransform(Rotation, Data->Center);
        const auto Part = [&](FVector Center, FVector Size, FName Finish, bool bCollision)
        {
            const auto Parts = Spawn({Mouth.Pose.TransformPosition(Center), Size, bCollision, Finish},
                EComponentMobility::Movable);
            if (Parts.Collision)
            {
                Parts.Collision->SetWorldRotation(Rotation);
            }
            return Parts;
        };
        Part(FVector(0, -HalfWidth - 10, 0), FVector(42, 20, HalfHeight * 2), TEXT("Frame"), true);
        Part(FVector(0, HalfWidth + 10, 0), FVector(42, 20, HalfHeight * 2), TEXT("Frame"), true);
        Part(FVector(0, 0, HalfHeight + 10), FVector(42, HalfWidth * 2 + 40, 20), TEXT("Frame"), true);
        Mouth.PowerBar = Part(FVector(24, 0, HalfHeight + 9), FVector(6, 148, 7), TEXT("Unpowered"), false).Visual;
        Mouth.Console = Spawn({Data->Console, FVector(35, 58, 62), true, TEXT("Frame")}).Collision;
        Mouth.Shutter = Part(FVector::ZeroVector, FVector(10, 2 * HalfWidth, 2 * HalfHeight),
            TEXT("Unpowered"), true).Collision;
        const auto Screen = Part(FVector(3, 0, 0), FVector(1), TEXT("Unpowered"), false);
        Mouth.Window = Screen.Visual;
        if (!Mouth.PowerBar || !Mouth.Console || !Mouth.Shutter || !Mouth.Window)
        {
            Error = TEXT("Could not assemble portal frame.");
            return;
        }
        Mouth.Window->SetStaticMesh(Plane);
        Mouth.Window->SetWorldRotation(FRotationMatrix::MakeFromZX(Mouth.Pose.GetUnitAxis(EAxis::X),
            Mouth.Pose.GetUnitAxis(EAxis::Y)).Rotator());
        Mouth.Window->SetWorldScale3D(FVector(HalfWidth / 50.0, HalfHeight / 50.0, 1));
        Mouth.Window->SetMaterial(0, View);
        Mouth.Window->SetCastShadow(false);
        Mouth.Window->SetVisibility(false);
    }

    AActor* CaptureOwner = World.SpawnActor<AActor>();
    if (!CaptureOwner)
    {
        Error = TEXT("Could not reserve the portal scene capture.");
        return;
    }
    Capture = NewObject<USceneCaptureComponent2D>(CaptureOwner);
    CaptureOwner->SetRootComponent(Capture);
    CaptureOwner->AddInstanceComponent(Capture);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->bEnableClipPlane = true;
    Capture->CaptureSource = SCS_SceneColorHDRNoAlpha;
    Capture->TextureTarget = ViewTexture;
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetTemporalAA(false);
    for (FPortalMouth& Mouth : Mouths)
    {
        Capture->HideComponent(Mouth.Window);
    }
    Capture->RegisterComponent();

    Cubes.SetNum(2);
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        Cubes[Index] = SpawnCube(Index);
        if (!Cubes[Index])
        {
            Error = TEXT("Could not spawn the portal lab cubes.");
            return;
        }
    }
    if (bDev20Run)
    {
        auto* Session = Abandoned::Infrastructure::Expedition::Dev20Session(this);
        if (!Session || !Spawn({Layout->Dev20Station - FVector(0, 0, 32), FVector(92, 71, 26),
            true, TEXT("Frame")}).Collision)
        {
            Error = TEXT("Could not initialize the DEV-20 recovery station.");
            return;
        }
        Session->Transfer.RecoverAtStation();
        RecoverDev20();
        if (!IsValid(Dev20))
        {
            Error = TEXT("Could not spawn DEV-20.");
            return;
        }
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (bDev20Run)
    {
        UPortalDev20Verification::AttachIfRequested(*this, *Layout, *Dev20, *Mouths[0].Console, bDev20Recovered);
    }
    else
    {
        UPortalLabVerification::AttachIfRequested(*this);
    }
#endif
    SetActorTickEnabled(true);
}

EPuzzleCubeType APortalLabGameMode::CubeType(int32 Index) const
{
    return Index == 0 ? EPuzzleCubeType::Small : EPuzzleCubeType::Large;
}

APuzzleCube* APortalLabGameMode::SpawnCube(int32 Index)
{
    APuzzleCube* Cube = GetWorld()->SpawnActor<APuzzleCube>();
    if (Cube)
    {
        Cube->Initialize(CubeType(Index), *CubeMesh);
        Cube->SetVisualMaterial(*Ballast);
        Cube->ResetPosition(Index == 0 ? Layout->SmallCube : Layout->LargeCube);
    }
    return Cube;
}

void APortalLabGameMode::HandleStartingNewPlayer_Implementation(APlayerController* Controller)
{
    if (Controller && Layout && IsActorTickEnabled())
    {
        RestartPlayerAtTransform(Controller, FTransform(FRotator::ZeroRotator, Layout->Spawn));
    }
}

void APortalLabGameMode::ResetLab(APortalLabCharacter& Player, bool bPowerFailure)
{
    Player.ReleaseHeldObject();
    Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
    Supply.Reset();
    PlayerState = {};
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        CubeStates[Index] = {};
        if (IsValid(Cubes[Index]))
        {
            Cubes[Index]->ResetPosition(Index == 0 ? Layout->SmallCube : Layout->LargeCube);
        }
        else
        {
            Cubes[Index] = SpawnCube(Index);
        }
    }
    bCrossedWithItem = false;
    bInterrupted = bPowerFailure;
    bExitOccupied = false;
    if (bDev20Run)
    {
        RecoverDev20();
        bDev20Recovered = bPowerFailure;
    }
    UpdateMouths();
}

void APortalLabGameMode::RecoverDev20()
{
    if (IsValid(Dev20))
    {
        Dev20->RestoreAt(Layout->Dev20Station);
    }
    else
    {
        Dev20 = GetWorld()->SpawnActor<ADev20Actor>(Layout->Dev20Station, FRotator::ZeroRotator);
    }
    Dev20State = {};
    bDev20Crossed = false;
}

void APortalLabGameMode::ToggleSupply()
{
    Supply.Observe(true, 0.0f);
    Supply.Observe(false, 0.0f);
    bInterrupted = false;
    bExitOccupied = false;
}

bool APortalLabGameMode::UseConsole(const UPrimitiveComponent& Target)
{
    for (const FPortalMouth& Mouth : Mouths)
    {
        if (&Target == Mouth.Console)
        {
            ToggleSupply();
            return true;
        }
    }
    return false;
}

void APortalLabGameMode::UpdateMouths()
{
    const bool bPowered = Supply.IsPowered();
    for (FPortalMouth& Mouth : Mouths)
    {
        Mouth.Shutter->SetCollisionEnabled(bPowered ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        Mouth.Shutter->SetVisibility(!bPowered, true);
        Mouth.PowerBar->SetMaterial(0, bPowered ? Lit : Unlit);
        if (!bPowered)
        {
            Mouth.Window->SetVisibility(false);
        }
    }
}

bool APortalLabGameMode::ExitClear(const UPrimitiveComponent& Body, const FTransform& Destination,
    const UPrimitiveComponent* Other) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PortalClearance), false, Body.GetOwner());
    if (Other)
    {
        Query.AddIgnoredActor(Other->GetOwner());
    }
    return !GetWorld()->OverlapBlockingTestByChannel(Destination.GetLocation(), Destination.GetRotation(),
        Body.GetCollisionObjectType(), Body.GetCollisionShape(-0.7f), Query,
        FCollisionResponseParams(Body.GetCollisionResponseToChannels()));
}

void APortalLabGameMode::ObserveTraveler(AActor& Actor, UPrimitiveComponent& Body, FTraveler& State,
    APortalLabCharacter* Player, int32 CubeIndex)
{
    const FVector Here = Body.GetComponentLocation();
    const FVector Previous = State.bTracked ? State.Previous : Here;
    const int32 PriorApproach = State.Approach;
    State.Approach = INDEX_NONE;
    State.Previous = Here;
    State.bTracked = true;
    UBoxComponent* Held = Player ? Player->GetHeldBody() : nullptr;
    for (int32 Index = 0; Index < Mouths.Num(); ++Index)
    {
        const FTransform& Source = Mouths[Index].Pose;
        const FTransform& Destination = Mouths[1 - Index].Pose;
        const FVector Local = Source.InverseTransformPositionNoScale(Here);
        const FVector Last = Source.InverseTransformPositionNoScale(Previous);
        const auto Measured = Abandoned::Infrastructure::Puzzles::TryPortalExtent(Body, Source);
        if (!Measured)
        {
            if (!State.bUnsupportedShape)
            {
                UE_LOG(LogPortalLab, Warning, TEXT("Portal transit rejected unsupported collision component %s (%s)."),
                    *Body.GetName(), *Body.GetClass()->GetName());
            }
            State = {};
            State.bUnsupportedShape = true;
            return;
        }
        State.bUnsupportedShape = false;
        const FVector Extent = Measured.GetValue();
        const bool bCrossedPlane = Last.X > 0.0 && Local.X <= 0.0;
        const FVector Threshold = bCrossedPlane ? FMath::Lerp(Last, Local, Last.X / (Last.X - Local.X)) : Local;
        const bool bCrossed = bCrossedPlane && FMath::Abs(Threshold.Y) <= HalfWidth &&
            FMath::Abs(Threshold.Z) <= HalfHeight;
        bool bFits = Fits(Threshold, Extent);
        const bool bMovingIn = Last.X >= Local.X - 0.1;
        bool bTouching = Local.X >= 0.0 && Local.X <= Extent.X + 1.0 && bMovingIn && IntersectsAperture(Local, Extent);
        FVector HeldExtent = FVector::ZeroVector;
        if (Held && (bTouching || bCrossed || (Local.X > 0.0 && Local.X <= 220.0)))
        {
            const FVector HeldLocal = Source.InverseTransformPositionNoScale(Held->GetComponentLocation());
            HeldExtent = Abandoned::Infrastructure::Puzzles::PortalBoxExtent(*Held, Source);
            bFits = bFits && Fits(HeldLocal, HeldExtent);
            bTouching = bTouching || (Local.X > 0.0 && bMovingIn && HeldLocal.X >= -180.0 &&
                HeldLocal.X <= HeldExtent.X && IntersectsAperture(HeldLocal, HeldExtent));
        }

        FTransform Through;
        FTransform Target;
        bool bClear = false;
        if (Supply.IsPowered() && bCrossed && bFits)
        {
            Through = Link(Source, Destination);
            const double Out = Destination.InverseTransformPositionNoScale(Through.TransformPosition(Here)).X;
            double ForwardOffset = Extent.X + Clearance - Out;
            if (Held)
            {
                const double HeldOut = Destination.InverseTransformPositionNoScale(
                    Through.TransformPosition(Held->GetComponentLocation())).X;
                ForwardOffset = FMath::Max(ForwardOffset, HeldExtent.X + Clearance - HeldOut);
            }
            Through.AddToTranslation(Destination.GetUnitAxis(EAxis::X) * FMath::Max(0.0, ForwardOffset));
            Target = Actor.GetActorTransform() * Through;
            bClear = ExitClear(Body, Target, Held) &&
                (!Held || ExitClear(*Held, Held->GetComponentTransform() * Through, &Body));
        }
        const PortalEvent Event = Abandoned::Domain::Puzzles::ObservePortal(
            Supply.IsPowered(), PriorApproach == Index, bTouching, bCrossed, bFits, bClear);
        if (Event == PortalEvent::Approach)
        {
            State.Approach = Index;
        }
        if (Event == PortalEvent::Interrupted)
        {
            if (Player)
            {
                APuzzleCube* Lost = Player->GetHeldCube();
                Player->ReleaseHeldObject();
                if (Lost)
                {
                    Lost->Destroy();
                }
                ResetLab(*Player, true);
                UE_LOG(LogPortalLab, Display, TEXT("Transit interrupted. Restored the checkpoint."));
            }
            else
            {
                if (&Actor == Dev20)
                {
                    RecoverDev20();
                    bDev20Recovered = true;
                }
                else if (Cubes.IsValidIndex(CubeIndex))
                {
                    Actor.Destroy();
                    Cubes[CubeIndex] = SpawnCube(CubeIndex);
                    State = {};
                }
            }
            return;
        }
        const bool bTransferred = Event == PortalEvent::Transfer && (!Player || Player->TransitHeldBody(Through));
        if (!bTransferred && Event != PortalEvent::Blocked && Event != PortalEvent::Transfer)
        {
            continue;
        }

        const FVector Before = Player ? Player->GetCharacterMovement()->Velocity : Body.GetPhysicsLinearVelocity();
        const FVector Angular = Player ? FVector::ZeroVector : Body.GetPhysicsAngularVelocityInRadians();
        FVector After;
        if (bTransferred)
        {
            if (Player)
            {
                Player->SetBase(nullptr);
                if (AController* Controller = Player->GetController())
                {
                    Controller->SetControlRotation((Through.GetRotation() * Controller->GetControlRotation().Quaternion()).Rotator());
                }
                bCrossedWithItem |= Held != nullptr;
                bDev20Crossed |= IsValid(Dev20) && Held == Dev20->GetBody();
            }
            Actor.SetActorTransform(Target, false, nullptr, ETeleportType::TeleportPhysics);
            After = Through.TransformVectorNoScale(Before);
            bExitOccupied = false;
#if WITH_DEV_AUTOMATION_TESTS
            InspectTransfer(Before, After);
#endif
        }
        else
        {
            const FVector Normal = Source.GetUnitAxis(EAxis::X);
            const FVector Correction = Normal * (Extent.X + Clearance - Local.X);
            if (Player && Held)
            {
                Player->TransitHeldBody(FTransform(Correction));
            }
            Actor.SetActorLocation(Actor.GetActorLocation() + Correction, false, nullptr, ETeleportType::TeleportPhysics);
            After = Before - FMath::Min(0.0, FVector::DotProduct(Before, Normal)) * Normal;
            bExitOccupied = true;
#if WITH_DEV_AUTOMATION_TESTS
            ++BlockedTransfers;
#endif
        }
        if (Player)
        {
            UCharacterMovementComponent& Movement = *Player->GetCharacterMovement();
            Movement.Velocity = After;
            Movement.UpdateComponentVelocity();
        }
        else
        {
            Body.SetPhysicsLinearVelocity(After);
            Body.SetPhysicsAngularVelocityInRadians(bTransferred ? Through.TransformVectorNoScale(Angular) : Angular);
        }
        State = {Body.GetComponentLocation(), INDEX_NONE, true};
        return;
    }
}

void APortalLabGameMode::CaptureView(const APortalLabCharacter& Player)
{
    if (!Supply.IsPowered())
    {
        return;
    }
    const APlayerController* Controller = Cast<APlayerController>(Player.GetController());
    const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager : nullptr;
    if (!Camera)
    {
        return;
    }
    const FVector Eye = Camera->GetCameraLocation();
    const FQuat Rotation = Camera->GetCameraRotation().Quaternion();
    int32 Visible = INDEX_NONE;
    double Nearest = FMath::Square(2200.0);
    for (int32 Index = 0; Index < Mouths.Num(); ++Index)
    {
        FPortalMouth& Mouth = Mouths[Index];
        const FVector Local = Mouth.Pose.InverseTransformPositionNoScale(Eye);
        if (Local.X > 0.0 && Local.SizeSquared() < Nearest &&
            FVector::DotProduct((Mouth.Pose.GetLocation() - Eye).GetSafeNormal(), Rotation.GetForwardVector()) > -0.1)
        {
            Visible = Index;
            Nearest = Local.SizeSquared();
        }
    }
    for (int32 Index = 0; Index < Mouths.Num(); ++Index)
    {
        if (Index != Visible)
        {
            Mouths[Index].Window->SetVisibility(false);
        }
    }
    if (Visible == INDEX_NONE)
    {
        return;
    }
    const ULocalPlayer* Local = Controller->GetLocalPlayer();
    const UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
    FSceneViewProjectionData Projection;
    if (!Local || !Viewport || !Viewport->Viewport || !Local->GetProjectionData(Viewport->Viewport, Projection))
    {
        return;
    }
    const FIntPoint ViewSize = Projection.GetConstrainedViewRect().Size();
    if (ViewSize.X <= 0 || ViewSize.Y <= 0)
    {
        return;
    }
    const double Scale = 960.0 / FMath::Max(ViewSize.X, ViewSize.Y);
    const int32 Width = FMath::Max(1, FMath::RoundToInt(ViewSize.X * Scale));
    const int32 Height = FMath::Max(1, FMath::RoundToInt(ViewSize.Y * Scale));
    if (ViewTexture->SizeX != Width || ViewTexture->SizeY != Height)
    {
        ViewTexture->ResizeTarget(Width, Height);
    }
    const FTransform& Exit = Mouths[1 - Visible].Pose;
    const FTransform Through = Link(Mouths[Visible].Pose, Exit);
    Capture->bUseCustomProjectionMatrix = true;
    Capture->CustomProjectionMatrix = Projection.ProjectionMatrix;
    Capture->FOVAngle = FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Projection.ProjectionMatrix.M[0][0]));
    Capture->ClipPlaneBase = Exit.GetLocation();
    Capture->ClipPlaneNormal = Exit.GetUnitAxis(EAxis::X);
    Capture->SetWorldLocationAndRotation(Through.TransformPosition(Eye), Through.GetRotation() * Rotation);
    if (!Mouths[Visible].Window->IsVisible())
    {
        // Submit the linked view before exposing its surface in the main viewport.
        Capture->CaptureScene();
        Mouths[Visible].Window->SetVisibility(true);
    }
    else
    {
        Capture->CaptureSceneDeferred();
    }
}

void APortalLabGameMode::Tick(float Delta)
{
    Super::Tick(Delta);
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    APortalLabCharacter* Player = Controller ? Cast<APortalLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player || bDev20Departed)
    {
        return;
    }
    ObserveTraveler(*Player, *Player->GetCapsuleComponent(), PlayerState, Player);
    for (int32 Index = 0; Index < Cubes.Num(); ++Index)
    {
        if (APuzzleCube* Cube = Cubes[Index]; IsValid(Cube) && Cube != Player->GetHeldCube() &&
            Cube->GetBody()->IsSimulatingPhysics())
        {
            ObserveTraveler(*Cube, *Cube->GetBody(), CubeStates[Index], nullptr, Index);
        }
        else
        {
            CubeStates[Index] = {};
        }
    }
    if (bDev20Run)
    {
        if (!IsValid(Dev20) || Dev20->GetActorLocation().Z < -240.0)
        {
            if (IsValid(Dev20) && Player->GetHeldDev20() == Dev20)
            {
                Player->ReleaseHeldObject();
            }
            RecoverDev20();
            bDev20Recovered = true;
        }
        if (IsValid(Dev20) && Player->GetHeldDev20() != Dev20)
        {
            ObserveTraveler(*Dev20, *Dev20->GetBody(), Dev20State, nullptr);
        }
        else
        {
            Dev20State = {};
        }
        const FSceneBlock& Exit = Layout->Dev20Exit;
        if (bDev20Crossed && IsValid(Dev20) && Player->GetHeldDev20() == Dev20 &&
            FBox::BuildAABB(Exit.Center, Exit.Size * 0.5).IsInside(Player->GetActorLocation()))
        {
            bDev20Departed = Abandoned::Infrastructure::Expedition::TravelWithDev20(
                this, Abandoned::Domain::Expedition::Dev20Stage::Station, true);
            if (bDev20Departed)
            {
                UE_LOG(LogPortalLab, Display, TEXT("DEV-20 departed the portal bay in the player's grip."));
                return;
            }
        }
    }
    UpdateMouths();
    CaptureView(*Player);
}

const TCHAR* APortalLabGameMode::GetMessage() const
{
    if (bDev20Run)
    {
        if (!Supply.IsPowered())
        {
            return bDev20Recovered ? TEXT("DEV-20 returned to its cradle. Power the pair [E].")
                : TEXT("DEV-20 transport. Power the pair [E], then take DEV-20 from its cradle.");
        }
        if (bExitOccupied)
        {
            return TEXT("Exit blocked. Clear the destination before crossing.");
        }
        return bDev20Crossed ? TEXT("Carry DEV-20 to the amber panel at the end of this bay.")
            : TEXT("Carry DEV-20 (34 kg) through the active frame.");
    }
    if (bInterrupted)
    {
        return TEXT("Power lost during transit. Checkpoint restored.");
    }
    if (!Supply.IsPowered())
    {
        return TEXT("Use the console [E] to power the portal pair.");
    }
    if (bExitOccupied)
    {
        return TEXT("Exit blocked. Clear the destination before crossing.");
    }
    return bCrossedWithItem ? TEXT("Transit complete. The carried item arrived with you.")
        : TEXT("Pick up a cube [E] and walk or jump through the active frame.");
}

#if WITH_DEV_AUTOMATION_TESTS
FPortalTransitObservation APortalLabGameMode::GetTransitObservation() const
{
    return {Supply.IsPowered(), PlayerState.Approach == 0, bInterrupted, bDev20Crossed,
        Transfers, LastBefore, LastAfter};
}

void APortalLabGameMode::InspectTransfer(const FVector& Before, const FVector& After)
{
    ++Transfers;
    LastBefore = Before;
    LastAfter = After;
    UE_LOG(LogPortalLab, Display, TEXT("Transfer %d: velocity %s -> %s, speed %.2f -> %.2f cm/s."),
        Transfers, *Before.ToCompactString(), *After.ToCompactString(), Before.Size(), After.Size());
}

#endif

void APortalLabGameMode::ResetPuzzle(APuzzleCharacter& Player)
{
    if (auto* LabPlayer = Cast<APortalLabCharacter>(&Player))
    {
        ResetLab(*LabPlayer);
    }
}

bool APortalLabGameMode::UseDevice(APuzzleCharacter&, UPrimitiveComponent& Target)
{
    return UseConsole(Target);
}

void APortalLabHUD::DrawHUD()
{
    Super::DrawHUD();
    if (const auto* Lab = GetWorld()->GetAuthGameMode<APortalLabGameMode>(); Canvas && Lab)
    {
        DrawText(TEXT("TRANSIT / POWERED PORTALS"), FLinearColor::White, 24, 24, nullptr, 1.1f);
        DrawText(Lab->GetMessage(), FLinearColor::White, 24, 54, nullptr, 1.0f);
        DrawText(TEXT("Reset [R]"), FLinearColor::White, 24, 80, nullptr, 0.9f);
        const float X = Canvas->ClipX * 0.5f;
        const float Y = Canvas->ClipY * 0.5f;
        DrawLine(X - 6, Y, X + 6, Y, FLinearColor::White, 1.0f);
        DrawLine(X, Y - 6, X, Y + 6, FLinearColor::White, 1.0f);
        if (const auto* Player = Cast<APortalLabCharacter>(GetOwningPawn()))
        {
            if (const TCHAR* Prompt = Player->GetPlacementPrompt())
            {
                DrawText(Prompt, FLinearColor::White, X + 18.0f, Y + 14.0f);
            }
        }
    }
}
