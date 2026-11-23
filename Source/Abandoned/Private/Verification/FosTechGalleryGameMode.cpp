#include "Verification/FosTechGalleryGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "NaniteSceneProxy.h"
#include "StaticMeshSceneProxyDesc.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "World/FosTech.h"

namespace
{
DEFINE_LOG_CATEGORY_STATIC(LogFosTechGallery, Log, All);

// In reading order, rows of 5, 5 and 3. Separate both plate parts so the base is visible.
struct FExhibit
{
    const TCHAR* Path;
    double Tilt;
};
constexpr FExhibit Collection[] = {
    {Abandoned::FosTech::SmallCube, 18}, {Abandoned::FosTech::LargeCube, 18},
    {Abandoned::FosTech::PlateBase, 52}, {Abandoned::FosTech::PlateTop, 52},
    {Abandoned::FosTech::Button, 16}, {Abandoned::FosTech::Switch, 16},
    {Abandoned::FosTech::Emitter, 18}, {Abandoned::FosTech::Receiver, 18},
    {Abandoned::FosTech::Relay, 16}, {Abandoned::FosTech::Frame, 8},
    {Abandoned::FosTech::Leaf, 8},
    {TEXT("/Game/Abandoned/Meshy/EQP_01/EQP-01/StaticMeshes/EQP-01.EQP-01"), 6},
    {TEXT("/Game/Abandoned/Meshy/EQP_05/EQP-05/StaticMeshes/EQP-05.EQP-05"), 14}
};

UStaticMeshComponent* Install(UWorld& World, UStaticMesh& Mesh, const FTransform& Transform)
{
    AStaticMeshActor* Actor = World.SpawnActor<AStaticMeshActor>();
    if (!Actor) { return nullptr; }
    UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->SetWorldTransform(Transform);
    Component->SetStaticMesh(&Mesh);
    return Component;
}

void Light(UWorld& World, FRotator Rotation, float Intensity)
{
    if (ADirectionalLight* Actor = World.SpawnActor<ADirectionalLight>(FVector::ZeroVector, Rotation))
    {
        ULightComponent* Lamp = Actor->GetLightComponent();
        Lamp->SetMobility(EComponentMobility::Movable);
        Lamp->SetIntensity(Intensity);
        Lamp->SetCastShadows(true);
    }
}

void Fail(const TCHAR* Reason)
{
    UE_LOG(LogFosTechGallery, Error, TEXT("FosTech gallery failed: %s"), Reason);
    FPlatformMisc::RequestExitWithStatus(false, 1);
}
}
#endif

AFosTechGalleryGameMode::AFosTechGalleryGameMode()
{
#if WITH_DEV_AUTOMATION_TESTS
    DefaultPawnClass = nullptr;
    HUDClass = nullptr;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void AFosTechGalleryGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!FParse::Param(FCommandLine::Get(), TEXT("VerifyFosTechGallery")))
    {
        SetActorTickEnabled(false);
        return;
    }
    UWorld& World = *GetWorld();
    GalleryCamera = World.SpawnActor<ACameraActor>(FVector(0, 640, 300), FRotator(0, -90, 0));
    if (!GalleryCamera.IsValid()) { return Fail(TEXT("camera missing")); }
    UCameraComponent* Camera = GalleryCamera->GetCameraComponent();
    Camera->SetFieldOfView(74.f);
    auto& Post = Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod = true;
    Post.AutoExposureMethod = AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Post.AutoExposureApplyPhysicalCameraExposure = false;
    Post.bOverride_AutoExposureBias = true;
    Post.AutoExposureBias = 0.f;

    Light(World, FRotator(-26, -65, 0), 3.8f);
    Light(World, FRotator(-12, -126, 0), 1.4f);
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Cube || !Install(World, *Cube, FTransform(FQuat::Identity, FVector(0, -95, 300), FVector(12, .10, 7))))
    {
        return Fail(TEXT("backdrop missing"));
    }

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Collection); ++Index)
    {
        const FExhibit& Entry = Collection[Index];
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Entry.Path);
        if (!Mesh) { return Fail(Entry.Path); }
        const FBox Bounds = Mesh->GetBoundingBox();
        const FQuat Angle = FRotator(0, 16, Entry.Tilt).Quaternion();
        const FVector RotatedSize = Bounds.TransformBy(FTransform(Angle)).GetSize();
        if (RotatedSize.X < 0.01 || RotatedSize.Z < 0.01) { return Fail(TEXT("empty exhibit bounds")); }
        const double Scale = FMath::Min(151.0 / RotatedSize.X, 131.0 / RotatedSize.Z);
        const int32 Row = Index / 5;
        const int32 Columns = Row == 2 ? 3 : 5;
        const FVector Center((Index % 5 - 0.5 * (Columns - 1)) * 172.0, 0.0, 300.0 + (1 - Row) * 157.0);
        const FTransform Placement(Angle, Center - Angle.RotateVector(Bounds.GetCenter() * Scale), FVector(Scale));
        UStaticMeshComponent* Exhibit = Install(World, *Mesh, Placement);
        if (!Exhibit) { return Fail(Entry.Path); }
        Exhibits.Add(Exhibit);
    }
}

void AFosTechGalleryGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!GalleryCamera.IsValid()) { return; }
    APlayerController* Player = GetWorld()->GetFirstPlayerController();
    if (!Player) { return; }
    if (Player->GetViewTarget() != GalleryCamera.Get()) { Player->SetViewTarget(GalleryCamera.Get()); }
    ++GalleryFrame;
    if (GalleryFrame == 150)
    {
        int32 NaniteCount = 0;
        for (const auto& Handle : Exhibits)
        {
            const UStaticMeshComponent* Exhibit = Handle.Get();
            if (!Exhibit || !Exhibit->GetSceneProxy()) { return Fail(TEXT("missing render proxy")); }
            Nanite::FMaterialAudit Materials;
            const bool Eligible = FStaticMeshSceneProxyDesc(Exhibit).ShouldCreateNaniteProxy(&Materials);
            const bool IsNanite = Exhibit->GetSceneProxy()->IsNaniteMesh();
            UE_LOG(LogFosTechGallery, Display,
                TEXT("FosTech mesh=%s built=%d eligible=%d proxy=%d auditedSlots=%d materialErrors=%d"),
                *Exhibit->GetStaticMesh()->GetPathName(), Exhibit->HasValidNaniteData(), Eligible, IsNanite,
                Materials.Entries.Num(), Materials.bHasAnyError);
            if (Eligible != IsNanite || (IsNanite && Materials.bHasAnyError))
            {
                return Fail(TEXT("render path disagrees with component or invalid material"));
            }
            NaniteCount += IsNanite;
        }
        UE_LOG(LogFosTechGallery, Display, TEXT("FosTech.SampleBegin meshes=%d nanite=%d"), Exhibits.Num(), NaniteCount);
        CSV_EVENT_GLOBAL(TEXT("FosTech.SampleBegin"));
    }
    else if (GalleryFrame == 450)
    {
        CSV_EVENT_GLOBAL(TEXT("FosTech.SampleEnd"));
        UE_LOG(LogFosTechGallery, Display, TEXT("FosTech.SampleEnd"));
    }
    else if (GalleryFrame == 480)
    {
        FScreenshotRequest::RequestScreenshot(TEXT("FosTech-Gallery"), false, false);
    }
    else if (GalleryFrame == 492)
    {
        if (GEngine) { GEngine->Exec(GetWorld(), TEXT("CsvProfile STOP")); }
    }
    else if (GalleryFrame == 510)
    {
        UE_LOG(LogFosTechGallery, Display, TEXT("FosTech gallery finished"));
        FPlatformMisc::RequestExitWithStatus(false, 0);
    }
}
#endif
