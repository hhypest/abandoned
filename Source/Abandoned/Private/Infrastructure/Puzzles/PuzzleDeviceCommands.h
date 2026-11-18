#pragma once

#include "UObject/Interface.h"
#include "PuzzleDeviceCommands.generated.h"

class APuzzleCharacter;
class UBoxComponent;
class UPrimitiveComponent;

UINTERFACE()
class UPuzzleDeviceCommands : public UInterface
{
    GENERATED_BODY()
};

class IPuzzleDeviceCommands
{
    GENERATED_BODY()

public:
    virtual bool UseDevice(APuzzleCharacter& Player, UPrimitiveComponent& Target) = 0;
    virtual void ResetPuzzle(APuzzleCharacter& Player) = 0;
    virtual void PlaceDevice(APuzzleCharacter&, UBoxComponent&) {}
    virtual const TCHAR* DevicePrompt(const UPrimitiveComponent&) const { return nullptr; }
};
