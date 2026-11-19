#include "Infrastructure/Puzzles/PortalRoomViewComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Infrastructure/Puzzles/UnrealPortalTransform.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderCommandFence.h"
#include "SceneView.h"
#include "UObject/ConstructorHelpers.h"

UPortalRoomViewComponent::UPortalRoomViewComponent()
{
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> View(
        TEXT("/Game/Abandoned/Materials/M_PortalView.M_PortalView"));
    WindowMaterial = View.Object;
    bCaptureEveryFrame = false;
    bCaptureOnMovement = false;
    bEnableClipPlane = true;
    CaptureSource = SCS_SceneColorHDRNoAlpha;
    ShowFlags.SetMotionBlur(false);
    ShowFlags.SetTemporalAA(false);
}

UPortalRoomViewComponent* UPortalRoomViewComponent::Create(UWorld& World, const FTransform& Arrival,
    const FTransform& Departure, const FVector2D& HalfSize)
{
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!Plane || !GetDefault<UPortalRoomViewComponent>()->WindowMaterial)
    {
        return nullptr;
    }
    AActor* Owner = World.SpawnActor<AActor>();
    if (!Owner)
    {
        return nullptr;
    }
    auto* RoomView = NewObject<UPortalRoomViewComponent>(Owner);
    Owner->SetRootComponent(RoomView);
    Owner->AddInstanceComponent(RoomView);
    RoomView->Poses[0] = Arrival;
    RoomView->Poses[1] = Departure;
    RoomView->InitializeWindows(*Plane, HalfSize);
    return RoomView;
}

void UPortalRoomViewComponent::InitializeWindows(UStaticMesh& Plane, const FVector2D& HalfSize)
{
    TextureTarget = NewObject<UTextureRenderTarget2D>(this);
    TextureTarget->RenderTargetFormat = RTF_RGBA16f;
    TextureTarget->AddressX = TA_Clamp;
    TextureTarget->AddressY = TA_Clamp;
    TextureTarget->InitAutoFormat(960, 540);
    // UpdateResourceImmediate queues render work. Complete the texture reference update
    // before the MID caches its bindless descriptor. This wait runs once during map load.
    TextureTarget->UpdateResourceImmediate();
    WindowMaterial->EnsureIsComplete();
    FRenderCommandFence TextureReady;
    TextureReady.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
    TextureReady.Wait();
    UMaterialInstanceDynamic* View = UMaterialInstanceDynamic::Create(WindowMaterial, this);
    View->SetTextureParameterValue(TEXT("PortalView"), TextureTarget);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Windows); ++Index)
    {
        const FTransform& Pose = Poses[Index];
        auto* Window = NewObject<UPortalApertureMeshComponent>(GetOwner());
        GetOwner()->AddInstanceComponent(Window);
        // Apertures stay in the room while the capture root follows the linked camera.
        Window->SetupAttachment(this);
        Window->SetAbsolute(true, true, true);
        Window->SetWorldLocation(Pose.TransformPosition(FVector(3, 0, 0)));
        Window->SetWorldRotation(FRotationMatrix::MakeFromZX(Pose.GetUnitAxis(EAxis::X),
            Pose.GetUnitAxis(EAxis::Y)).Rotator());
        Window->SetWorldScale3D(FVector(HalfSize.X / 50.0, HalfSize.Y / 50.0, 1));
        Window->SetMobility(EComponentMobility::Movable);
        Window->SetStaticMesh(&Plane);
        Window->SetCollisionProfileName(TEXT("NoCollision"));
        Window->SetMaterial(0, View);
        Window->SetCastShadow(false);
        Window->SetVisibility(false);
        Windows[Index] = Window;
        HideComponent(Window);
    }
    RegisterComponent();
    for (auto& Window : Windows)
    {
        Window->RegisterComponent();
    }
}

void UPortalRoomViewComponent::UpdateView(const APlayerController* Viewer)
{
    const APlayerCameraManager* Camera = Viewer ? Viewer->PlayerCameraManager : nullptr;
    if (!Camera)
    {
        for (auto& Window : Windows)
        {
            Window->SetVisibility(false);
        }
        return;
    }
    const FVector Eye = Camera->GetCameraLocation();
    const FQuat Rotation = Camera->GetCameraRotation().Quaternion();
    int32 Visible = INDEX_NONE;
    double Nearest = FMath::Square(2200.0);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Windows); ++Index)
    {
        const FVector Local = Poses[Index].InverseTransformPositionNoScale(Eye);
        if (Local.X > 0.0 && Local.SizeSquared() < Nearest &&
            FVector::DotProduct((Poses[Index].GetLocation() - Eye).GetSafeNormal(), Rotation.GetForwardVector()) > -0.1)
        {
            Visible = Index;
            Nearest = Local.SizeSquared();
        }
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Windows); ++Index)
    {
        if (Index != Visible)
        {
            Windows[Index]->SetVisibility(false);
        }
    }
    if (Visible == INDEX_NONE)
    {
        return;
    }
    const ULocalPlayer* Local = Viewer->GetLocalPlayer();
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
    if (TextureTarget->SizeX != Width || TextureTarget->SizeY != Height)
    {
        TextureTarget->ResizeTarget(Width, Height);
    }
    const FTransform& Exit = Poses[1 - Visible];
    const FTransform Through = Abandoned::Infrastructure::Puzzles::PortalLinkTransform(Poses[Visible], Exit);
    bUseCustomProjectionMatrix = true;
    CustomProjectionMatrix = Projection.ProjectionMatrix;
    FOVAngle = FMath::RadiansToDegrees(2.0 * FMath::Atan(1.0 / Projection.ProjectionMatrix.M[0][0]));
    ClipPlaneBase = Exit.GetLocation();
    ClipPlaneNormal = Exit.GetUnitAxis(EAxis::X);
    SetWorldLocationAndRotation(Through.TransformPosition(Eye), Through.GetRotation() * Rotation);
    if (!Windows[Visible]->IsVisible())
    {
        // Submit the linked view before exposing its surface in the main viewport.
        CaptureScene();
        Windows[Visible]->SetVisibility(true);
    }
    else
    {
        CaptureSceneDeferred();
    }
}
