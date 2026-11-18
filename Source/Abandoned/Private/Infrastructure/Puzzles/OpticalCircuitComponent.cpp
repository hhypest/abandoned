#include "Infrastructure/Puzzles/OpticalCircuitComponent.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Domain/Puzzles/OpticalReflector.h"
#include "Engine/World.h"
#include "Infrastructure/Puzzles/OpticalReflectorComponent.h"
#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"
#include "World/Blockout.h"

bool UOpticalCircuitComponent::Build(const FOpticalLane& Direct, const FOpticalLane& Reflected,
    UStaticMesh& Mesh, UStaticMeshComponent& SupplyLamp,
    UMaterialInterface& BeamLight, UMaterialInterface& PowerLight, UMaterialInterface& Off)
{
    Interruption = Direct;
    Reflection = Reflected;
    ButtonLamp = &SupplyLamp;
    Cyan = &BeamLight;
    Amber = &PowerLight;
    Inactive = &Off;
    Interruption.Direction = (Direct.Receiver->GetComponentLocation() - Direct.Emitter->GetComponentLocation()).GetSafeNormal();
    // One direct segment, plus the incident and reflected segments of the second lane.
    constexpr int32 BeamCapacity = 3;
    for (int32 Index = 0; Index < BeamCapacity; ++Index)
    {
        auto Segment = Abandoned::Blockout::SpawnBox(*GetWorld(), Mesh, FVector::ZeroVector, FVector(1),
            EComponentMobility::Movable, TEXT("NoCollision"));
        if (!Segment.Visual)
        {
            return false;
        }
        Segment.Visual->SetMaterial(0, Cyan);
        Segment.Visual->SetCastShadow(false);
        Beams.Add(Segment.Visual);
    }
    Reset();
    return true;
}

void UOpticalCircuitComponent::Reset()
{
    Course.Reset();
    Reflection.Direction = FVector::ForwardVector;
    bDirectSignal = bReflectedSignal = false;
    ButtonLamp->SetMaterial(0, Inactive);
    for (const FOpticalLane* Lane : { &Interruption, &Reflection })
    {
        Lane->EmitterLamp->SetMaterial(0, Inactive);
        Lane->ReceiverLamp->SetMaterial(0, Inactive);
        Lane->PowerLine->SetMaterial(0, Inactive);
    }
    for (UStaticMeshComponent* Beam : Beams)
    {
        Beam->SetVisibility(false);
    }
}

void UOpticalCircuitComponent::AimEmitter(FVector Point)
{
    const FVector Direction = (Point - Reflection.Emitter->GetComponentLocation()).GetSafeNormal();
    if (!Direction.IsNearlyZero())
    {
        Reflection.Direction = Direction;
    }
}

void UOpticalCircuitComponent::AimReflector(UOpticalReflectorComponent& Reflector, FVector Point) const
{
    if (!IsValid(Reflector.GetSurface()))
    {
        return;
    }
    FVector Pivot = Reflector.GetSurface()->GetComponentLocation();
    const FVector Start = Reflection.Emitter->GetComponentLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(AimMirror), false, Reflection.Emitter->GetOwner());
    FHitResult Incoming;
    GetWorld()->LineTraceSingleByChannel(Incoming, Start, Start + Reflection.Direction * BeamReachCm, BeamChannel, Query);
    if (Incoming.GetComponent() == Reflector.GetSurface())
    {
        Pivot = Incoming.ImpactPoint;
    }
    Reflector.Align(Start, Pivot, Point);
}

void UOpticalCircuitComponent::DrawBeam(FVector Start, FVector End, int32& Index)
{
    const FVector Span = End - Start;
    UStaticMeshComponent& Segment = *Beams[Index++];
    Segment.SetWorldLocationAndRotation((Start + End) * 0.5, Span.Rotation());
    Segment.SetWorldScale3D(FVector(Span.Length() / 100.0, 0.022, 0.022));
    Segment.SetVisibility(true);
}

bool UOpticalCircuitComponent::TraceLane(const FOpticalLane& Lane, bool bPowered, const UOpticalReflectorComponent* Reflector, int32& BeamIndex)
{
    if (!bPowered)
    {
        return false;
    }
    FVector Start = Lane.Emitter->GetComponentLocation();
    FVector Direction = Lane.Direction;
    double Range = BeamReachCm;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(OpticalLane), false, Lane.Emitter->GetOwner());
    for (int32 Segment = 0; Segment < 2; ++Segment)
    {
        const FVector End = Start + Direction * Range;
        FHitResult Hit;
        const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, BeamChannel, Query);
        DrawBeam(Start, bHit ? Hit.ImpactPoint : End, BeamIndex);
        if (Hit.GetComponent() == Lane.Receiver)
        {
            return !Reflector || Segment > 0;
        }
        if (!bHit || Segment > 0 || !Reflector || !IsValid(Reflector->GetSurface()) ||
            Hit.GetComponent() != Reflector->GetSurface() || Abandoned::Infrastructure::Puzzles::IsDoorClamped(Reflector->GetBody()))
        {
            return false;
        }
        const FVector Normal = Reflector->GetSurface()->GetForwardVector();
        if (!Abandoned::Domain::Puzzles::CanReflectFromFace(
            FVector::DotProduct(Hit.ImpactNormal, Normal), FVector::DotProduct(Direction, Normal)))
        {
            return false;
        }
        Direction = Direction.MirrorByVector(Normal).GetSafeNormal();
        Query.AddIgnoredActor(Reflector->GetBody()->GetOwner());
        Range -= Hit.Distance + 2.0;
        if (Range <= 0.0)
        {
            return false;
        }
        Start = Hit.ImpactPoint + Direction * 2.0;
    }
    return false;
}

Abandoned::Application::Puzzles::OpticsDoors UOpticalCircuitComponent::Update(float Delta, const UOpticalReflectorComponent& Reflector)
{
    const auto Power = Course.GetPower();
    int32 BeamIndex = 0;
    bDirectSignal = TraceLane(Interruption, Power.DirectEmitter, nullptr, BeamIndex);
    bReflectedSignal = TraceLane(Reflection, Power.ReflectionEmitter, &Reflector, BeamIndex);
    for (; BeamIndex < Beams.Num(); ++BeamIndex)
    {
        Beams[BeamIndex]->SetVisibility(false);
    }
    const auto Doors = Course.ObserveReceivers(bDirectSignal, bReflectedSignal, Delta);
    ButtonLamp->SetMaterial(0, Power.DirectEmitter ? Amber : Inactive);
    Interruption.EmitterLamp->SetMaterial(0, Power.DirectEmitter ? Cyan : Inactive);
    Reflection.EmitterLamp->SetMaterial(0, Power.ReflectionEmitter ? Cyan : Inactive);
    Interruption.ReceiverLamp->SetMaterial(0, bDirectSignal ? Cyan : Inactive);
    Reflection.ReceiverLamp->SetMaterial(0, bReflectedSignal ? Cyan : Inactive);
    Interruption.PowerLine->SetMaterial(0, Doors.Interruption ? Amber : Inactive);
    Reflection.PowerLine->SetMaterial(0, Doors.Reflection ? Amber : Inactive);
    return Doors;
}
