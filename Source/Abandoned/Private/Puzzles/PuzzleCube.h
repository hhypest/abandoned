#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PuzzleCube.generated.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EPuzzleCubeType : uint8
{
    Small,
    Large,
};

UCLASS()
class APuzzleCube final : public AActor
{
    GENERATED_BODY()

public:
    APuzzleCube();
    static bool IsValidType(EPuzzleCubeType Type);
    void Initialize(EPuzzleCubeType Type, UStaticMesh& Mesh);
    UBoxComponent* GetBody() const { return Body; }
    double GetHalfSize() const;
    float GetMassKg() const { return MassKg; }
    float GetStackMass() const;
    UBoxComponent* GetSupport() const { return Support.Get(); }
    bool CanSupportCube() const;
    bool SnapTo(UBoxComponent& NewSupport, const FVector& Center, const FRotator& Rotation);
    void DetachForPickup();
    void ResetPosition(const FVector& Center);

protected:
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    void ReleaseSupport();
    void ReleaseStack();
    void SetFreeBody();

    UFUNCTION()
    void OnSupportEndPlay(AActor* Actor, EEndPlayReason::Type Reason);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> Body;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Visual;

    TWeakObjectPtr<UBoxComponent> Support;
    TWeakObjectPtr<APuzzleCube> StackedCube;
    float MassKg = 0.0f;
};
