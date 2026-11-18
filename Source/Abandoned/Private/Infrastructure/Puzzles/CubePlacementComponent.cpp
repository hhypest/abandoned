#include "Infrastructure/Puzzles/CubePlacementComponent.h"

#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Infrastructure/Puzzles/ObjectCarryComponent.h"
#include "Infrastructure/Puzzles/PuzzleInteractionSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Puzzles/PuzzleCube.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName PressurePlateTag(TEXT("Puzzle.PressurePlate"));
constexpr double SnapRange = 75.0;
constexpr double SnapGap = 0.5;
}

UCubePlacementComponent::UCubePlacementComponent()
{
    SetAbsolute(true, true, true);
    SetCollisionProfileName(TEXT("NoCollision"));
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    SetCastShadow(false);
    SetReceivesDecals(false);
    SetVisibility(false);

    static const ConstructorHelpers::FObjectFinder<UStaticMesh> PreviewMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> PreviewMaterial(
        TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent_OneSided.Widget3DPassThrough_Translucent_OneSided"));
    if (PreviewMesh.Succeeded() && PreviewMaterial.Succeeded())
    {
        SetStaticMesh(PreviewMesh.Object);
        SetMaterial(0, PreviewMaterial.Object);
    }
}

void UCubePlacementComponent::BeginPlay()
{
    Super::BeginPlay();
    if (GetStaticMesh())
    {
        UMaterialInstanceDynamic* PreviewMaterial = CreateDynamicMaterialInstance(0);
        if (PreviewMaterial)
        {
            PreviewMaterial->SetVectorParameterValue(TEXT("TintColorAndOpacity"),
                GetDefault<UPuzzleInteractionSettings>()->PlacementGhostTint);
        }
    }
}

void UCubePlacementComponent::MarkPressurePlate(UBoxComponent& Surface)
{
    Surface.ComponentTags.AddUnique(PressurePlateTag);
}

const UBoxComponent* UCubePlacementComponent::GetPreviewSupport() const
{
    return PreviewSupport.Get();
}

bool UCubePlacementComponent::FindPlacement(const APuzzleCube& Cube, FVector ViewStart, FVector ViewEnd, FPlacement& Placement) const
{
    const FVector Current = Cube.GetActorLocation();
    const double Half = Cube.GetHalfSize();
    FCollisionQueryParams TraceQuery(SCENE_QUERY_STAT(CubePlacementSurface), false, GetOwner());
    TraceQuery.AddIgnoredActor(&Cube);
    const auto TrySurface = [&](const FHitResult& Hit)
    {
        UBoxComponent* Surface = Cast<UBoxComponent>(Hit.GetComponent());
        if (!Surface || Hit.ImpactNormal.Z < 0.9)
        {
            return false;
        }
        const APuzzleCube* LowerCube = Cast<APuzzleCube>(Surface->GetOwner());
        if (!Surface->ComponentHasTag(PressurePlateTag) && !LowerCube)
        {
            return false;
        }
        const FVector SurfaceCenter = Surface->GetComponentLocation();
        const FVector Target = SurfaceCenter + FVector(0.0, 0.0, Surface->GetScaledBoxExtent().Z + Half + SnapGap);
        if (Current.Z < Target.Z - 8.0 || FVector::DistSquared(Current, Target) > FMath::Square(SnapRange))
        {
            return false;
        }
        const FRotator Rotation(0.0, Surface->GetComponentRotation().Yaw, 0.0);
        Placement = {Surface, Target, Rotation};
        if (LowerCube)
        {
            switch (LowerCube->GetStackBlock(Cube))
            {
            case ECubeStackBlock::Size:
                Placement.BlockReason = TEXT("Large cubes need a large support");
                break;
            case ECubeStackBlock::Occupied:
                Placement.BlockReason = TEXT("This cube already supports a stack");
                break;
            case ECubeStackBlock::Height:
                Placement.BlockReason = TEXT("Maximum stack height: three cubes");
                break;
            case ECubeStackBlock::Unstable:
                Placement.BlockReason = TEXT("Wait for the support to settle");
                break;
            case ECubeStackBlock::None:
                break;
            }
            if (Placement.BlockReason)
            {
                return false;
            }
        }
        const FCollisionShape Shape = FCollisionShape::MakeBox(FVector(Half));
        FCollisionQueryParams PlacementQuery(SCENE_QUERY_STAT(CubePlacementClearance), false, &Cube);
        PlacementQuery.AddIgnoredComponent(Surface);
        if (GetWorld()->OverlapBlockingTestByProfile(Target, Rotation.Quaternion(), TEXT("PhysicsActor"), Shape, PlacementQuery))
        {
            Placement.BlockReason = TEXT("Not enough room for the cube");
            return false;
        }
        // Sweep a small inset so a cube already touching the floor can slide clear of it.
        FHitResult Obstruction;
        if (GetWorld()->SweepSingleByProfile(Obstruction, Current, Target, Rotation.Quaternion(),
            TEXT("PhysicsActor"), FCollisionShape::MakeBox(FVector(Half - 1.0)), PlacementQuery))
        {
            Placement.BlockReason = TEXT("Something blocks the placement path");
            return false;
        }
        return true;
    };
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(Hit, ViewStart, ViewEnd,
        ECC_Visibility, TraceQuery) && TrySurface(Hit))
    {
        return true;
    }
    return GetWorld()->LineTraceSingleByChannel(Hit, Current, Current - FVector(0.0, 0.0, Half + SnapRange),
        ECC_Visibility, TraceQuery) && TrySurface(Hit);
}

void UCubePlacementComponent::PlaceOrDrop(APuzzleCube& Cube, UObjectCarryComponent& Grip, FVector ViewStart, FVector ViewEnd)
{
    FPlacement Placement;
    const bool bCanSnap = FindPlacement(Cube, ViewStart, ViewEnd, Placement);
    ClearPreview();
    Grip.ReleaseComponent();
    if (bCanSnap)
    {
        Cube.SnapTo(*Placement.Support, Placement.Center, Placement.Rotation);
    }
}

const TCHAR* UCubePlacementComponent::PlacementPrompt() const
{
    if (bPreviewBlocked)
    {
        return PreviewBlockReason ? PreviewBlockReason : TEXT("Placement blocked");
    }
    if (const UBoxComponent* Support = PreviewSupport.Get())
    {
        return Cast<APuzzleCube>(Support->GetOwner()) ? TEXT("[E] Snap to cube") : TEXT("[E] Snap to plate");
    }
    return TEXT("[E] Drop cube");
}

void UCubePlacementComponent::UpdatePreview(const APuzzleCube* Held, FVector ViewStart, FVector ViewEnd)
{
    FPlacement Placement;
    const bool bCanPlace = IsValid(Held) && FindPlacement(*Held, ViewStart, ViewEnd, Placement);
    if (Placement.Support)
    {
        PreviewSupport = Placement.Support;
        PreviewBlockReason = Placement.BlockReason;
        if (bPreviewBlocked == bCanPlace)
        {
            const UPuzzleInteractionSettings* Art = GetDefault<UPuzzleInteractionSettings>();
            if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(GetMaterial(0)))
            {
                Material->SetVectorParameterValue(TEXT("TintColorAndOpacity"),
                    bCanPlace ? Art->PlacementGhostTint : Art->BlockedGhostTint);
            }
        }
        bPreviewBlocked = !bCanPlace;
        SetWorldTransform(FTransform(Placement.Rotation, Placement.Center,
            FVector(Held->GetHalfSize() / 50.0)));
        SetVisibility(GetStaticMesh() != nullptr);
        return;
    }
    ClearPreview();
}

void UCubePlacementComponent::ClearPreview()
{
    if (bPreviewBlocked)
    {
        if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(GetMaterial(0)))
        {
            Material->SetVectorParameterValue(TEXT("TintColorAndOpacity"), GetDefault<UPuzzleInteractionSettings>()->PlacementGhostTint);
        }
        bPreviewBlocked = false;
    }
    PreviewSupport.Reset();
    PreviewBlockReason = nullptr;
    SetVisibility(false);
}
