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
    enum class EMenuAction { Resume, TrainingChamber, Balance, MovementLab, PowerLab, GravityLab, PortalLab, Dev20, Radiation, Fullscreen, VSync, FrameRate, Quit };

    void ToggleMenu();
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
