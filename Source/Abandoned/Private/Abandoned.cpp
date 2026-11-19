#include "FXSystem.h"
#include "Misc/CoreMisc.h"
#include "Modules/ModuleManager.h"

class FAbandonedGameModule final : public FDefaultGameModuleImpl
{
public:
    void StartupModule() override
    {
        if (IsRunningGame())
        {
            // Niagara registers this factory in PreDefault. The campaign has no Niagara GPU simulations.
            // In UE 5.7 its per-world dispatcher retains the rendered view's distance-field resources.
            FFXSystemInterface::UnregisterCustomFXSystem(TEXT("FNiagaraGpuComputeDispatch"));
        }
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAbandonedGameModule, Abandoned, "Abandoned");
