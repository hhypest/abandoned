#include "Puzzles/PortalLab.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SphereComponent.h"
#include "Verification/PortalShapeVerification.h"
#endif
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AssetCompilingManager.h"
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
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/AbandonedTestController.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PortalLabLayout.h"
#include "Puzzles/PuzzleCube.h"
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
    // Publish the render resource before a material caches its Vulkan texture reference.
    ViewTexture->UpdateResourceImmediate();
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
    bVerifyDev20 = bDev20Run && FParse::Param(FCommandLine::Get(), TEXT("VerifyDev20"));
    bVerifyDev20RecoveryArrival = bVerifyDev20 && bDev20Recovered &&
        FParse::Param(FCommandLine::Get(), TEXT("VerifyDev20Recovery"));
    bVerify = !bDev20Run && FParse::Param(FCommandLine::Get(), TEXT("VerifyPortalLab"));
    bCapture = FParse::Param(FCommandLine::Get(), bDev20Run ? TEXT("CaptureDev20") : TEXT("CapturePortalLab"));
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
        Mouths[Index].Window->SetVisibility(Index == Visible);
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
    Capture->CaptureSceneDeferred();
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
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerify)
    {
        Verify(*Player, Delta);
    }
    if (bVerifyDev20)
    {
        VerifyDev20(*Player, Delta);
    }
#endif
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
void APortalLabGameMode::VerifyDev20(APortalLabCharacter& Player, float Delta)
{
    using Abandoned::Domain::Expedition::Dev20MassKg;
    VerificationTime += Delta;
    const auto Fail = [this](const TCHAR* Reason)
    {
        UE_LOG(LogPortalLab, Error, TEXT("DEV-20 portal test failed at step %d: %s"), VerificationStep, Reason);
        bVerifyDev20 = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto Next = [this]
    {
        ++VerificationStep;
        VerificationTime = 0.0f;
    };
    const auto LookFrom = [&Player](FVector At, FVector Target)
    {
        At.Z = 92.0;
        Player.PlaceAt(FTransform((Target - At - FVector(0, 0, Abandoned::MovementProfile::CameraHeight)).Rotation(), At));
    };
    const auto PowerAndTake = [&]
    {
        LookFrom(Layout->Arrival.Console + FVector(0, 120, 0), Layout->Arrival.Console);
        Player.Interact();
        LookFrom(Layout->Dev20Station - FVector(100, -30, 0), Dev20->GetActorLocation());
        Player.Interact();
        if (!Supply.IsPowered() || Player.GetHeldDev20() != Dev20 ||
            !FMath::IsNearlyEqual(Player.GetCharacterMovement()->MaxWalkSpeed,
                Abandoned::MovementProfile::WalkSpeed * Abandoned::Domain::Puzzles::CarrySpeedScale(Dev20MassKg), 0.05f))
        {
            return false;
        }
        Player.PlaceAt(FTransform(FRotator(-18, 0, 0), Layout->Arrival.Center - FVector(235, 0, 53)));
        PlayerState = {};
        return true;
    };
    if (!IsValid(Dev20) || VerificationTime > 8.0f)
    {
        Fail(TEXT("Could not complete the physical route"));
        return;
    }
    if (bVerifyDev20RecoveryArrival)
    {
        if (VerificationTime < 0.25f)
        {
            return;
        }
        TArray<AActor*> Units;
        UGameplayStatics::GetAllActorsOfClass(this, ADev20Actor::StaticClass(), Units);
        if (Units.Num() != 1 || Player.GetHeldDev20() || Supply.IsPowered() ||
            !Dev20->GetActorLocation().Equals(Layout->Dev20Station, 4.0))
        {
            Fail(TEXT("Recovery from the previous world did not restore one DEV-20 at the cradle"));
            return;
        }
        UE_LOG(LogPortalLab, Display, TEXT("DEV-20 recovery verification passed: lost in Button, returned to the station."));
        bVerifyDev20 = false;
        FPlatformMisc::RequestExitWithStatus(false, 0);
        return;
    }
    switch (VerificationStep)
    {
    case 0:
        if (VerificationTime > 0.4f)
        {
            if (!FMath::IsNearlyEqual(Dev20->GetBody()->GetMass(), Dev20MassKg, 0.05f))
            {
                Fail(TEXT("Chaos mass is not 34 kg"));
                return;
            }
            LookFrom(Layout->Dev20Station - FVector(133, -35, 0), Dev20->GetActorLocation());
            VerifyDev20Item = Dev20;
            Next();
        }
        break;
    case 1:
        if (bCapture && VerificationTime >= 0.12f)
        {
            FScreenshotRequest::RequestScreenshot(TEXT("DEV20-Station"), false, false);
            bCapture = false;
        }
        if (VerificationTime > 0.20f)
        {
            if (!PowerAndTake())
            {
                Fail(TEXT("Could not power the pair or pick up DEV-20 with the expected carry speed"));
                return;
            }
            UE_LOG(LogPortalLab, Display, TEXT("DEV-20: 34 kg Chaos mass and carry slowdown confirmed."));
            Next();
        }
        break;
    case 2:
        if (VerificationTime > 0.30f)
        {
            Player.Move(FVector::ForwardVector);
            if (PlayerState.Approach == 0)
            {
                ToggleSupply();
                Next();
            }
        }
        break;
    case 3:
        if (bInterrupted)
        {
            if (Supply.IsPowered() || Player.GetHeldDev20() || VerifyDev20Item.Get() != Dev20 ||
                !Dev20->GetActorLocation().Equals(Layout->Dev20Station, 4.0))
            {
                Fail(TEXT("Power loss failed to recover the same DEV-20 at its station"));
                return;
            }
            UE_LOG(LogPortalLab, Display, TEXT("DEV-20: loss of portal power returned the same item to its cradle."));
            if (!PowerAndTake())
            {
                Fail(TEXT("Recovered DEV-20 could not be carried again"));
                return;
            }
            Next();
        }
        break;
    case 4:
        if (VerificationTime >= 0.40f)
        {
            Player.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            Player.GetCharacterMovement()->Velocity = FVector(660, 0, 560);
            Next();
        }
        break;
    case 5:
        Player.Move(FVector::ForwardVector);
        if (bDev20Crossed)
        {
            if (Player.GetHeldDev20() != VerifyDev20Item.Get() || Transfers != 1 ||
                LastBefore.X <= 300.0 || !LastAfter.Equals(FVector(-LastBefore.Y, LastBefore.X, LastBefore.Z), 0.05))
            {
                Fail(TEXT("Portal traversal lost DEV-20 or its carrier's velocity"));
                return;
            }
            UE_LOG(LogPortalLab, Display, TEXT("DEV-20: same 34 kg body remained in the grip after the portal."));
            Next();
        }
        break;
    case 6:
        if (Player.GetHeldDev20() != Dev20)
        {
            Fail(TEXT("DEV-20 was lost before the chamber hand-off"));
            return;
        }
        Player.Move(FVector::ForwardVector);
        break;
    }
}

void APortalLabGameMode::InspectTransfer(const FVector& Before, const FVector& After)
{
    ++Transfers;
    LastBefore = Before;
    LastAfter = After;
    UE_LOG(LogPortalLab, Display, TEXT("Transfer %d: velocity %s -> %s, speed %.2f -> %.2f cm/s."),
        Transfers, *Before.ToCompactString(), *After.ToCompactString(), Before.Size(), After.Size());
}

void APortalLabGameMode::Verify(APortalLabCharacter& Player, float Delta)
{
    VerificationTime += Delta;
    const auto Fail = [this, &Player](const TCHAR* Reason)
    {
        UE_LOG(LogPortalLab, Error, TEXT("Portal lab test failed at step %d: %s. Player %s, traversals %d, blocked %d."),
            VerificationStep, Reason, *Player.GetActorLocation().ToCompactString(), Transfers, BlockedTransfers);
        bVerify = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    const auto Next = [this](const TCHAR* Message)
    {
        UE_LOG(LogPortalLab, Display, TEXT("Portal lab: %s"), Message);
        VerificationTime = 0.0f;
        ++VerificationStep;
    };
    const auto LookFrom = [&Player](FVector At, FVector Target)
    {
        At.Z = 92.0;
        Player.PlaceAt(FTransform((Target - At - FVector(0, 0, 75)).Rotation(), At));
    };
    const auto VelocityRotated = [this]
    {
        return LastBefore.X > 300.0 && LastAfter.Equals(FVector(-LastBefore.Y, LastBefore.X, LastBefore.Z), 0.05);
    };
    if (VerificationTime > 8.0f)
    {
        Fail(TEXT("Timed out waiting for a physical transition"));
        return;
    }
    switch (VerificationStep)
    {
    case 0:
        if (VerificationTime >= 0.2f)
        {
            const FVector Center = Layout->Arrival.Center;
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(ClosedPortal), false, &Player);
            if (!Abandoned::Verification::VerifyPortalShapeProjection())
            {
                Fail(TEXT("Incorrect capsule or rotated box aperture extent"));
                return;
            }
            FHitResult Hit;
            GetWorld()->LineTraceSingleByChannel(Hit, Center - FVector(100, 0, 0),
                Center + FVector(100, 0, 0), ECC_Visibility, Query);
            if (Supply.IsPowered() || Hit.GetComponent() != Mouths[0].Shutter)
            {
                Fail(TEXT("The unpowered portal was not solid"));
                return;
            }
            LookFrom(Layout->Arrival.Console + FVector(0, 120, 0), Layout->Arrival.Console);
            Player.Interact();
            if (!Supply.IsPowered())
            {
                Fail(TEXT("Portal console did not power the pair"));
                return;
            }
            // Present an unsupported body crossing a powered entrance from its approach side.
            auto* Sphere = NewObject<USphereComponent>(&Player);
            const FTransform& Entrance = Mouths[0].Pose;
            Sphere->SetWorldLocation(Entrance.TransformPosition(FVector(-1, 0, 0)));
            FTraveler Unsupported{Entrance.TransformPosition(FVector(1, 0, 0)), 0, true};
            const FTransform Before = Player.GetActorTransform();
            const FVector Speed = Player.GetCharacterMovement()->Velocity;
            ObserveTraveler(Player, *Sphere, Unsupported, &Player);
            if (!Unsupported.bUnsupportedShape || Unsupported.bTracked || Unsupported.Approach != INDEX_NONE ||
                !Player.GetActorTransform().Equals(Before) || !Player.GetCharacterMovement()->Velocity.Equals(Speed))
            {
                Fail(TEXT("Unsupported shape changed the traveler or retained a pending crossing"));
                return;
            }
            UE_LOG(LogPortalLab, Display, TEXT("Portal collision shapes verified: capsule, rotated box and rejected sphere."));
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Center - FVector(320, 0, 53)));
#if WITH_EDITOR
            FAssetCompilingManager::Get().FinishAllCompilation();
#endif
            Next(TEXT("Unpowered shutter blocked the aperture. Console supplied the pair."));
        }
        break;
    case 1:
        if (VerificationTime >= 0.50f)
        {
            if (!Mouths[0].Window->IsVisible() || Mouths[1].Window->IsVisible())
            {
                Fail(TEXT("Portal view failed to select the entrance"));
                return;
            }
            TArray<FLinearColor> Pixels;
            const int32 X = ViewTexture->SizeX / 2;
            const int32 Y = ViewTexture->SizeY / 2;
            FTextureRenderTargetResource* Resource = ViewTexture->GameThread_GetRenderTargetResource();
            if (!Resource || !Resource->ReadLinearColorPixels(Pixels, FReadSurfaceDataFlags(RCM_MinMax),
                FIntRect(X - 1, Y - 1, X + 2, Y + 2)) || Pixels.IsEmpty())
            {
                Fail(TEXT("Could not read the linked aperture capture"));
                return;
            }
            const FLinearColor Sample = Pixels[Pixels.Num() / 2];
            UE_LOG(LogPortalLab, Display, TEXT("Linked scene center %s via %s."),
                *Sample.ToString(), *GetNameSafe(Mouths[0].Window->GetMaterial(0)));
            if (Sample.R <= 0.0f || Sample.R <= Sample.B)
            {
                Fail(TEXT("The aperture capture did not contain the destination's warm wall panel"));
                return;
            }
            if (bCapture)
            {
                FScreenshotRequest::RequestScreenshot(TEXT("PortalLab-Active"), false, false);
            }
            Next(TEXT("Powered entrance captured the linked room."));
        }
        break;
    case 2:
        if (VerificationTime >= 0.15f)
        {
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Spawn));
            PlayerState = {};
            Cubes[0]->ResetPosition(Layout->Arrival.Center - FVector(100, 0, 46));
            CubeStates[0] = {};
            Cubes[0]->GetBody()->SetPhysicsLinearVelocity(FVector(1350, 0, 30));
            Next(TEXT("Sent a free physical cube through the frame."));
        }
        break;
    case 3:
        if (Transfers == 1)
        {
            if (!VelocityRotated() || Cubes[0]->GetActorLocation().X < Layout->Departure.Center.X - 80.0)
            {
                Fail(TEXT("The free cube did not arrive with the rotated velocity"));
                return;
            }
            Cubes[0]->ResetPosition(Layout->SmallCube);
            CubeStates[0] = {};
            Cubes[1]->ResetPosition(Layout->Departure.Center + FVector(0, 38, -103));
            CubeStates[1] = {};
            Player.PlaceAt(FTransform(FRotator::ZeroRotator, Layout->Arrival.Center - FVector(135, 0, 53)));
            PlayerState = {};
            Next(TEXT("Free cube retained speed through the 90 degree turn. Testing a blocked exit."));
        }
        break;
    case 4:
        Player.Move(FVector::ForwardVector);
        if (BlockedTransfers > 0)
        {
            if (Transfers != 1 || Player.GetActorLocation().X >= Layout->Arrival.Center.X)
            {
                Fail(TEXT("An occupied exit admitted the player"));
                return;
            }
            Cubes[1]->ResetPosition(Layout->LargeCube);
            CubeStates[1] = {};
            LookFrom(Layout->SmallCube - FVector(130, 0, 0), Cubes[0]->GetActorLocation());
            Player.Interact();
            VerifyItem = Player.GetHeldCube();
            if (VerifyItem.Get() != Cubes[0])
            {
                Fail(TEXT("Player did not pick up the test cube"));
                return;
            }
            Player.PlaceAt(FTransform(FRotator(-18, 0, 0), Layout->Arrival.Center - FVector(235, 0, 53)));
            PlayerState = {};
            Next(TEXT("Occupied exit stopped transit. The player picked up a cube."));
        }
        break;
    case 5:
        if (VerificationTime >= 0.28f)
        {
            Player.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            Player.GetCharacterMovement()->Velocity = FVector(660, 0, 560);
            Next(TEXT("Entering the portal airborne with the cube held."));
        }
        break;
    case 6:
        Player.Move(FVector::ForwardVector);
        if (Transfers == 2)
        {
            if (!VerifyItem.IsValid() || Player.GetHeldCube() != VerifyItem.Get() ||
                !VelocityRotated() || FMath::Abs(LastBefore.Z) < 25.0 ||
                !FMath::IsNearlyEqual(Player.GetViewRotation().Yaw, 90.0, 1.0) ||
                !Player.GetCharacterMovement()->Velocity.Equals(LastAfter, 0.1))
            {
                Fail(TEXT("Transfer lost motion, aim or ownership of the held object"));
                return;
            }
            Next(TEXT("Player arrived airborne with the same cube, rotated view and unchanged speed."));
        }
        break;
    case 7:
        if (VerificationTime > 0.22f)
        {
            if (Transfers != 2 || !VerifyItem.IsValid() || Player.GetHeldCube() != VerifyItem.Get())
            {
                Fail(TEXT("Immediate reverse transfer or loss of grip"));
                return;
            }
            Player.PlaceAt(FTransform(FRotator(-18, -90, 0), Layout->Departure.Center + FVector(0, 238, -53)));
            PlayerState = {};
            Next(TEXT("No repeat transit at the exit. Returning with the same cube."));
        }
        break;
    case 8:
        if (VerificationTime > 0.30f)
        {
            Player.Move(FVector::ForwardVector);
        }
        if (Transfers == 3)
        {
            if (!VerifyItem.IsValid() || Player.GetHeldCube() != VerifyItem.Get() ||
                LastBefore.Y >= -300.0 || !LastAfter.Equals(FVector(LastBefore.Y, -LastBefore.X, LastBefore.Z), 0.05))
            {
                Fail(TEXT("Reverse transit lost the cube or the inverse velocity transform"));
                return;
            }
            Next(TEXT("The inverse passage retained the cube and speed."));
        }
        break;
    case 9:
        if (VerificationTime > 0.22f)
        {
            Player.PlaceAt(FTransform(FRotator(-18, 0, 0), Layout->Arrival.Center - FVector(238, 0, 53)));
            PlayerState = {};
            Next(TEXT("Approaching the first portal again to test loss of power."));
        }
        break;
    case 10:
        if (VerificationTime > 0.30f)
        {
            Player.Move(FVector::ForwardVector);
            if (PlayerState.Approach == 0)
            {
                ToggleSupply();
                Next(TEXT("Cut the supply while the carried object overlapped the active frame."));
            }
        }
        break;
    case 11:
        if (bInterrupted)
        {
            if (Supply.IsPowered() || VerifyItem.IsValid() || Player.GetHeldBody() || !IsValid(Cubes[0]) ||
                !Player.GetActorLocation().Equals(Layout->Spawn, 4.0))
            {
                Fail(TEXT("Power loss did not destroy the item and restore the checkpoint"));
                return;
            }
            UE_LOG(LogPortalLab, Display, TEXT("Portal lab verification passed: view, power, free cube, blocked exit, carried item, inertia and interruption."));
            bVerify = false;
            FPlatformMisc::RequestExitWithStatus(false, 0);
        }
        break;
    }
}
#endif

void APortalLabCharacter::ResetChamber()
{
    if (APortalLabGameMode* Lab = GetWorld()->GetAuthGameMode<APortalLabGameMode>())
    {
        Lab->ResetLab(*this);
    }
}

bool APortalLabCharacter::InteractWithDevice(UPrimitiveComponent& Target)
{
    APortalLabGameMode* Lab = GetWorld()->GetAuthGameMode<APortalLabGameMode>();
    return Lab && Lab->UseConsole(Target);
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
