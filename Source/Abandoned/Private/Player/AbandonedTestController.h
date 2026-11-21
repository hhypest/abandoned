#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AbandonedTestController.generated.h"

class SWidget;

UCLASS()
class AAbandonedTestController final : public APlayerController
{
    GENERATED_BODY()

protected:
    void BeginPlay() override;
    void SetupInputComponent() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EMenuAction;

    void OpenPauseMenu();
#if !UE_BUILD_SHIPPING
    void OpenTestMenu();
#endif
    void OpenMenu(const FText& Title, FKey ToggleKey, const TSharedRef<SWidget>& Destinations);
    void CloseMenu();
    void RemoveMenu();
    bool CanChangeDisplaySettings() const;
    void ToggleFullscreen();
    UFUNCTION(Exec)
    void AbandonedToggleVSync();
    UFUNCTION(Exec)
    void AbandonedCycleFrameLimit();
    FText GetVSyncLabel() const;
    FText GetFrameLimitLabel() const;
    FText GetFullscreenLabel() const;
    FReply RunMenuAction(EMenuAction Action);

    TSharedPtr<SWidget> Menu;
};
