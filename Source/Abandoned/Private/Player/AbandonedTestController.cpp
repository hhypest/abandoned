#include "Player/AbandonedTestController.h"

#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FName EntryMap(TEXT("/Engine/Maps/Entry"));

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
                    SNew(SBorder)
                    .Padding(20.0f)
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
                            .Text(NSLOCTEXT("TestMenu", "EntryCourse", "Load Entry Course"))
                            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::TrainingChamber)
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
                            .Text_UObject(this, &AAbandonedTestController::GetFullscreenLabel)
                            .IsEnabled_UObject(this, &AAbandonedTestController::CanToggleFullscreen)
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

bool AAbandonedTestController::CanToggleFullscreen() const
{
    const ULocalPlayer* LocalPlayer = GetLocalPlayer();
    return LocalPlayer && LocalPlayer->ViewportClient && LocalPlayer->ViewportClient->GetWindow().IsValid() &&
        GetWorld() && GetWorld()->WorldType == EWorldType::Game && UGameUserSettings::GetGameUserSettings();
}

FText AAbandonedTestController::GetFullscreenLabel() const
{
    if (!CanToggleFullscreen())
    {
        return NSLOCTEXT("TestMenu", "FullscreenStandalone", "Fullscreen available in standalone");
    }
    return GetLocalPlayer()->ViewportClient->GetWindow()->GetWindowMode() == EWindowMode::Windowed
        ? NSLOCTEXT("TestMenu", "EnterFullscreen", "Fullscreen (F11)")
        : NSLOCTEXT("TestMenu", "ExitFullscreen", "Windowed (F11)");
}

void AAbandonedTestController::ToggleFullscreen()
{
    if (!CanToggleFullscreen())
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
    switch (Action)
    {
    case EMenuAction::Resume:
        CloseMenu();
        break;
    case EMenuAction::TrainingChamber:
        CloseMenu();
        UGameplayStatics::OpenLevel(this, EntryMap, true, TEXT("game=/Script/Abandoned.TrainingChamberGameMode"));
        break;
    case EMenuAction::MovementLab:
        CloseMenu();
        UGameplayStatics::OpenLevel(this, EntryMap, true, TEXT("game=/Script/Abandoned.MovementLabGameMode"));
        break;
    case EMenuAction::Fullscreen:
        ToggleFullscreen();
        break;
    case EMenuAction::Quit:
        CloseMenu();
        UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
        break;
    }
    return FReply::Handled();
}
