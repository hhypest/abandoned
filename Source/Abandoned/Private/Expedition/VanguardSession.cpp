#include "Expedition/VanguardSession.h"

#include "Engine/World.h"

UVanguardSession* UVanguardSession::Find(const UObject& Context)
{
    const UWorld* World = Context.GetWorld();
    return World ? UGameInstance::GetSubsystem<UVanguardSession>(World->GetGameInstance()) : nullptr;
}
