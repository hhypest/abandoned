#include "Expedition/VanguardRoute.h"
#include "Expedition/VanguardRouteLayout.h"

#include "Expedition/VanguardSession.h"
#include "Infrastructure/Expedition/VanguardRouteStateComponent.h"
#include "Expedition/WristDisplayComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Camera/CameraComponent.h"
#include "Player/FosTechWristMount.h"
#include "Player/BeltTablet.h"
#include "Player/FosTechBody.h"
#include "EnhancedInputComponent.h"
#include "Materials/MaterialInterface.h"

namespace
{
using Abandoned::Application::Expedition::SuitPage;
using Abandoned::Application::Expedition::SuitSignal;
using Abandoned::Domain::Expedition::SuitRecord;

const FLinearColor InstrumentLabel(0.16f, 0.86f, 0.55f);
const FLinearColor InstrumentValue(0.26f, 0.98f, 0.59f);
const FLinearColor FlareWarning(1.0f, 0.62f, 0.24f);

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
    BeltTablet = CreateDefaultSubobject<UBeltTablet>(TEXT("BeltTablet"));
    BeltTablet->SetupAttachment(GetRootComponent());
    WristDisplay = CreateDefaultSubobject<UWristDisplayComponent>(TEXT("WristDisplay"));
    WristDisplay->SetupAttachment(WristCasing);
    WristDisplay->SetTickWhenOffscreen(true);
    const FTransform Projection = Abandoned::WristMount::ProjectionInCasing();
    WristDisplay->SetRelativeLocationAndRotation(Projection.GetLocation(), Projection.GetRotation());
    WristDisplay->SetRelativeScale3D(FVector(70.144 / 768.0));
    WristDisplay->SetWidgetSpace(EWidgetSpace::World);
    WristDisplay->SetDrawSize(FVector2D(768, 504));
    WristDisplay->SetPivot(FVector2D(0.5, 0.5));
    WristDisplay->SetBlendMode(EWidgetBlendMode::Transparent);
    WristDisplay->SetBackgroundColor(FLinearColor::Transparent);
    WristDisplay->SetManuallyRedraw(true);
    WristDisplay->SetWindowFocusable(false);
    WristDisplay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WristDisplay->SetCanEverAffectNavigation(false);
    WristDisplay->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    WristDisplay->SetOnlyOwnerSee(true);
    WristDisplay->SetCastShadow(false);
    WristDisplay->SetVisibility(false);
}

bool AVanguardCharacter::SupportsCustomSuitFit() const
{
    return FParse::Param(FCommandLine::Get(), TEXT("CustomMannyFit")) && BeltTablet->HasCasingAsset() && SuitBody->HasLedgeFitAssets();
}

void AVanguardCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if (GetWorld()->IsGameWorld()) { BeltTablet->Equip(*SuitBody); }
}

void AVanguardCharacter::ToggleBeltTablet()
{
    if (!IsMantling()) { BeltTablet->Toggle(); }
}

void AVanguardCharacter::ConfigureWristTerminal()
{
    if (SuitBody->IsCustomManny()) { return; }
    const auto& Suit = *GetDefault<UVanguardMantleVisuals>();
    if (!DisplayBody.IsValid())
    {
        if (UMaterialInterface* Surface = Suit.WristSurface.LoadSynchronous())
        {
            WristDisplay->SetMaterial(0, Surface);
        }
        WristDisplay->SetSlateWidget(
            SNew(SBox).Padding(FMargin(28, 16)).Visibility(EVisibility::HitTestInvisible)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [
                    SAssignNew(DisplayHeading, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 31))
                    .ColorAndOpacity(InstrumentLabel)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [
                    SAssignNew(DisplayPrimary, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 56))
                    .ColorAndOpacity(InstrumentValue)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(DisplayBody, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 36))
                    .ColorAndOpacity(FLinearColor(0.86f, 0.93f, 0.85f))
                ]
            ]);
        WristDisplay->PrepareGlass();
        // Request close-up mips early without deprioritizing the level or pinning the atlas permanently.
        WristCasing->PrestreamTextures(2.0f, false);
        SuitBody->PrestreamTextures(2.0f, false);
    }
}

void AVanguardCharacter::BindWristTerminalInput(UEnhancedInputComponent& Input)
{
    if (SuitBody->IsCustomManny())
    {
        if (BeltTablet->IsEquipped())
        {
            Input.BindAction(MapAction(EKeys::Tab), ETriggerEvent::Started, this, &AVanguardCharacter::ToggleBeltTablet);
            Input.BindAction(MapAction(EKeys::PageDown), ETriggerEvent::Started, BeltTablet.Get(), &UBeltTablet::Browse, true);
            Input.BindAction(MapAction(EKeys::PageUp), ETriggerEvent::Started, BeltTablet.Get(), &UBeltTablet::Browse, false);
        }
        return;
    }
    Input.BindAction(MapAction(EKeys::T), ETriggerEvent::Started, this, &AVanguardCharacter::ToggleWristTerminal);
    Input.BindAction(MapAction(EKeys::Tab), ETriggerEvent::Started, this, &AVanguardCharacter::AdvanceTerminalPage);
    Input.BindAction(MapAction(EKeys::PageUp), ETriggerEvent::Started, this, &AVanguardCharacter::PreviousArchiveRecord);
    Input.BindAction(MapAction(EKeys::PageDown), ETriggerEvent::Started, this, &AVanguardCharacter::NextArchiveRecord);
}

void AVanguardCharacter::ToggleWristTerminal()
{
    if (!IsMantling() && !SuitBody->IsCustomManny())
    {
        WristTerminal.Toggle();
        WristDisplay->SetVisibility(IsWristTerminalOpen());
    }
}

void AVanguardCharacter::CloseWristTerminal()
{
    BeltTablet->Holster(*SuitBody);
    WristTerminal.Close();
    WristDisplay->SetVisibility(false);
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
    FString Primary;
    FString Body;
    switch (*Page)
    {
    case SuitPage::Status:
        Primary = FString::Printf(TEXT("OXYGEN  %.1f%%"), Reading.OxygenPercent);
        Body = FString::Printf(TEXT("DOSE    %.1f%% OF LIMIT"), Reading.DosePercent);
        if (Reading.Forecast)
        {
            const auto& Forecast = *Reading.Forecast;
            const int32 Seconds = FMath::CeilToInt(Forecast.SecondsToChange);
            if (Forecast.FlareActive)
            {
                using Abandoned::Domain::Expedition::RadiationPosition;
                Heading = Forecast.Position == RadiationPosition::Sheltered ? TEXT("FLARE / SHELTERED")
                    : Forecast.Position == RadiationPosition::Exposed ? TEXT("FLARE / EXPOSED") : TEXT("FLARE / SAFE APPROACH");
                Body = Primary + TEXT("\n") + Body;
                Primary = FString::Printf(TEXT("ENDS IN  %d s"), Seconds);
            }
            else
            {
                Body += FString::Printf(TEXT("\nNEXT FLARE IN %d s"), Seconds);
            }
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
    if (DisplayBody.IsValid() && (WristHeading != Heading || WristPrimary != Primary || WristReadout != Body))
    {
        WristHeading = Heading;
        WristPrimary = MoveTemp(Primary);
        WristReadout = MoveTemp(Body);
        DisplayHeading->SetText(FText::FromString(WristHeading));
        const bool bFlareStatus = *Page == SuitPage::Status && Reading.Forecast && Reading.Forecast->FlareActive;
        DisplayHeading->SetColorAndOpacity(bFlareStatus ? FlareWarning : InstrumentLabel);
        DisplayPrimary->SetColorAndOpacity(bFlareStatus ? FlareWarning : InstrumentValue);
        DisplayPrimary->SetVisibility(WristPrimary.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
        DisplayPrimary->SetText(FText::FromString(WristPrimary));
        DisplayBody->SetText(FText::FromString(WristReadout));
        WristDisplay->RequestRenderUpdate();
    }
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
    const bool bReading = Player->BeltTablet->IsReading();
    if (Dosimeter)
    {
        const float Y = bReading ? Canvas->ClipY - 100.f : 152.f;
        using Abandoned::Domain::Expedition::RadiationPosition;
        const TCHAR* Zone = Dosimeter->Position == RadiationPosition::Sheltered ? TEXT("SHELTERED")
            : Dosimeter->Position == RadiationPosition::Exposed ? TEXT("EXPOSED") : TEXT("SAFE APPROACH");
        const TCHAR* Phase = Dosimeter->FlareActive ? TEXT("FLARE ENDS IN") : TEXT("NEXT FLARE IN");
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, Y, 310.0f, 88.0f);
        DrawText(FString::Printf(TEXT("DOSIMETER / %s"), Zone), FLinearColor::White, 24.0f, Y + 6);
        DrawText(FString::Printf(TEXT("%s %d s"), Phase, FMath::CeilToInt(Dosimeter->SecondsToChange)),
            FLinearColor::White, 24.0f, Y + 26);
        DrawText(FString::Printf(TEXT("DOSE %.1f%% OF LIMIT  |  %.1f%%/s"), Dosimeter->DosePercent, Dosimeter->DosePerSecond),
            FLinearColor::White, 24.0f, Y + 46);
        DrawText(TEXT("[R] Restart passage"), FLinearColor::White, 24.0f, Y + 66);
    }
    if (Route.HasCheckpointWriteFailed())
    {
        DrawText(TEXT("Could not save resupply record. Previous data preserved."), FLinearColor::White, 24.0f, bReading ? 46.0f : 270.0f);
    }
    const bool bLoadFailed = Player->Segment->StationLoadFailed();
    if (bLoadFailed)
    {
        DrawText(TEXT("Station unavailable. [R] Retry airlock"), FLinearColor::Yellow, 24.f, bReading ? 18.f : 80.f);
    }
    if (Route.HasMinorInjury())
    {
        const float Y = bReading ? 18.f : 129.f;
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, Y, 330.0f, 23.0f);
        DrawText(TEXT("Suit: minor impact injury. Recovering gait."), FLinearColor(1.0, 0.68, 0.23), 24.0f, Y + 3);
    }
    if (bReading) { return; }
    const TCHAR* Location = Reading.AtStation ? TEXT("VANGUARD / OUTPOST 01") : TEXT("PASSAGE / VANGUARD OUTPOST 01");
    DrawText(Dosimeter ? TEXT("ENERGY TIER / EXTERIOR GALLERY") : Location,
        FLinearColor::White, 24.0f, 24.0f, nullptr, 1.2f);
    if (!Player->IsWristTerminalOpen())
    {
        DrawText(FString::Printf(TEXT("Suit oxygen: %.0f%%"), Reading.OxygenPercent), FLinearColor::White, 24.0f, 54.0f);
    }
    if (!bLoadFailed && Player->CanReachSupply())
    {
        DrawText(TEXT("[E] Connect oxygen supply"), FLinearColor(1.0, 0.68, 0.23), 24.0f, 80.0f);
    }
    else if (!bLoadFailed && Route.HasResupplied())
    {
        DrawText(TEXT("Supply restored"), FLinearColor(1.0, 0.68, 0.23), 24.0f, 80.0f);
    }
    DrawText(Player->IsFlashlightOn() ? TEXT("[F] Shoulder light on") : TEXT("[F] Shoulder light off"),
        FLinearColor::White, 24.0f, 106.0f);
    if (Route.HasClearedRadiation())
    {
        DrawText(TEXT("Station decontamination complete"), FLinearColor::White, 24.0f, 132.0f);
    }
    if (Player->CanMantle() && !Player->IsMantling())
    {
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, 152.0f, 194.0f, 26.0f);
        DrawText(TEXT("[Hold Space] Climb ledge"), FLinearColor::White, 24.0f, 158.0f);
    }
    if (!Player->IsMantling() && (Player->BeltTablet->IsEquipped() || !Player->SuitBody->IsCustomManny()))
    {
        const float Y = Dosimeter ? 244.0f : 183.0f;
        const TCHAR* Tip = Player->BeltTablet->IsEquipped() ? TEXT("[Tab] Поясной КПК") : Player->IsWristTerminalOpen()
            ? TEXT("[T] Lower wrist  [Tab] Page  [PgUp/PgDn] Archive")
            : TEXT("[T] Wrist terminal");
        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f), 16.0f, Y, Player->IsWristTerminalOpen() ? 370.0f : 145.0f, 23.0f);
        DrawText(Tip, FLinearColor::White, 24.0f, Y + 3.0f);
    }
}
