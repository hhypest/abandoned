#include "FXSystem.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CoreMisc.h"
#include "Modules/ModuleManager.h"

class FAbandonedGameModule final : public FDefaultGameModuleImpl
{
public:
    void StartupModule() override
    {
        PreExit = FCoreDelegates::OnPreExit.AddStatic(&Abandoned::Infrastructure::Expedition::UnrealCheckpointStore::WaitForPublication);
        if (IsRunningGame())
        {
            // Niagara registers this factory in PreDefault. The campaign has no Niagara GPU simulations.
            // In UE 5.7 its per-world dispatcher retains the rendered view's distance-field resources.
            FFXSystemInterface::UnregisterCustomFXSystem(TEXT("FNiagaraGpuComputeDispatch"));
        }
    }

    void ShutdownModule() override
    {
        FCoreDelegates::OnPreExit.Remove(PreExit);
        Abandoned::Infrastructure::Expedition::UnrealCheckpointStore::WaitForPublication();
    }

private:
    FDelegateHandle PreExit;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAbandonedGameModule, Abandoned, "Abandoned");
