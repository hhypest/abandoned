#include "Puzzles/PowerLab.h"
#include "Puzzles/PowerLabLayout.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "World/Blockout.h"

namespace
{
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

bool APowerLabGameMode::SpawnPulseBay(UStaticMesh& Mesh, UMaterialInterface& Steel, UMaterialInterface& Panel,
    UMaterialInterface& Screen)
{
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
    PulseDoor = Door.Collision;
    const auto Line = Box(FVector(Closed.Center.X - 200.0, Closed.Center.Y, 3.0),
        FVector(400, 10, 6), false, *Inactive);
    const auto Display = Box(Closed.Center + FVector(-39, 0, 178), FVector(12, 230, 62), false, Screen);
    if (!PulseButton || !PulseReturn || !PulseDoor || !Door.Visual || !Line.Visual || !Display.Collision || !Display.Visual)
    {
        return false;
    }
    PulseClamp.AddSigns(*PulseDoor);
    PulseIndicators.Add(Line.Visual);
    PulseClock = AddPulseLabel(*Display.Collision, FVector(-7, 0, 0), FRotator(0, 180, 0), 30.0f, TEXT("READY"));
    Pulse = Abandoned::Application::Puzzles::TimedPassage(Layout->PulseSeconds);
    UpdatePulseDisplay();
    return true;
}

bool APowerLabGameMode::IsInPulseBay(const FVector& Player) const
{
    return Layout && IsValid(PulseDoor) && Player.X > Layout->Reflection.Door.X + 90.0;
}

float APowerLabGameMode::GetPulseSeconds() const
{
    return Pulse.RemainingSeconds();
}

float APowerLabGameMode::GetPulseFraction() const
{
    return Layout && Layout->PulseSeconds > 0.0f ? FMath::Clamp(GetPulseSeconds() / Layout->PulseSeconds, 0.0f, 1.0f) : 0.0f;
}

void APowerLabGameMode::ObservePulse(float DeltaSeconds, const FVector& Location)
{
    const FSceneBlock& Exit = Layout->PulseExit;
    const FBox ExitZone(Exit.Center - Exit.Size * 0.5, Exit.Center + Exit.Size * 0.5);
    Pulse.Observe(DeltaSeconds, ExitZone.IsInside(Location));
    UpdatePulseDisplay();
}

void APowerLabGameMode::UpdatePulseDisplay()
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

void APowerLabGameMode::ResetPulseBay()
{
    Pulse.Reset();
    PulseClamp.Release();
    PulseDoor->SetWorldLocation(Layout->PulseDoor.Center, false, nullptr, ETeleportType::TeleportPhysics);
    UpdatePulseDisplay();
}
