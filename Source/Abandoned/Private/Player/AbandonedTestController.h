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
    void SetupInputComponent() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    enum class EMenuAction { Resume, TrainingChamber, MovementLab, Fullscreen };

    void ToggleMenu();
    void CloseMenu();
    void RemoveMenu();
    bool CanToggleFullscreen() const;
    void ToggleFullscreen();
    FText GetFullscreenLabel() const;
    FReply RunMenuAction(EMenuAction Action);

    TSharedPtr<SWidget> Menu;
};
