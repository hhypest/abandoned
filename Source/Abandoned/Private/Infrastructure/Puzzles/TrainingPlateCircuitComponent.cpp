#include "Infrastructure/Puzzles/TrainingPlateCircuitComponent.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Domain/Expedition/Dev20.h"
#include "Expedition/Dev20Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Infrastructure/Puzzles/CubePlacementComponent.h"
#include "Player/PuzzleCharacter.h"
#include "Puzzles/PuzzleCube.h"
#include "TrainingChamber/TrainingChamberLayout.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrainingPlates, Log, All);

namespace
{
using Abandoned::Domain::Puzzles::CircuitRules;
using Abandoned::Domain::Expedition::Dev20MassKg;

const UPrimitiveComponent* FindPlayerSupport(const APuzzleCharacter& Player)
{
    const UPrimitiveComponent* PlayerSupport = Player.GetCharacterMovement()->IsMovingOnGround()
        ? Player.GetMovementBase() : nullptr;
    while (PlayerSupport)
    {
        const APuzzleCube* Cube = Cast<APuzzleCube>(PlayerSupport->GetOwner());
        if (!Cube || !Cube->GetSupport())
        {
            break;
        }
        PlayerSupport = Cube->GetSupport();
    }
    return PlayerSupport;
}

float MeasurePlate(const UBoxComponent& Plate, const FSceneBlock& Block, const UPrimitiveComponent* PlayerSupport,
    float PlayerLoad, const UBoxComponent* Held, const TArray<TObjectPtr<APuzzleCube>>& Cubes, const ADev20Actor* Dev20)
{
    const FVector AllowedOffset = Block.Size * 0.5 - FVector(10.0);
    const double PlateTop = Block.Center.Z + Block.Size.Z * 0.5;
    const auto OnPlate = [&](UBoxComponent& Body)
    {
        const FBoxSphereBounds& Bounds = Body.Bounds;
        const FVector Offset = Bounds.Origin - Block.Center;
        const double BottomZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
        return FMath::Abs(Offset.X) < AllowedOffset.X && FMath::Abs(Offset.Y) < AllowedOffset.Y &&
            FMath::IsNearlyEqual(BottomZ, PlateTop, 4.0) &&
            FMath::Abs(Body.GetPhysicsLinearVelocity().Z) < 30.0;
    };
    float MassKg = PlayerSupport == &Plate ? PlayerLoad : 0.0f;
    for (const APuzzleCube* Cube : Cubes)
    {
        if (!IsValid(Cube) || Cube->GetBody() == Held)
        {
            continue;
        }
        if (Cube->GetSupport() == &Plate)
        {
            MassKg += Cube->GetStackMass();
            continue;
        }
        UBoxComponent* Body = Cube->GetBody();
        if (!Cube->GetSupport() && IsValid(Body) && OnPlate(*Body))
        {
            MassKg += Cube->GetStackMass();
            if (PlayerSupport == Body)
            {
                MassKg += PlayerLoad;
            }
        }
    }
    if (IsValid(Dev20) && IsValid(Dev20->GetBody()) && Dev20->GetBody() != Held && OnPlate(*Dev20->GetBody()))
    {
        MassKg += Dev20MassKg + (PlayerSupport == Dev20->GetBody() ? PlayerLoad : 0.0f);
    }
    return MassKg;
}

bool HasStackAtExit(const TArray<TObjectPtr<APuzzleCube>>& Cubes, const FBox& StackBounds)
{
    return Cubes.ContainsByPredicate([&StackBounds](const APuzzleCube* Cube)
    {
        return IsValid(Cube) && Cube->GetStackMass() > Cube->GetMassKg() &&
            StackBounds.IsInside(Cube->GetActorLocation());
    });
}
}

void UTrainingPlateCircuitComponent::Configure(const UTrainingChamberLayout& Settings, bool bCountCarriedMass)
{
    Layout = &Settings;
    bIncludeHeldMass = bCountCarriedMass;
}

void UTrainingPlateCircuitComponent::AddPlate(UBoxComponent& Body, UStaticMeshComponent& Visual, UPointLightComponent* Indicator)
{
    FTrainingPressurePlate& Plate = Plates.AddDefaulted_GetRef();
    Plate.Body = &Body;
    Plate.Visual = &Visual;
    Plate.RestLocation = Visual.GetRelativeLocation();
    Plate.Indicator = Indicator;
    UCubePlacementComponent::MarkPressurePlate(Body);
    PlateLoads.Add(0.0f);
}

bool UTrainingPlateCircuitComponent::IsExitEnabled() const
{
    return Layout && Layout->UsesPlateBalance() ? Balance.IsBalanced() : Circuit.GetState().ExitEnabled;
}

void UTrainingPlateCircuitComponent::Observe(const APuzzleCharacter& Player, const TArray<TObjectPtr<APuzzleCube>>& Cubes,
    const ADev20Actor* Dev20, float DeltaSeconds)
{
    if (!Layout || Plates.Num() != Layout->GetPlateCount())
    {
        return;
    }
    const UBoxComponent* Held = Player.GetHeldBody();
    const float PlayerLoad = Layout->PlayerMassKg + (bIncludeHeldMass && IsValid(Held) ? Held->GetMass() : 0.0f);
    const UPrimitiveComponent* PlayerSupport = FindPlayerSupport(Player);
    for (int32 Index = 0; Index < Plates.Num(); ++Index)
    {
        const UBoxComponent* Plate = GetPlate(Index);
        PlateLoads[Index] = IsValid(Plate)
            ? MeasurePlate(*Plate, Layout->GetPlateAt(Index), PlayerSupport, PlayerLoad, Held, Cubes, Dev20)
            : 0.0f;
    }
    if (Layout->UsesPlateBalance())
    {
        Balance.Observe({Layout->BalanceTotalKg, 0.01f}, {PlateLoads.GetData(), static_cast<size_t>(PlateLoads.Num())}, DeltaSeconds);
        for (int32 Index = 0; Index < Plates.Num(); ++Index)
        {
            SetPlatePressed(Index, PlateLoads[Index] > 0.01f);
        }
        return;
    }
    const auto DoorGate = Layout->UsesParallelPlates() ? CircuitRules::Gate::ButtonAndParallelPlates :
        Layout->bRequiresStack ? CircuitRules::Gate::StackLatch :
        (Layout->bRequiresButton ? CircuitRules::Gate::ButtonAndPlate : CircuitRules::Gate::Plate);
    Circuit.Observe({Layout->PlateActivationMassKg, Layout->PlateMaximumMassKg, DoorGate}, GetMeasuredMass(0),
        Layout->bRequiresStack && HasStackAtExit(Cubes, Layout->GetStackBounds()), DeltaSeconds, GetMeasuredMass(1));
    SetPlatePressed(0, Circuit.GetState().PlatePressed);
    if (Layout->UsesParallelPlates())
    {
        SetPlatePressed(1, Circuit.GetState().AlternatePlatePressed);
    }
}

void UTrainingPlateCircuitComponent::SetPlatePressed(int32 Index, bool bPressed)
{
    FTrainingPressurePlate& Plate = Plates[Index];
    if (Plate.bPressed == bPressed)
    {
        return;
    }
    Plate.bPressed = bPressed;
    if (IsValid(Plate.Visual))
    {
        Plate.Visual->SetRelativeLocation(Plate.RestLocation + FVector(0.0, 0.0, bPressed ? -4.0 : 0.0));
    }
    if (IsValid(Plate.Indicator))
    {
        Plate.Indicator->SetIntensity(bPressed ? Layout->PressedLightIntensity : Layout->PlateLight.Intensity);
    }
    UE_LOG(LogTrainingPlates, Display, TEXT("Pressure plate %d %s."), Index + 1, bPressed ? TEXT("pressed") : TEXT("released"));
}

void UTrainingPlateCircuitComponent::Reset()
{
    Circuit.Reset();
    Balance.Reset();
    for (int32 Index = 0; Index < Plates.Num(); ++Index)
    {
        PlateLoads[Index] = 0.0f;
        SetPlatePressed(Index, false);
    }
}
