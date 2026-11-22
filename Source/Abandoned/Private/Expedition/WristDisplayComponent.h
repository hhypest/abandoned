#pragma once

#include "Components/WidgetComponent.h"
#include "WristDisplayComponent.generated.h"

UCLASS()
class UWristDisplayComponent final : public UWidgetComponent
{
    GENERATED_BODY()

public:
    // Allocate the render target during equip, while the player is still lowering the arm.
    void PrepareGlass()
    {
        if (!GetRenderTarget())
        {
            DrawWidgetToRenderTarget(0.0f);
        }
    }
};
