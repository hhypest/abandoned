#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "World/Blockout.h"

bool ATrainingChamberGameMode::SpawnRelayTraces(UStaticMesh& Mesh, UMaterialInterface* Idle, UMaterialInterface* Powered)
{
    if (!Idle || !Powered)
    {
        return false;
    }
    RelayIdleMaterial = Idle;
    RelayPoweredMaterial = Powered;
    RelayTraces.Reserve(Layout->RelayTraces.Num());
    for (const FTrainingRelayTrace& Trace : Layout->RelayTraces)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(*GetWorld(), Mesh, Trace.Block.Center, Trace.Block.Size,
            EComponentMobility::Movable, TEXT("NoCollision"));
        if (!IsValid(Parts.Visual))
        {
            return false;
        }
        RelayTraces.Add(Parts.Visual);
        Parts.Visual->SetMaterial(0, Idle);
    }
    return true;
}

void ATrainingChamberGameMode::UpdateRelayTraces()
{
    const auto& State = Circuit.GetState();
    const bool Powered[] = {State.PlatePressed, State.AlternatePlatePressed, State.ExitEnabled};
    for (int32 Index = 0; Index < RelayTraces.Num(); ++Index)
    {
        UStaticMeshComponent* Trace = RelayTraces[Index];
        if (IsValid(Trace))
        {
            const auto Signal = static_cast<uint8>(Layout->RelayTraces[Index].Signal);
            UMaterialInterface* Material = Powered[Signal] ? RelayPoweredMaterial.Get() : RelayIdleMaterial.Get();
            if (Trace->GetMaterial(0) != Material)
            {
                Trace->SetMaterial(0, Material);
            }
        }
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogParallelCircuit, Log, All);

void ATrainingChamberGameMode::VerifyParallelCircuit(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    const auto Fail = [this](const TCHAR* Message)
    {
        UE_LOG(LogParallelCircuit, Error, TEXT("Parallel circuit verification failed at step %d: %s."), RelayTestStep, Message);
        bVerifyParallelCircuit = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    if (!Layout->UsesParallelPlates() || !IsValid(GetPlate(1)) || !IsValid(Button) ||
        Cubes.Num() != 2 || !IsValid(Cubes[0]) || !IsValid(Cubes[1]))
    {
        Fail(TEXT("invalid relay fixture"));
        return;
    }
    const auto Snap = [this](int32 Index)
    {
        APuzzleCube& Cube = *Cubes[Index];
        const FSceneBlock& Block = Layout->GetPlateAt(Index);
        const FVector Center = Block.Center + FVector(0, 0, Block.Size.Z * 0.5 + Cube.GetHalfSize() + 0.5);
        return Cube.SnapTo(*GetPlate(Index), Center, FRotator::ZeroRotator);
    };
    const auto Aim = [&Player](const FVector& From, const FVector& Target)
    {
        const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Target - Eye).Rotation(), From));
    };
    const auto Check = [this](bool A, bool B, bool Drive)
    {
        if (A != IsPlatePressed(0) || B != IsPlatePressed(1) || Drive != IsExitEnabled())
        {
            return false;
        }
        const bool Expected[] = {A, B, Drive};
        for (int32 Index = 0; Index < RelayTraces.Num(); ++Index)
        {
            const auto Signal = static_cast<uint8>(Layout->RelayTraces[Index].Signal);
            const UMaterialInterface* Finish = Expected[Signal] ? RelayPoweredMaterial.Get() : RelayIdleMaterial.Get();
            if (!IsValid(RelayTraces[Index]) || RelayTraces[Index]->GetMaterial(0) != Finish)
            {
                return false;
            }
        }
        return true;
    };
    RelayTestSeconds += DeltaSeconds;
    if (RelayTestSeconds < 1.05f)
    {
        return;
    }
    RelayTestSeconds = 0.0f;
    switch (RelayTestStep++)
    {
    case 0:
        if (!Check(false, false, false) || !Snap(0))
        {
            Fail(TEXT("initial state or ballast placement failed"));
        }
        break;
    case 1:
        if (!Check(true, false, false) || IsButtonLatched() || IsDoorOpen())
        {
            Fail(TEXT("load on A must wait for the wall switch"));
            break;
        }
        Aim(FVector(Layout->Button.Center.X - 156.0, Layout->Button.Center.Y, Layout->Spawn.Z), Layout->Button.Center);
        Player.Interact();
        break;
    case 2:
        if (!Check(true, false, true) || !IsDoorOpen() || !Snap(1))
        {
            Fail(TEXT("A alone must open the armed door"));
        }
        break;
    case 3:
        if (!Check(true, true, true) || !IsDoorOpen())
        {
            Fail(TEXT("both signals must keep the door open"));
            break;
        }
        ReturnCube(Player, 0);
        Aim(FVector(Layout->Plate.Center.X - 300.0, 0.0, Layout->Spawn.Z),
            FVector(Layout->Door.Center.X, Layout->Door.Center.Y, 45.0));
        break;
    case 4:
        if (!Check(false, true, true) || !IsDoorOpen())
        {
            Fail(TEXT("removing A must leave B in control of the door"));
            break;
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("CaptureParallelCircuit")))
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Entry-Parallel-Circuit-B"), false, false);
        }
        break;
    case 5:
        ReturnCube(Player, 1);
        break;
    case 6:
        if (!Check(false, false, false) || !IsValid(Door) ||
            !Door->GetComponentLocation().Equals(Layout->Door.Center, 2.0) || !Snap(1))
        {
            Fail(TEXT("the last release must shut the door"));
            break;
        }
        Player.PlaceAt(FTransform(FRotator::ZeroRotator, FVector(Layout->Plate.Center.X, Layout->Plate.Center.Y,
            Layout->GetPlateTop() + Abandoned::MovementProfile::StandingHalfHeight + 1.0)));
        break;
    case 7:
        if (!Check(false, true, true) || !IsDoorOpen() ||
            !FMath::IsNearlyEqual(GetMeasuredMass(0), Layout->PlayerMassKg, 0.05f))
        {
            Fail(TEXT("overload on A must not cut the valid B branch"));
            break;
        }
        ResetChamber(Player);
        break;
    case 8:
        if (!Check(false, false, false) || IsButtonLatched() || IsDoorOpen() ||
            !Player.GetActorLocation().Equals(Layout->Spawn, 3.0) ||
            !FMath::IsNearlyZero(GetMeasuredMass(0)) || !FMath::IsNearlyZero(GetMeasuredMass(1)))
        {
            Fail(TEXT("reset must release both plates and disarm the door"));
            break;
        }
        UE_LOG(LogParallelCircuit, Display,
            TEXT("Parallel circuit verification passed: A, A+B, B, last release, overload, power restored, reset."));
        bVerifyParallelCircuit = false;
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    default:
        break;
    }
}
#endif
