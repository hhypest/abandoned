#include "Infrastructure/Puzzles/UnrealSlidingDoor.h"

#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "Infrastructure/Puzzles/DoorClampSignSettings.h"
#include "Puzzles/PuzzleCube.h"

namespace Abandoned::Infrastructure::Puzzles
{
namespace
{
const FName DoorClampTag(TEXT("ClampedBySlidingDoor"));
}

bool IsDoorClamped(const UPrimitiveComponent* Body)
{
    return IsValid(Body) && Body->ComponentHasTag(DoorClampTag);
}

void SlidingDoorClamp::AddSigns(UBoxComponent& Door)
{
    const UDoorClampSignSettings& Settings = *GetDefault<UDoorClampSignSettings>();
    if (!FMath::IsFinite(Settings.TextSizeCm) || Settings.TextSizeCm <= 0.0f)
    {
        return;
    }
    AActor* Owner = Door.GetOwner();
    for (const double Side : {-1.0, 1.0})
    {
        auto* Label = NewObject<UTextRenderComponent>(Owner);
        Owner->AddInstanceComponent(Label);
        Label->SetupAttachment(&Door);
        Label->SetRelativeLocation(FVector(Side * (Door.GetUnscaledBoxExtent().X + 2.0),
            0.0, -Door.GetUnscaledBoxExtent().Z * 0.5));
        Label->SetRelativeRotation(FRotator(0.0, Side < 0.0 ? 180.0 : 0.0, 0.0));
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetVerticalAlignment(EVRTA_TextCenter);
        Label->SetWorldSize(Settings.TextSizeCm);
        Label->SetTextRenderColor(Settings.TextColor);
        Label->SetText(NSLOCTEXT("SlidingDoor", "ClampedSign", "OBSTRUCTED"));
        Label->SetVisibility(false);
        Label->RegisterComponent();
        Signs.Add(Label);
    }
}

void SlidingDoorClamp::ShowSigns(bool Visible)
{
    for (const auto& Sign : Signs)
    {
        if (UTextRenderComponent* Label = Sign.Get())
        {
            Label->SetVisibility(Visible);
        }
    }
}

DoorSweep SlidingDoorClamp::Move(UBoxComponent& Door, double ClosedZ, double TravelCm, double StepCm, bool Opening)
{
    if (Clamped.IsStale())
    {
        Release();
    }
    if (HasObject() && !Opening)
    {
        return {};
    }

    const FVector From = Door.GetComponentLocation();
    FVector To = From;
    const double TargetZ = ClosedZ + (Opening ? TravelCm : 0.0);
    To.Z += FMath::Clamp(TargetZ - From.Z, -StepCm, StepCm);
    DoorSweep Contact;
    Contact.Closing = To.Z < From.Z;
    if (To.Z != From.Z)
    {
        Door.SetWorldLocation(To, true, &Contact.Hit);
    }
    if (const UBoxComponent* Body = Clamped.Get(); Body && Domain::Puzzles::CanReleaseDoorClamp(
        Opening, Door.Bounds.GetBox().Min.Z, Body->Bounds.GetBox().Max.Z))
    {
        Release();
    }
    return Contact;
}

bool SlidingDoorClamp::Capture(UBoxComponent& Body)
{
    if (HasObject() || IsDoorClamped(&Body))
    {
        return false;
    }
    if (APuzzleCube* Cube = Cast<APuzzleCube>(Body.GetOwner()); Cube && Cube->HasSnapConstraints())
    {
        Cube->ReleaseSnapConstraints();
    }
    if (!Body.IsSimulatingPhysics())
    {
        return false;
    }
    Body.SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body.SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    Body.SetSimulatePhysics(false);
    Body.ComponentTags.AddUnique(DoorClampTag);
    Clamped = &Body;
    ShowSigns(true);
    return true;
}

void SlidingDoorClamp::Release()
{
    UBoxComponent* Body = Clamped.Get();
    Clamped.Reset();
    ShowSigns(false);
    if (Body)
    {
        Body->ComponentTags.Remove(DoorClampTag);
        Body->SetSimulatePhysics(true);
        Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
        Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        Body->WakeRigidBody();
    }
}
}
