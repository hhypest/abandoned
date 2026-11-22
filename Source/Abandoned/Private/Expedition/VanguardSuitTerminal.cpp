#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#include "Expedition/VanguardSession.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
using Abandoned::Application::Expedition::SuitPage;
using Abandoned::Application::Expedition::SuitSignal;
using Abandoned::Domain::Expedition::SuitRecord;

const TCHAR* RecordText(SuitRecord Record)
{
    switch (Record)
    {
    case SuitRecord::ForecastSynchronized:
        return TEXT("EXTERIOR DOSIMETER\nFlare forecast acquired.\nUse cover during a flare.");
    case SuitRecord::Decontaminated:
        return TEXT("VANGUARD OUTPOST 01\nDecontamination complete.\nResidual dose cleared.");
    case SuitRecord::OxygenRestored:
        return TEXT("VANGUARD OUTPOST 01\nOxygen supply connected.\nSuit reservoir refilled.");
    }
    return TEXT("Entry unavailable.");
}
}

void AVanguardCharacter::CreateWristTerminal()
{
    WristRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SuitWrist"));
    WristRoot->SetupAttachment(Camera);
    WristRoot->SetRelativeLocation(FVector(48.0, -15.0, -12.0));
    // Keep the raised wrist inside the player's collision envelope without changing its framing.
    Camera->SetEnableFirstPersonScale(true);
    Camera->SetFirstPersonScale(0.45f);

    const auto MakePart = [this](const FName& Name, FVector Position, FVector Scale)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(WristRoot);
        Part->SetRelativeLocation(Position);
        Part->SetRelativeScale3D(Scale);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
        Part->SetOnlyOwnerSee(true);
        Part->SetCastShadow(false);
        Part->SetVisibility(false);
        return Part;
    };
    // The EQP-01 glass faces local +Y. Seat the rear plate against the raised left sleeve.
    WristCasing = MakePart(TEXT("WristCasing"), FVector::ZeroVector, FVector(0.15));
    WristCasing->SetRelativeRotation(FRotator(0, 90, 0));
    WristSleeve = MakePart(TEXT("WristSleeve"), FVector(-7.6, 3, 0), FVector(1.2, 1, 1));
    WristSleeve->SetRelativeRotation(FRotationMatrix::MakeFromX(FVector(0, -27, -8)).Rotator());
    WristGlove = MakePart(TEXT("WristGlove"), FVector(-7.6, 13, 1.6), FVector::OneVector);

    WristRoot->SetVisibility(false);
}

void AVanguardCharacter::ConfigureWristTerminal()
{
    const auto& Suit = *GetDefault<UVanguardMantleVisuals>();
    WristCasing->SetStaticMesh(Suit.WristTerminal.LoadSynchronous());
    WristGlove->SetStaticMesh(Suit.Glove.LoadSynchronous());
    const FQuat Hand = FRotationMatrix::MakeFromXZ(FVector(0, 1, 0.18), -FVector::ForwardVector).ToQuat();
    WristGlove->SetRelativeRotation(Hand * Suit.GloveRotation.Quaternion());
    WristGlove->SetRelativeScale3D(FVector(Suit.GloveScale));
    WristSleeve->SetStaticMesh(Suit.Sleeve.LoadSynchronous());
    WristSleeve->SetMaterial(0, Suit.Finish.LoadSynchronous());
}

void AVanguardCharacter::BindWristTerminalInput(UEnhancedInputComponent& Input)
{
    Input.BindAction(MapAction(EKeys::T), ETriggerEvent::Started, this, &AVanguardCharacter::ToggleWristTerminal);
    Input.BindAction(MapAction(EKeys::Tab), ETriggerEvent::Started, this, &AVanguardCharacter::AdvanceTerminalPage);
    Input.BindAction(MapAction(EKeys::PageUp), ETriggerEvent::Started, this, &AVanguardCharacter::PreviousArchiveRecord);
    Input.BindAction(MapAction(EKeys::PageDown), ETriggerEvent::Started, this, &AVanguardCharacter::NextArchiveRecord);
}

void AVanguardCharacter::ToggleWristTerminal()
{
    if (!IsMantling())
    {
        WristTerminal.Toggle();
        WristRoot->SetVisibility(IsWristTerminalOpen(), true);
    }
}

void AVanguardCharacter::CloseWristTerminal()
{
    WristTerminal.Close();
    WristRoot->SetVisibility(false, true);
}

void AVanguardCharacter::AdvanceTerminalPage()
{
    WristTerminal.NextPage();
}

void AVanguardCharacter::NextArchiveRecord()
{
    MoveArchive(true);
}

void AVanguardCharacter::PreviousArchiveRecord()
{
    MoveArchive(false);
}

void AVanguardCharacter::MoveArchive(bool Forward)
{
    if (const UVanguardSession* Session = UVanguardSession::Find(*this))
    {
        WristTerminal.MoveArchive(Forward, Session->Suit.Records());
    }
}

void AVanguardCharacter::UpdateWristTerminal(const UVanguardRouteStateComponent& Route)
{
    const auto Page = WristTerminal.Page();
    const UVanguardSession* Session = UVanguardSession::Find(*this);
    if (!Page || !Session)
    {
        return;
    }

    const auto Reading = Route.GetSuitTelemetry();
    const TCHAR* Heading = TEXT("SUIT / STATUS");
    FString Body;
    switch (*Page)
    {
    case SuitPage::Status:
        Body = FString::Printf(TEXT("OXYGEN  %.1f%%\nDOSE    %.1f%% OF LIMIT"), Reading.OxygenPercent, Reading.DosePercent);
        if (Reading.Forecast)
        {
            const auto& Forecast = *Reading.Forecast;
            Body += FString::Printf(TEXT("\n%s IN %d s"),
                Forecast.FlareActive ? TEXT("FLARE ENDS") : TEXT("NEXT FLARE"),
                FMath::CeilToInt(Forecast.SecondsToChange));
        }
        break;
    case SuitPage::Signal:
        Heading = TEXT("SUIT / SIGNAL");
        switch (Reading.Signal())
        {
        case SuitSignal::ExteriorForecast:
            Body = TEXT("EXTERIOR DOSIMETER\nLink active\nForecast receiving");
            break;
        case SuitSignal::VanguardStation:
            Body = TEXT("VANGUARD OUTPOST 01\nLocal link active\nExterior forecast offline");
            break;
        case SuitSignal::Unavailable:
            Body = TEXT("No signal in range");
            break;
        }
        break;
    case SuitPage::Archive:
        Heading = TEXT("SUIT / ARCHIVE");
        if (const auto Record = Session->Suit.Records().At(WristTerminal.ArchiveIndex()))
        {
            Body = FString::Printf(TEXT("%d / %d\n%s"), static_cast<int32>(WristTerminal.ArchiveIndex() + 1),
                static_cast<int32>(Session->Suit.Records().Size()), RecordText(*Record));
        }
        else
        {
            Body = TEXT("No records yet");
        }
        break;
    }
    WristHeading = Heading;
    WristReadout = MoveTemp(Body);
}

void AVanguardHUD::DrawWristReadout(const AVanguardCharacter& Player)
{
    if (!Player.IsWristTerminalOpen() || !Player.WristCasing->IsVisible())
    {
        return;
    }
    // Project the face of the camera-mounted display so text stays crisp in the dark and while moving.
    // Inset corners on the recessed EQP-01 glass, measured in imported mesh space (cm).
    const FTransform& Face = Player.WristCasing->GetComponentTransform();
    const FVector Top = Project(Face.TransformPosition(FVector(-48, 82, 49)));
    const FVector Bottom = Project(Face.TransformPosition(FVector(-48, 93, 1)));
    const float Height = static_cast<float>(Bottom.Y - Top.Y);
    if (Height <= 0.0f)
    {
        return;
    }
    const float Scale = Height / 110.0f;
    DrawText(Player.WristHeading, FLinearColor(0.49f, 0.95f, 0.88f), Top.X, Top.Y, nullptr, Scale * 1.12f);
    DrawText(Player.WristReadout, FLinearColor(0.9f, 0.95f, 0.98f), Top.X, Top.Y + Height * 0.22f, nullptr, Scale);
}

void AVanguardHUD::DrawHUD()
{
    Super::DrawHUD();
    const AVanguardCharacter* Player = Cast<AVanguardCharacter>(GetOwningPawn());
    if (!Player || !Player->Segment)
    {
        return;
    }
    const UVanguardRouteStateComponent& Route = Player->Segment->GetRouteState();
    const auto Reading = Route.GetSuitTelemetry();
    const auto& Dosimeter = Reading.Forecast;
    const TCHAR* Location = Reading.AtStation ? TEXT("VANGUARD / OUTPOST 01") : TEXT("PASSAGE / VANGUARD OUTPOST 01");
    DrawText(Dosimeter ? TEXT("ENERGY TIER / EXTERIOR GALLERY") : Location,
        FLinearColor::White, 24.0f, 24.0f, nullptr, 1.2f);
    DrawText(FString::Printf(TEXT("Suit oxygen: %.0f%%"), Reading.OxygenPercent), FLinearColor::White, 24.0f, 54.0f);
    if (Player->Segment->StationLoadFailed())
    {
        DrawText(TEXT("Station unavailable. [R] Retry airlock"), FLinearColor::Yellow, 24.0f, 80.0f);
    }
    else if (Player->CanReachSupply())
    {
        DrawText(TEXT("[E] Connect oxygen supply"), FLinearColor(1.0, 0.68, 0.23), 24.0f, 80.0f);
    }
    else if (Route.HasResupplied())
    {
        DrawText(TEXT("Supply restored"), FLinearColor(1.0, 0.68, 0.23), 24.0f, 80.0f);
    }
    DrawText(Player->IsFlashlightOn() ? TEXT("[F] Shoulder light on") : TEXT("[F] Shoulder light off"),
        FLinearColor::White, 24.0f, 106.0f);
    if (Route.HasMinorInjury())
    {
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 129.0f, 330.0f, 23.0f);
        DrawText(TEXT("Suit: minor impact injury. Recovering gait."), FLinearColor(1.0, 0.68, 0.23), 24.0f, 132.0f);
    }
    if (Route.HasClearedRadiation())
    {
        DrawText(TEXT("Station decontamination complete"), FLinearColor::White, 24.0f, 132.0f);
    }
    if (Player->CanMantle() && !Player->IsMantling())
    {
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 152.0f, 194.0f, 26.0f);
        DrawText(TEXT("[Hold Space] Climb ledge"), FLinearColor::White, 24.0f, 158.0f);
    }
    if (Dosimeter)
    {
        using Abandoned::Domain::Expedition::RadiationPosition;
        const TCHAR* Zone = Dosimeter->Position == RadiationPosition::Sheltered ? TEXT("SHELTERED")
            : Dosimeter->Position == RadiationPosition::Exposed ? TEXT("EXPOSED") : TEXT("SAFE APPROACH");
        const TCHAR* Phase = Dosimeter->FlareActive ? TEXT("FLARE ENDS IN") : TEXT("NEXT FLARE IN");
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 152.0f, 310.0f, 88.0f);
        DrawText(FString::Printf(TEXT("DOSIMETER / %s"), Zone), FLinearColor::White, 24.0f, 158.0f);
        DrawText(FString::Printf(TEXT("%s %d s"), Phase, FMath::CeilToInt(Dosimeter->SecondsToChange)),
            FLinearColor::White, 24.0f, 178.0f);
        DrawText(FString::Printf(TEXT("DOSE %.1f%% OF LIMIT  |  %.1f%%/s"), Dosimeter->DosePercent, Dosimeter->DosePerSecond),
            FLinearColor::White, 24.0f, 198.0f);
        DrawText(TEXT("[R] Restart passage"), FLinearColor::White, 24.0f, 218.0f);
    }
    if (!Player->IsMantling())
    {
        const float Y = Dosimeter ? 244.0f : 183.0f;
        const TCHAR* Tip = Player->IsWristTerminalOpen()
            ? TEXT("[T] Lower wrist  [Tab] Page  [PgUp/PgDn] Archive")
            : TEXT("[T] Wrist terminal");
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, Y, Player->IsWristTerminalOpen() ? 370.0f : 145.0f, 23.0f);
        DrawText(Tip, FLinearColor::White, 24.0f, Y + 3.0f);
    }
    if (Route.HasCheckpointWriteFailed())
    {
        DrawText(TEXT("Could not save resupply record. Previous data preserved."), FLinearColor::White, 24.0f, 270.0f);
    }
    DrawWristReadout(*Player);
}
