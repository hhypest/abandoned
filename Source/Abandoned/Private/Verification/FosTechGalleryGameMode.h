#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FosTechGalleryGameMode.generated.h"

// Asset comparison fixture. Its setup and command-line entry point exist only in development builds.
UCLASS(Transient, NotBlueprintable)
class AFosTechGalleryGameMode final : public AGameModeBase
{
    GENERATED_BODY()

public:
    AFosTechGalleryGameMode();

#if WITH_DEV_AUTOMATION_TESTS
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    TArray<TWeakObjectPtr<class UStaticMeshComponent>> Exhibits;
    TWeakObjectPtr<class ACameraActor> GalleryCamera;
    int32 GalleryFrame = 0;
#endif
};
