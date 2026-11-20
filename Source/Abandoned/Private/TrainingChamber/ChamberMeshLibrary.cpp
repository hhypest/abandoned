#include "TrainingChamber/ChamberMeshLibrary.h"

#include "Engine/StaticMesh.h"
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
    Meshes.Reset();
    Super::Deinitialize();
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
        Meshes.Reset();
        LastChamber.Reset();
    }
}
