#include "Player/AbandonedTestController.h"

#include "Algo/Find.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "Expedition/Dev20Actor.h"
#include "Player/PuzzleCharacter.h"
#include "Infrastructure/Expedition/UnrealDev20Transfer.h"
#include "Widgets/SOverlay.h"
#include "Sound/SoundBase.h"
#include "Misc/PackageName.h"
#include "Kismet/GameplayStatics.h"
#if !UE_BUILD_SHIPPING
#include "Infrastructure/Expedition/UnrealWorldTravel.h"
#endif
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

class SPauseMenu final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPauseMenu) {}
        SLATE_EVENT(FSimpleDelegate, OnClose)
        SLATE_EVENT(FSimpleDelegate, OnFullscreen)
        SLATE_ARGUMENT(FKey, ToggleKey)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        ToggleKey = Args._ToggleKey;
        OnClose = Args._OnClose;
        OnFullscreen = Args._OnFullscreen;
        ChildSlot[Args._Content.Widget];
    }

    bool SupportsKeyboardFocus() const override { return true; }

    FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        const FKey Key = Event.GetKey();
        if (Key == EKeys::Escape || Key == ToggleKey || Key == EKeys::F11)
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
    FKey ToggleKey;
    FSimpleDelegate OnClose;
    FSimpleDelegate OnFullscreen;
};
}

enum class AAbandonedTestController::EMenuAction
{
    Resume, Fullscreen, VSync, FrameRate, Quit,
#if !UE_BUILD_SHIPPING
    TrainingChamber, Balance, MovementLab, PowerLab, GravityLab, PortalLab, Dev20, Radiation
#endif
};

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
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AAbandonedTestController::OpenPauseMenu);
#if !UE_BUILD_SHIPPING
    InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AAbandonedTestController::OpenTestMenu);
#endif
    InputComponent->BindKey(EKeys::F11, IE_Pressed, this, &AAbandonedTestController::ToggleFullscreen);
}

void AAbandonedTestController::OpenPauseMenu()
{
    OpenMenu(NSLOCTEXT("PauseMenu", "Title", "Paused"), EKeys::Escape, SNullWidget::NullWidget);
}

#if !UE_BUILD_SHIPPING
void AAbandonedTestController::OpenTestMenu()
{
    struct FDestinationButton
    {
        EMenuAction Action;
        FText Label;
    };
    const FDestinationButton Destinations[] = {
        {EMenuAction::TrainingChamber, NSLOCTEXT("TestMenu", "EntryCourse", "Load Entry Course")},
        {EMenuAction::Balance, NSLOCTEXT("TestMenu", "Balance", "Load Balance Chamber")},
        {EMenuAction::MovementLab, NSLOCTEXT("TestMenu", "Lab", "Load Movement Lab")},
        {EMenuAction::PowerLab, NSLOCTEXT("TestMenu", "Optics", "Load Optics Lab")},
        {EMenuAction::GravityLab, NSLOCTEXT("TestMenu", "Gravity", "Load Gravity Lab")},
        {EMenuAction::PortalLab, NSLOCTEXT("TestMenu", "PortalLab", "Load Portal Lab")},
        {EMenuAction::Dev20, NSLOCTEXT("TestMenu", "Dev20", "Load DEV-20 Route")},
        {EMenuAction::Radiation, NSLOCTEXT("TestMenu", "Radiation", "Load Radiation Passage")}
    };
    const TSharedRef<SVerticalBox> Buttons = SNew(SVerticalBox);
    for (const FDestinationButton& Destination : Destinations)
    {
        Buttons->AddSlot().AutoHeight().Padding(0.0f, 3.0f)
        [
            SNew(SButton)
            .Text(Destination.Label)
            .OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, Destination.Action)
        ];
    }
    OpenMenu(NSLOCTEXT("TestMenu", "Title", "Test Menu"), EKeys::M, Buttons);
}
#endif

void AAbandonedTestController::OpenMenu(const FText& Title, FKey ToggleKey, const TSharedRef<SWidget>& Destinations)
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
    Menu = SNew(SPauseMenu)
        .ToggleKey(ToggleKey)
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
                            .Text(Title)
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
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            Destinations
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
    PresentMenu(ResumeButton.ToSharedRef());
}

void AAbandonedTestController::PresentMenu(const TSharedRef<SWidget>& Focus)
{
    ULocalPlayer* LocalPlayer = GetLocalPlayer();
    LocalPlayer->ViewportClient->AddViewportWidgetForPlayer(LocalPlayer, Menu.ToSharedRef(), 10);
    FInputModeUIOnly Input;
    Input.SetWidgetToFocus(Focus).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Input);
    bShowMouseCursor = true;
}

void AAbandonedTestController::OfferDev20Discard(ADev20Actor& Unit)
{
    ULocalPlayer* Local = GetLocalPlayer();
    if (Menu || !Local || !Local->ViewportClient || !SetPause(true)) { return; }
    DiscardCandidate = &Unit;
    TSharedPtr<SButton> Keep;
    Menu = SNew(SPauseMenu)
        .OnClose(FSimpleDelegate::CreateUObject(this, &AAbandonedTestController::CloseMenu))
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.012f, 0.019f, 0.022f, 0.96f))
            .HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
                [ SNew(STextBlock).Text(INVTEXT("Выбросить DEV-20?")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 27)) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 24)
                [ SNew(STextBlock).Text(INVTEXT("Без силового узла эвакуация невозможна. История героя закончится.")).WrapTextAt(460) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SAssignNew(Keep, SButton).Text(INVTEXT("Оставить в руках")).OnClicked_UObject(this, &AAbandonedTestController::RunMenuAction, EMenuAction::Resume) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [ SNew(SButton).Text(INVTEXT("Выбросить навсегда")).OnClicked_UObject(this, &AAbandonedTestController::ConfirmDev20Discard) ]
            ]
        ];
    PresentMenu(Keep.ToSharedRef());
}

FReply AAbandonedTestController::ConfirmDev20Discard()
{
    APuzzleCharacter* Carrier = Cast<APuzzleCharacter>(GetPawn());
    ADev20Actor* Unit = DiscardCandidate.Get();
    auto* Session = Abandoned::Infrastructure::Expedition::Dev20Session(this);
    if (Session && Session->Transfer.EvacuationLost()) { return FReply::Handled(); }
    if (!Session || !Unit || !Carrier || !Session->Transfer.AbandonHeldUnit(Carrier->GetHeldDev20() == Unit))
    {
        CloseMenu();
        return FReply::Handled();
    }

    Carrier->ReleaseHeldObject();
    Unit->SetActorHiddenInGame(true);
    Unit->SetActorEnableCollision(false);
    RemoveMenu();
    const TCHAR* Cue = TEXT("/Game/AbandonedLocal/Dev20Ending/S_Dev20Ending");
    USoundBase* Sound = FPackageName::DoesPackageExist(Cue) ? LoadObject<USoundBase>(nullptr, Cue) : nullptr;
    const double Start = FPlatformTime::Seconds();
    const auto Elapsed = [Start]() { return FPlatformTime::Seconds() - Start; };
    TSharedPtr<SButton> Restart;
    Menu = SNew(SBorder).Padding(36).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor::Black)
        [
            SNew(SOverlay)
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12)
                [ SNew(STextBlock).Visibility_Lambda([Elapsed]() { return Elapsed() >= 4.2 ? EVisibility::Visible : EVisibility::Hidden; })
                    .Text(INVTEXT("Эвакуация сорвана")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 28)) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [ SAssignNew(Restart, SButton).Text(INVTEXT("Вернуться на станцию")).OnClicked_UObject(this, &AAbandonedTestController::RestartDev20Route) ]
            ]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 26)
            [
                SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 21))
                .Text_Lambda([Elapsed]() {
                    const double Seconds = Elapsed();
                    if (Seconds < 0.55) { return FText::GetEmpty(); }
                    if (Seconds < 2.2) { return INVTEXT("Я не смог."); }
                    if (Seconds < 4.2) { return INVTEXT("[Одиночный выстрел]"); }
                    return FText::GetEmpty();
                })
            ]
        ];
    PresentMenu(Restart.ToSharedRef());
    if (Sound) { UGameplayStatics::PlaySound2D(this, Sound); }
    UE_LOG(LogTemp, Display, TEXT("DEV-20 deliberately discarded. Evacuation lost."));
    return FReply::Handled();
}

FReply AAbandonedTestController::RestartDev20Route()
{
    CloseMenu();
    Abandoned::Infrastructure::Expedition::ReturnToDev20Station(this, Abandoned::Application::Expedition::Dev20Entry::Recovery);
    return FReply::Handled();
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
    DiscardCandidate.Reset();
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
#if !UE_BUILD_SHIPPING
    using namespace Abandoned::Application::Expedition;
    using Abandoned::Domain::Expedition::Segment;
    using Abandoned::Infrastructure::Expedition::TravelToWorld;
#endif
    switch (Action)
    {
    case EMenuAction::Resume:
        CloseMenu();
        break;
#if !UE_BUILD_SHIPPING
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
#endif
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
