#include "Player/AbandonedTestController.h"

#include "Algo/Find.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr int32 FrameRateLimits[] = {30, 60, 120, 180};

class STestMenu final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(STestMenu) {}
        SLATE_EVENT(FSimpleDelegate, OnClose)
        SLATE_EVENT(FSimpleDelegate, OnFullscreen)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        OnClose = Args._OnClose;
        OnFullscreen = Args._OnFullscreen;
        ChildSlot[Args._Content.Widget];
    }

    bool SupportsKeyboardFocus() const override { return true; }

    FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        const FKey Key = Event.GetKey();
        if (Key == EKeys::Escape || Key == EKeys::M || Key == EKeys::F11)
        {
            if (!Event.IsRepeat())
            {
                // The close delegate can remove this widget from the viewport.
                const FSimpleDelegate Callback = Key == EKeys::F11 ? OnFullscreen : OnClose;
                Callback.ExecuteIfBound();
            }
            return FReply::Handled();
        }
        return SCompoundWidget::OnKeyDown(Geometry, Event);
    }

private:
    FSimpleDelegate OnClose;
    FSimpleDelegate OnFullscreen;
};
}

void AAbandonedTestController::BeginPlay()
{
    Super::BeginPlay();
    if (CanChangeDisplaySettings())
    {
        UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
        if (!Algo::Find(FrameRateLimits, Settings->GetFrameRateLimit()))
        {
            // Older profiles can contain the former unlimited cap.
            Settings->SetFrameRateLimit(60);
            Settings->ApplyNonResolutionSettings();
            Settings->SaveSettings();
        }
    }
}

void AAbandonedTestController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AAbandonedTestController::ToggleMenu);
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AAbandonedTestController::ToggleMenu);
    InputComponent->BindKey(EKeys::F11, IE_Pressed, this, &AAbandonedTestController::ToggleFullscreen);
}

void AAbandonedTestController::ToggleMenu()
{
    if (Menu)
    {
        CloseMenu();
        return;
    }
    ULocalPlayer* LocalPlayer = GetLocalPlayer();
    if (!LocalPlayer || !LocalPlayer->ViewportClient || !SetPause(true))
    {
        return;
    }

    TSharedPtr<SButton> ResumeButton;
    Menu = SNew(STestMenu)
        .OnClose(FSimpleDelegate::CreateUObject(this, &AAbandonedTestController::CloseMenu))
        .OnFullscreen(FSimpleDelegate::CreateUObject(this, &AAbandonedTestController::ToggleFullscreen))
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(320.0f)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot().Padding(20.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
                        [
                            SNew(STextBlock)
                            .Text(NSLOCTEXT("TestMenu", "Title", "Test Menu"))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SAssignNew(ResumeButton, SButton)
                            .Text(NSLOCTEXT("TestMenu", "Resume", "Resume"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::Resume)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text_UObject(this, &AAbandonedTestController::GetVSyncLabel)
                            .IsEnabled_UObject(this, &AAbandonedTestController::CanChangeDisplaySettings)
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::VSync)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text_UObject(this, &AAbandonedTestController::GetFrameLimitLabel)
                            .IsEnabled_UObject(this, &AAbandonedTestController::CanChangeDisplaySettings)
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::FrameRate)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "EntryCourse", "Load Entry Course"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::TrainingChamber)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "Balance", "Load Balance Chamber"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::Balance)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "Lab", "Load Movement Lab"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::MovementLab)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "Optics", "Load Optics Lab"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::PowerLab)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "Gravity", "Load Gravity Lab"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::GravityLab)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "PortalLab", "Load Portal Lab"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::PortalLab)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "Dev20", "Load DEV-20 Route"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::Dev20)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "Radiation", "Load Radiation Passage"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::Radiation)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text_UObject(this, &AAbandonedTestController::GetFullscreenLabel)
                            .IsEnabled_UObject(this, &AAbandonedTestController::CanChangeDisplaySettings)
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::Fullscreen)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                        [
                            SNew(SButton)
                            .Text(NSLOCTEXT("TestMenu", "Quit", "Quit Game"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::Quit)
                        ]
                    ]
                ]
            ]
        ];
    LocalPlayer->ViewportClient->AddViewportWidgetForPlayer(LocalPlayer, Menu.ToSharedRef(), 10);
    FInputModeUIOnly Input;
    Input.SetWidgetToFocus(ResumeButton).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Input);
    bShowMouseCursor = true;
}

void AAbandonedTestController::RemoveMenu()
{
    if (!Menu)
    {
        return;
    }
    if (ULocalPlayer* LocalPlayer = GetLocalPlayer(); LocalPlayer && LocalPlayer->ViewportClient)
    {
        LocalPlayer->ViewportClient->RemoveViewportWidgetForPlayer(LocalPlayer, Menu.ToSharedRef());
    }
    Menu.Reset();
}

void AAbandonedTestController::CloseMenu()
{
    RemoveMenu();
    bShowMouseCursor = false;
    FInputModeGameOnly Input;
    SetInputMode(Input);
    SetPause(false);
}

void AAbandonedTestController::EndPlay(const EEndPlayReason::Type Reason)
{
    RemoveMenu();
    Super::EndPlay(Reason);
}

bool AAbandonedTestController::CanChangeDisplaySettings() const
{
    const ULocalPlayer* LocalPlayer = GetLocalPlayer();
    return LocalPlayer && LocalPlayer->ViewportClient && LocalPlayer->ViewportClient->GetWindow().IsValid() &&
        GetWorld() && GetWorld()->WorldType == EWorldType::Game && UGameUserSettings::GetGameUserSettings();
}

FText AAbandonedTestController::GetFullscreenLabel() const
{
    if (!CanChangeDisplaySettings())
    {
        return NSLOCTEXT("TestMenu", "FullscreenStandalone", "Fullscreen available in standalone");
    }
    return GetLocalPlayer()->ViewportClient->GetWindow()->GetWindowMode() == EWindowMode::Windowed
        ? NSLOCTEXT("TestMenu", "EnterFullscreen", "Fullscreen (F11)")
        : NSLOCTEXT("TestMenu", "ExitFullscreen", "Windowed (F11)");
}

FText AAbandonedTestController::GetVSyncLabel() const
{
    const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
    return Settings && Settings->IsVSyncEnabled()
        ? NSLOCTEXT("TestMenu", "VSyncOn", "Vertical sync: On")
        : NSLOCTEXT("TestMenu", "VSyncOff", "Vertical sync: Off");
}

FText AAbandonedTestController::GetFrameLimitLabel() const
{
    const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
    return FText::Format(NSLOCTEXT("TestMenu", "FrameLimit", "Frame limit: {0} FPS"),
        FText::AsNumber(Settings ? Settings->GetFrameRateLimit() : 60.0f));
}

void AAbandonedTestController::AbandonedToggleVSync()
{
    if (CanChangeDisplaySettings())
    {
        UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
        Settings->SetVSyncEnabled(!Settings->IsVSyncEnabled());
        Settings->ApplyNonResolutionSettings();
        Settings->SaveSettings();
    }
}

void AAbandonedTestController::AbandonedCycleFrameLimit()
{
    if (CanChangeDisplaySettings())
    {
        UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
        const int32* Next = Algo::FindByPredicate(FrameRateLimits, [Settings](int32 Limit)
        {
            return Limit > Settings->GetFrameRateLimit();
        });
        Settings->SetFrameRateLimit(Next ? *Next : FrameRateLimits[0]);
        Settings->ApplyNonResolutionSettings();
        Settings->SaveSettings();
    }
}

void AAbandonedTestController::ToggleFullscreen()
{
    if (!CanChangeDisplaySettings())
    {
        return;
    }
    const EWindowMode::Type Current = GetLocalPlayer()->ViewportClient->GetWindow()->GetWindowMode();
    const EWindowMode::Type Target = Current == EWindowMode::Windowed
        ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
    UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
    Settings->SetFullscreenMode(Target);
    Settings->ApplyResolutionSettings(false);
    Settings->ConfirmVideoMode();
    Settings->SaveSettings();
}

FReply AAbandonedTestController::RunMenuAction(EMenuAction Action)
{
    using namespace Abandoned::Application::Expedition;
    using Abandoned::Domain::Expedition::Segment;
    using Abandoned::Infrastructure::Expedition::TravelToWorld;
    switch (Action)
    {
    case EMenuAction::Resume:
        CloseMenu();
        break;
    case EMenuAction::TrainingChamber:
        CloseMenu();
        TravelToWorld(this, ChamberDestination{});
        break;
    case EMenuAction::Balance:
        CloseMenu();
        TravelToWorld(this, ChamberDestination{Segment::Balance});
        break;
    case EMenuAction::MovementLab:
        CloseMenu();
        TravelToWorld(this, LabDestination::Movement);
        break;
    case EMenuAction::PowerLab:
        CloseMenu();
        TravelToWorld(this, LabDestination::Optics);
        break;
    case EMenuAction::GravityLab:
        CloseMenu();
        TravelToWorld(this, LabDestination::Gravity);
        break;
    case EMenuAction::PortalLab:
        CloseMenu();
        TravelToWorld(this, LabDestination::Portals);
        break;
    case EMenuAction::Dev20:
        CloseMenu();
        Abandoned::Infrastructure::Expedition::ReturnToDev20Station(this);
        break;
    case EMenuAction::Radiation:
        CloseMenu();
        TravelToWorld(this, VanguardDestination{VanguardStop::RadiationPassage});
        break;
    case EMenuAction::Fullscreen:
        ToggleFullscreen();
        break;
    case EMenuAction::VSync:
        AbandonedToggleVSync();
        break;
    case EMenuAction::FrameRate:
        AbandonedCycleFrameLimit();
        break;
    case EMenuAction::Quit:
        CloseMenu();
        UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
        break;
    }
    return FReply::Handled();
}
