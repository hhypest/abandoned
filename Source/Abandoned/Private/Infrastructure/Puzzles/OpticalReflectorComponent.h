#pragma once

#include "Components/ActorComponent.h"
#include "OpticalReflectorComponent.generated.h"

class APuzzleCharacter;
class UBoxComponent;
class UPrimitiveComponent;

UCLASS()
class UOpticalReflectorComponent final : public UActorComponent
{
    GENERATED_BODY()

public:
    static constexpr double HeightCm = 108.0;
    bool Build(FVector Home, FVector Dock, UStaticMesh& Mesh, UMaterialInterface& Base, UMaterialInterface& Steel);
    bool IsMirrorTarget(const UPrimitiveComponent* Target) const;
    bool CanAimMirror(const APuzzleCharacter& Player) const;
    void PickUp(APuzzleCharacter& Player);
    void PlaceMirror(const APuzzleCharacter& Player);
    void RestoreMirror();
    void Align(FVector Start, FVector Pivot, FVector Point);
    UBoxComponent* GetBody() const { return Mirror.Get(); }
    UBoxComponent* GetSurface() const { return MirrorSurface.Get(); }
    UBoxComponent* PortableBody() const { return bMirrorDocked ? nullptr : Mirror.Get(); }

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class UOpticsVerification;
#endif
    UPROPERTY()
    TObjectPtr<UBoxComponent> Mirror;

    UPROPERTY()
    TObjectPtr<UBoxComponent> MirrorSurface;

    FVector HomeLocation = FVector::ZeroVector;
    FVector DockLocation = FVector::ZeroVector;
    bool bMirrorDocked = false;
};
