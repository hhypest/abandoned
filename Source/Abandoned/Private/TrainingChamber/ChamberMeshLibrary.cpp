#include "TrainingChamber/ChamberMeshLibrary.h"

#include "Engine/StaticMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/UObjectGlobals.h"

void UChamberMeshLibrary::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UChamberMeshLibrary::OnMapLoaded);
}

void UChamberMeshLibrary::Deinitialize()
{
    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
    ReleaseWeightShellRequest();
    Meshes.Reset();
    Super::Deinitialize();
}

void UChamberMeshLibrary::PrepareWeightShell()
{
    CSV_EVENT_GLOBAL(TEXT("WeightShellPrepare"));
    if (!FParse::Param(FCommandLine::Get(), TEXT("PreloadWeightShell")))
    {
        return;
    }
    const FSoftObjectPath Path(WeightShellPath);
    if (WeightShellRequest || Path.ResolveObject())
    {
        return;
    }
    CSV_EVENT_GLOBAL(TEXT("WeightShellRequest"));
    WeightShellRequest = UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,
        FStreamableDelegate::CreateWeakLambda(this, [this]()
        {
            if (WeightShellRequest && WeightShellRequest->GetLoadedAsset<UStaticMesh>())
            {
                CSV_EVENT_GLOBAL(TEXT("WeightShellReady"));
            }
        }));
}

void UChamberMeshLibrary::ReleaseWeightShellRequest()
{
    if (WeightShellRequest)
    {
        WeightShellRequest->CancelHandle();
        WeightShellRequest.Reset();
    }
}

UStaticMesh* UChamberMeshLibrary::LoadForChamber(UWorld& World, const TCHAR* Path)
{
    TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Path);
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
#if !UE_BUILD_SHIPPING
    static const bool bDisableReuse = FParse::Param(FCommandLine::Get(), TEXT("NoChamberMeshReuse"));
    if (bDisableReuse)
    {
        return Mesh;
    }
#endif
    if (Mesh)
    {
        LastChamber = &World;
        Meshes.AddUnique(Mesh);
    }
    return Mesh;
}

void UChamberMeshLibrary::OnMapLoaded(UWorld* World)
{
    if (!World || (World->GetGameInstance() == GetGameInstance() && World != LastChamber.Get()))
    {
        ReleaseWeightShellRequest();
        Meshes.Reset();
        LastChamber.Reset();
    }
}
