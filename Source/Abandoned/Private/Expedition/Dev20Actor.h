#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Dev20Actor.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS()
class ADev20Actor final : public AActor
{
    GENERATED_BODY()

public:
    ADev20Actor();
    UBoxComponent* GetBody() const { return Body; }
    void RestoreAt(FVector Location);

protected:
    void PostInitializeComponents() override;
    void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UBoxComponent> Body;

    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Parts;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Finish;
};
