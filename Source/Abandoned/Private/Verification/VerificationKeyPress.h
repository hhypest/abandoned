#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

// Keep a press across one player-input tick. Release also handles world teardown.
struct FVerificationKeyPress
{
    void Press(APlayerController& Controller, const FKey& Key)
    {
        Owner = &Controller;
        Held = Key;
        Controller.InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, IE_Pressed, FPlatformTime::Cycles64()));
    }

    bool Release()
    {
        if (!Held.IsValid())
        {
            return false;
        }
        if (APlayerController* Controller = Owner.Get())
        {
            Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Held, IE_Released, FPlatformTime::Cycles64()));
        }
        Owner.Reset();
        Held = FKey{};
        return true;
    }

private:
    TWeakObjectPtr<APlayerController> Owner;
    FKey Held;
};
#endif
