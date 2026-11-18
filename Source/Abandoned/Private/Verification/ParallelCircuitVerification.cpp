#include "Verification/ParallelCircuitVerification.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "Puzzles/PuzzleCube.h"
#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogParallelCircuit, Log, All);
#endif

UParallelCircuitVerification::UParallelCircuitVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
bool UParallelCircuitVerification::IsRequested()
{
    return FParse::Param(FCommandLine::Get(), TEXT("VerifyParallelCircuit"));
}

void UParallelCircuitVerification::Attach(ATrainingChamberGameMode& Chamber, const TArray<TObjectPtr<APuzzleCube>>& Cubes,
    const TArray<TObjectPtr<UStaticMeshComponent>>& Traces, UMaterialInterface* Idle, UMaterialInterface* Powered)
{
    auto* Scenario = NewObject<UParallelCircuitVerification>(&Chamber);
    const auto* Layout = Chamber.GetLayout();
    if (!Layout || !Layout->UsesParallelPlates() || Cubes.Num() != 2 || Layout->CubeSpawns.Num() != 2 ||
        Traces.IsEmpty() || Traces.Num() != Layout->RelayTraces.Num() || !IsValid(Idle) || !IsValid(Powered) || Idle == Powered)
    {
        Scenario->Fail(TEXT("invalid relay fixture"));
        return;
    }
    Scenario->Ballast[0] = Cubes[0].Get();
    Scenario->Ballast[1] = Cubes[1].Get();
    for (UStaticMeshComponent* Trace : Traces)
    {
        Scenario->RelayTraces.Add(Trace);
    }
    Scenario->IdleMaterial = Idle;
    Scenario->PoweredMaterial = Powered;
    Chamber.AddInstanceComponent(Scenario);
    Scenario->RegisterComponent();
    Scenario->AddTickPrerequisiteActor(&Chamber);
    Scenario->SetComponentTickEnabled(true);
}

bool UParallelCircuitVerification::SignalsMatch(const ATrainingChamberGameMode& Chamber, bool A, bool B, bool Drive) const
{
    if (Chamber.IsPlatePressed(0) != A || Chamber.IsPlatePressed(1) != B || Chamber.IsExitEnabled() != Drive ||
        !IdleMaterial.IsValid() || !PoweredMaterial.IsValid())
    {
        return false;
    }
    const bool Expected[] = {A, B, Drive};
    const auto& LayoutTraces = Chamber.GetLayout()->RelayTraces;
    for (int32 Index = 0; Index < RelayTraces.Num(); ++Index)
    {
        // The chamber validates every configured signal before spawning its visual.
        const auto Signal = static_cast<uint8>(LayoutTraces[Index].Signal);
        const auto* Material = Expected[Signal] ? PoweredMaterial.Get() : IdleMaterial.Get();
        const auto* Trace = RelayTraces[Index].Get();
        if (!Trace || Trace->GetMaterial(0) != Material)
        {
            return false;
        }
    }
    return true;
}

void UParallelCircuitVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogParallelCircuit, Error, TEXT("Parallel circuit verification failed: %s (phase %d)."), Reason, static_cast<int32>(Phase));
    SetComponentTickEnabled(false);
    Input.Release();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UParallelCircuitVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    Input.Release();
    Super::EndPlay(Reason);
}

void UParallelCircuitVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    Elapsed += DeltaSeconds;
    if (Elapsed > 16.0)
    {
        Fail(TEXT("timed out waiting for player or relay sequence"));
        return;
    }
    PhaseSeconds += DeltaSeconds;
    if (Input.Release() || PhaseSeconds < 1.05)
    {
        return;
    }
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player)
    {
        return;
    }
    const auto* Chamber = Cast<ATrainingChamberGameMode>(GetOwner());
    const auto* Layout = Chamber ? Chamber->GetLayout() : nullptr;
    if (!Layout || !IsValid(Chamber->GetPlate(0)) || !IsValid(Chamber->GetPlate(1)) || !IsValid(Chamber->GetButton()) ||
        !IsValid(Chamber->GetDoor()) || !Ballast[0].IsValid() || !Ballast[1].IsValid())
    {
        Fail(TEXT("missing relay fixture"));
        return;
    }
    PhaseSeconds = 0.0;
    const auto Snap = [&](int32 Index)
    {
        const FSceneBlock& Block = Layout->GetPlateAt(Index);
        APuzzleCube& Cube = *Ballast[Index];
        const FVector Center = Block.Center + FVector(0, 0, Block.Size.Z * 0.5 + Cube.GetHalfSize() + 0.5);
        return Cube.SnapTo(*Chamber->GetPlate(Index), Center, FRotator::ZeroRotator);
    };
    const auto PositionPlayer = [&](const FVector& From, const FRotator& View)
    {
        Player->SetActorLocationAndRotation(From, FRotator(0, View.Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        Controller->SetControlRotation(View);
    };
    const auto Aim = [&](const FVector& From, const FVector& Target)
    {
        PositionPlayer(From, (Target - From - FVector(0, 0, Abandoned::MovementProfile::CameraHeight)).Rotation());
    };
    const bool DoorClosed = Chamber->GetDoor()->GetComponentLocation().Equals(Layout->Door.Center, 2.0);
    switch (Phase)
    {
    case EPhase::Initial:
        if (!SignalsMatch(*Chamber, false, false, false) || !DoorClosed || !Snap(0))
        {
            Fail(TEXT("initial state or ballast placement failed"));
            return;
        }
        Phase = EPhase::UnarmedA;
        break;
    case EPhase::UnarmedA:
        if (!SignalsMatch(*Chamber, true, false, false) || Chamber->IsButtonLatched() || !DoorClosed)
        {
            Fail(TEXT("load on A must wait for the wall switch"));
            return;
        }
        Aim(FVector(Layout->Button.Center.X - 156.0, Layout->Button.Center.Y, Layout->Spawn.Z), Layout->Button.Center);
        Input.Press(*Controller, EKeys::E);
        Phase = EPhase::ArmedA;
        break;
    case EPhase::ArmedA:
        if (!SignalsMatch(*Chamber, true, false, true) || !Chamber->IsButtonLatched() || !Chamber->IsDoorOpen() || !Snap(1))
        {
            Fail(TEXT("A alone must open the armed door"));
            return;
        }
        Phase = EPhase::Both;
        break;
    case EPhase::Both:
        if (!SignalsMatch(*Chamber, true, true, true) || !Chamber->IsDoorOpen())
        {
            Fail(TEXT("both signals must keep the door open"));
            return;
        }
        Ballast[0]->ResetPosition(Layout->CubeSpawns[0].Center);
        Aim(FVector(Layout->Plate.Center.X - 300.0, 0.0, Layout->Spawn.Z), FVector(Layout->Door.Center.X, Layout->Door.Center.Y, 45.0));
        Phase = EPhase::OnlyB;
        break;
    case EPhase::OnlyB:
        if (!SignalsMatch(*Chamber, false, true, true) || !Chamber->IsDoorOpen())
        {
            Fail(TEXT("removing A must leave B in control of the door"));
            return;
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("CaptureParallelCircuit")))
        {
            FScreenshotRequest::RequestScreenshot(TEXT("Entry-Parallel-Circuit-B"), false, false);
        }
        Phase = EPhase::RemoveB;
        break;
    case EPhase::RemoveB:
        Ballast[1]->ResetPosition(Layout->CubeSpawns[1].Center);
        Phase = EPhase::Unloaded;
        break;
    case EPhase::Unloaded:
        if (!SignalsMatch(*Chamber, false, false, false) || !DoorClosed || !Snap(1))
        {
            Fail(TEXT("the last release must shut the door"));
            return;
        }
        PositionPlayer(FVector(Layout->Plate.Center.X, Layout->Plate.Center.Y,
            Layout->GetPlateTop() + Abandoned::MovementProfile::StandingHalfHeight + 1.0), FRotator::ZeroRotator);
        Phase = EPhase::Overload;
        break;
    case EPhase::Overload:
        if (!FMath::IsNearlyEqual(Chamber->GetMeasuredMass(0), Layout->PlayerMassKg, 0.05f))
        {
            Fail(TEXT("plate A did not measure the player's weight"));
            return;
        }
        if (Chamber->IsPlatePressed(0))
        {
            Fail(TEXT("overloaded plate A still sends a signal"));
            return;
        }
        if (!SignalsMatch(*Chamber, false, true, true) || !Chamber->IsDoorOpen())
        {
            Fail(TEXT("overload on A must not cut the valid B branch"));
            return;
        }
        Input.Press(*Controller, EKeys::R);
        Phase = EPhase::AfterReset;
        break;
    case EPhase::AfterReset:
        if (!SignalsMatch(*Chamber, false, false, false) || Chamber->IsButtonLatched() || !DoorClosed ||
            !Player->GetActorLocation().Equals(Layout->Spawn, 3.0) ||
            !FMath::IsNearlyZero(Chamber->GetMeasuredMass(0)) || !FMath::IsNearlyZero(Chamber->GetMeasuredMass(1)))
        {
            Fail(TEXT("reset must release both plates and disarm the door"));
            return;
        }
        UE_LOG(LogParallelCircuit, Display,
            TEXT("Parallel circuit verification passed: A, A+B, B, last release, overload, power restored, reset."));
        SetComponentTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}
#endif
