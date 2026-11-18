#include "Infrastructure/Puzzles/PowerLabDoorsComponent.h"
#include "Puzzles/PowerLabLayout.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "Materials/MaterialInterface.h"
#include "Player/PuzzleCharacter.h"
#include "World/Blockout.h"

namespace
{
constexpr double DoorTravelCm = 270.0;
constexpr double DoorSpeedCm = 320.0;

UTextRenderComponent* AddPulseLabel(UBoxComponent& Body, FVector Offset, FRotator Rotation, float Size, const TCHAR* Text)
{
    AActor* Owner = Body.GetOwner();
    auto* Label = NewObject<UTextRenderComponent>(Owner);
    Owner->AddInstanceComponent(Label);
    Label->SetupAttachment(&Body);
    Label->SetRelativeLocation(Offset);
    Label->SetRelativeRotation(Rotation);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(Size);
    Label->SetText(FText::FromString(Text));
    Label->RegisterComponent();
    return Label;
}
}

bool UPowerLabDoorsComponent::Build(const UPowerLabLayout& Data, UBoxComponent& DirectDoor, UBoxComponent& ReflectedDoor,
    UStaticMesh& Mesh, UMaterialInterface& Steel, UMaterialInterface& Panel, UMaterialInterface& Screen,
    UMaterialInterface& PowerLight, UMaterialInterface& Off)
{
    Layout = &Data;
    Amber = &PowerLight;
    Inactive = &Off;
    Interruption.Bind(DirectDoor);
    Reflection.Bind(ReflectedDoor);
    const auto Box = [&](FVector Center, FVector Size, bool bSolid, UMaterialInterface& Material)
    {
        const auto Parts = Abandoned::Blockout::SpawnBox(*GetWorld(), Mesh, Center, Size,
            EComponentMobility::Movable, bSolid ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
        if (Parts.Visual)
        {
            Parts.Visual->SetMaterial(0, &Material);
        }
        return Parts;
    };
    const auto BuildSwitch = [&](const FVector& Point, const TCHAR* Text) -> UBoxComponent*
    {
        const auto Parts = Box(Point, FVector(54, 22, 42), true, Panel);
        const auto Lamp = Box(Point + FVector(0, 12, 11), FVector(38, 4, 7), false, *Inactive);
        if (!Parts.Collision || !Parts.Visual || !Lamp.Visual)
        {
            return nullptr;
        }
        PulseIndicators.Add(Lamp.Visual);
        AddPulseLabel(*Parts.Collision, FVector(0, 13.5, -5), FRotator(0, 90, 0), 10.0f, Text);
        return Parts.Collision;
    };
    PulseButton = BuildSwitch(Layout->PulseButton, TEXT("PULSE"));
    PulseReturn = BuildSwitch(Layout->PulseReturn, TEXT("RETURN"));
    const FSceneBlock& Closed = Layout->PulseDoor;
    const auto Door = Box(Closed.Center, Closed.Size, true, Steel);
    const auto Line = Box(FVector(Closed.Center.X - 200.0, Closed.Center.Y, 3.0),
        FVector(400, 10, 6), false, *Inactive);
    const auto Display = Box(Closed.Center + FVector(-39, 0, 178), FVector(12, 230, 62), false, Screen);
    if (!PulseButton || !PulseReturn || !Door.Collision || !Door.Visual || !Line.Visual || !Display.Collision || !Display.Visual)
    {
        return false;
    }
    TimedDoor.Bind(*Door.Collision);
    PulseIndicators.Add(Line.Visual);
    PulseClock = AddPulseLabel(*Display.Collision, FVector(-7, 0, 0), FRotator(0, 180, 0), 30.0f, TEXT("READY"));
    Pulse = Abandoned::Application::Puzzles::TimedPassage(Layout->PulseSeconds);
    UpdatePulseDisplay();
    return true;
}

bool UPowerLabDoorsComponent::IsInPulseBay(const FVector& Player) const
{
    return Layout && TimedDoor.Body.IsValid() && Player.X > Layout->Reflection.Door.X + 90.0;
}

float UPowerLabDoorsComponent::GetPulseSeconds() const
{
    return Pulse.RemainingSeconds();
}

float UPowerLabDoorsComponent::GetPulseFraction() const
{
    return Layout && Layout->PulseSeconds > 0.0f ? FMath::Clamp(GetPulseSeconds() / Layout->PulseSeconds, 0.0f, 1.0f) : 0.0f;
}

void UPowerLabDoorsComponent::UpdatePulseDisplay()
{
    const int32 Tenths = FMath::CeilToInt(GetPulseSeconds() * 10.0f);
    if (DisplayedPulseTenths == Tenths)
    {
        return;
    }
    const bool bPowered = Pulse.IsPowered();
    for (UStaticMeshComponent* Indicator : PulseIndicators)
    {
        Indicator->SetMaterial(0, bPowered ? Amber.Get() : Inactive.Get());
    }
    PulseClock->SetText(FText::FromString(bPowered ? FString::Printf(TEXT("%.1f s"), Tenths / 10.0f) : TEXT("READY")));
    PulseClock->SetTextRenderColor(bPowered ? FColor(255, 200, 105) : FColor::White);
    DisplayedPulseTenths = Tenths;
}

const TCHAR* UPowerLabDoorsComponent::GetPulseObjective(const FVector& Player) const
{
    if (TimedDoor.Clamp.HasObject())
    {
        return TEXT("Timed door obstructed. Start a pulse to release the item.");
    }
    if (Player.X > Layout->PulseDoor.Center.X)
    {
        return Pulse.IsComplete() ? TEXT("Timed passage complete. Use the return switch [E].")
            : TEXT("Clear the doorway.");
    }
    return Pulse.IsPowered() ? TEXT("Pulse active. Cross the marked door.")
        : TEXT("Use the pulse switch [E], then cross before the timer expires.");
}

void UPowerLabDoorsComponent::FDoor::Bind(UBoxComponent& Leaf)
{
    Body = &Leaf;
    Closed = Leaf.GetComponentLocation();
    Clamp.AddSigns(Leaf);
}

void UPowerLabDoorsComponent::FDoor::Reset()
{
    Clamp.Release();
    if (UBoxComponent* Leaf = Body.Get())
    {
        Leaf->SetWorldLocation(Closed, false, nullptr, ETeleportType::TeleportPhysics);
    }
}

void UPowerLabDoorsComponent::Reset()
{
    Interruption.Reset();
    Reflection.Reset();
    TimedDoor.Reset();
    Pulse.Reset();
    UpdatePulseDisplay();
}

void UPowerLabDoorsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Interruption.Clamp.Release();
    Reflection.Clamp.Release();
    TimedDoor.Clamp.Release();
    Super::EndPlay(Reason);
}

void UPowerLabDoorsComponent::ReleaseIfHolding(const UBoxComponent* Body)
{
    for (FDoor* Door : {&Interruption, &Reflection, &TimedDoor})
    {
        if (Door->Clamp.Holds(Body))
        {
            Door->Clamp.Release();
        }
    }
}

bool UPowerLabDoorsComponent::UseSwitch(const UPrimitiveComponent& Target)
{
    if (&Target != PulseButton && &Target != PulseReturn)
    {
        return false;
    }
    Pulse.Press();
    UpdatePulseDisplay();
    return true;
}

bool UPowerLabDoorsComponent::MoveDoor(FDoor& Drive, bool bOpen, float Delta, APuzzleCharacter& Player,
    UBoxComponent* Cube, UBoxComponent* LooseMirror)
{
    using namespace Abandoned::Domain::Puzzles;
    UBoxComponent* Door = Drive.Body.Get();
    if (!Door)
    {
        Drive.Clamp.Release();
        return false;
    }
    const auto Sweep = Drive.Clamp.Move(*Door, Drive.Closed.Z, DoorTravelCm, DoorSpeedCm * Delta, bOpen);
    if (!Sweep.Closing || !Sweep.Hit.bBlockingHit)
    {
        return false;
    }
    UBoxComponent* Body = Cast<UBoxComponent>(Sweep.Hit.GetComponent());
    const bool bPortable = Body && (Body == Cube || Body == LooseMirror);
    const DoorContact Contact = Sweep.Hit.GetActor() == &Player ? DoorContact::Player :
        bPortable ? DoorContact::Carryable : DoorContact::None;
    switch (ResolveClosingContact(Sweep.Closing, Contact, true))
    {
    case ClosingContact::Clamp:
        if (Player.GetHeldBody() == Body)
        {
            Player.ReleaseHeldObject();
        }
        Drive.Clamp.Capture(*Body);
        break;
    case ClosingContact::ResetPlayer:
        return true;
    case ClosingContact::Block:
        break;
    }
    return false;
}

EPowerLabRecovery UPowerLabDoorsComponent::Update(float DeltaSeconds, const Abandoned::Application::Puzzles::OpticsDoors& Signals,
    APuzzleCharacter& Player, UBoxComponent* Cube, UBoxComponent* LooseMirror)
{
    const FVector Location = Player.GetActorLocation();
    const FSceneBlock& Exit = Layout->PulseExit;
    const FBox ExitZone(Exit.Center - Exit.Size * 0.5, Exit.Center + Exit.Size * 0.5);
    Pulse.Observe(DeltaSeconds, ExitZone.IsInside(Location));
    UpdatePulseDisplay();
    if (MoveDoor(Interruption, Signals.Interruption, DeltaSeconds, Player, Cube, LooseMirror) ||
        MoveDoor(Reflection, Signals.Reflection, DeltaSeconds, Player, Cube, LooseMirror))
    {
        return EPowerLabRecovery::ResetLab;
    }
    return MoveDoor(TimedDoor, Pulse.IsPowered(), DeltaSeconds, Player, Cube, LooseMirror)
        ? EPowerLabRecovery::PulseRetry : EPowerLabRecovery::None;
}
