#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/Blockout.h"

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
    WristRoot->SetRelativeLocation(FVector(52.0, -26.0, -14.0));
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
    WristCasing = MakePart(TEXT("WristCasing"), FVector::ZeroVector, FVector(0.042, 0.30, 0.163));
    WristDisplay = MakePart(TEXT("WristDisplay"), FVector(-2.2, 0, 0), FVector(0.012, 0.277, 0.139));
    WristSleeve = MakePart(TEXT("WristSleeve"), FVector(3.5, 1, -1.5), FVector(1.35, 1.0, 1.0));
    WristSleeve->SetRelativeRotation(FRotationMatrix::MakeFromX(FVector(-8, -27, -16)).Rotator());
    WristGlove = MakePart(TEXT("WristGlove"), FVector(4, 10, -0.5), FVector::OneVector);
    WristGlove->SetRelativeRotation(FRotationMatrix::MakeFromXZ(FVector(0, 1, 0.25), -FVector::ForwardVector).Rotator());

    WristRoot->SetVisibility(false);
}

void AVanguardCharacter::ConfigureWristTerminal(UMaterialInterface* Surface)
{
    UStaticMesh* Cube = Abandoned::Blockout::LoadCube();
    WristCasing->SetStaticMesh(Cube);
    WristDisplay->SetStaticMesh(Cube);
    const UVanguardMantleVisuals& Suit = *GetDefault<UVanguardMantleVisuals>();
    WristSleeve->SetStaticMesh(Suit.Sleeve.LoadSynchronous());
    WristGlove->SetStaticMesh(Suit.LeftGlove.LoadSynchronous());
    if (UMaterialInterface* SuitFinish = Suit.Finish.LoadSynchronous())
    {
        WristSleeve->SetMaterial(0, SuitFinish);
        WristGlove->SetMaterial(0, SuitFinish);
    }
    const auto SetFinish = [this, Surface](UStaticMeshComponent& Part, const FLinearColor& Tint, float Glow)
    {
        if (UMaterialInstanceDynamic* Finish = UMaterialInstanceDynamic::Create(Surface, this))
        {
            Finish->SetVectorParameterValue(TEXT("DiffuseColor"), Tint);
            Finish->SetScalarParameterValue(TEXT("Roughness"), 0.62f);
            Finish->SetScalarParameterValue(TEXT("Glow"), Glow);
            Part.SetMaterial(0, Finish);
        }
    };
    SetFinish(*WristCasing, FLinearColor(0.014f, 0.022f, 0.027f), 0.14f);
    SetFinish(*WristDisplay, FLinearColor(0.001f, 0.002f, 0.003f), 0.04f);
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
    const AVanguardRouteGameMode* Mode = GetWorld()->GetAuthGameMode<AVanguardRouteGameMode>();
    if (const UVanguardSession* Session = Mode ? Mode->GetSession() : nullptr)
    {
        WristTerminal.MoveArchive(Forward, Session->Suit.Records());
    }
}

void AVanguardCharacter::UpdateWristTerminal(const AVanguardRouteGameMode& Mode)
{
    const auto Page = WristTerminal.Page();
    const UVanguardSession* Session = Mode.GetSession();
    if (!Page || !Session)
    {
        return;
    }

    const auto Reading = Mode.GetSuitTelemetry();
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
    if (!Player.IsWristTerminalOpen() || !Player.WristDisplay->IsVisible())
    {
        return;
    }
    // Project the face of the camera-mounted display so text stays crisp in the dark and while moving.
    const FTransform& Face = Player.WristDisplay->GetComponentTransform();
    const FVector Top = Project(Face.TransformPosition(FVector(-50, -44, 42)));
    const FVector Bottom = Project(Face.TransformPosition(FVector(-50, -44, -50)));
    const float Height = static_cast<float>(Bottom.Y - Top.Y);
    if (Height <= 0.0f)
    {
        return;
    }
    const float Scale = Height / 110.0f;
    DrawText(Player.WristHeading, FLinearColor(0.49f, 0.95f, 0.88f), Top.X, Top.Y, nullptr, Scale * 1.12f);
    DrawText(Player.WristReadout, FLinearColor(0.9f, 0.95f, 0.98f), Top.X, Top.Y + Height * 0.22f, nullptr, Scale);
}
