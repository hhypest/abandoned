#pragma once

#include "Components/SceneCaptureComponent2D.h"
#include "PortalRoomViewComponent.generated.h"

class APlayerController;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class UPortalRoomViewComponent final : public USceneCaptureComponent2D
{
    GENERATED_BODY()

public:
    UPortalRoomViewComponent();

    static UPortalRoomViewComponent* Create(UWorld& World, const FTransform& Arrival,
        const FTransform& Departure, const FVector2D& HalfSize);

    // A null viewer closes both windows, including on a checkpoint reset.
    void UpdateView(const APlayerController* Viewer);

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FPortalLabFixture;
#endif

    void InitializeWindows(UStaticMesh& Plane, const FVector2D& HalfSize);

    UPROPERTY()
    TObjectPtr<UMaterialInterface> WindowMaterial;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Windows[2];

    FTransform Poses[2];
};
