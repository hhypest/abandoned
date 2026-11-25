#pragma once

#include "Components/StaticMeshComponent.h"
#include "CubePlacementComponent.generated.h"

class APuzzleCube;
class UBoxComponent;
class UObjectCarryComponent;

UCLASS()
class UCubePlacementComponent final : public UStaticMeshComponent
{
    GENERATED_BODY()

public:
    UCubePlacementComponent();
    static void MarkPressurePlate(UBoxComponent& Surface);
    // During the chest-height grip trial the aim ray still selects the original placement region.
    TOptional<double> AimDistance;
    void UpdatePreview(const APuzzleCube* Held, FVector ViewStart, FVector ViewEnd);
    void ClearPreview();
    void PlaceOrDrop(APuzzleCube& Cube, UObjectCarryComponent& Grip, FVector ViewStart, FVector ViewEnd);
    const TCHAR* PlacementPrompt() const;
    const UBoxComponent* GetPreviewSupport() const;
    bool IsPreviewBlocked() const { return bPreviewBlocked; }

protected:
    void BeginPlay() override;

private:
    struct FPlacement
    {
        UBoxComponent* Support = nullptr;
        FVector Center;
        FRotator Rotation;
        const TCHAR* BlockReason = nullptr;
    };
    bool FindPlacement(const APuzzleCube& Cube, FVector ViewStart, FVector ViewEnd, FPlacement& Placement) const;
    TWeakObjectPtr<UBoxComponent> PreviewSupport;
    bool bPreviewBlocked = false;
    const TCHAR* PreviewBlockReason = nullptr;
};
