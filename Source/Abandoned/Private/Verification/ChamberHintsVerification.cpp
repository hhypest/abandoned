#include "Verification/ChamberHintsVerification.h"

UChamberHintsVerification::UChamberHintsVerification()
{
#if WITH_DEV_AUTOMATION_TESTS
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"
#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogHintVerification, Log, All);

bool UChamberHintsVerification::IsRequested()
{
    return FParse::Param(FCommandLine::Get(), TEXT("VerifyChamberHints"));
}

UEntryWeightLayout* UChamberHintsVerification::CreateFixtureLayout(ATrainingChamberGameMode& Chamber)
{
    auto* Layout = NewObject<UEntryWeightLayout>(&Chamber);
    Layout->HintDelaySeconds = 2.0;
    return Layout;
}

void UChamberHintsVerification::Attach(ATrainingChamberGameMode& Chamber)
{
    if (IsRequested())
    {
        auto* Scenario = NewObject<UChamberHintsVerification>(&Chamber);
        Chamber.AddInstanceComponent(Scenario);
        Scenario->RegisterComponent();
        Scenario->AddTickPrerequisiteActor(&Chamber);
        Scenario->SetComponentTickEnabled(true);
    }
}

void UChamberHintsVerification::Press(APlayerController& Controller, const FKey& Key)
{
    InputOwner = &Controller;
    PressedKey = Key;
    Controller.InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, IE_Pressed, FPlatformTime::Cycles64()));
}

void UChamberHintsVerification::ReleaseKey()
{
    if (APlayerController* Controller = InputOwner.Get())
    {
        Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, PressedKey, IE_Released, FPlatformTime::Cycles64()));
    }
    InputOwner.Reset();
    PressedKey = FKey{};
}

void UChamberHintsVerification::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseKey();
    Super::EndPlay(Reason);
}

void UChamberHintsVerification::Fail(const TCHAR* Reason)
{
    UE_LOG(LogHintVerification, Error, TEXT("Chamber hints failed at step %d: %s."), static_cast<int32>(Phase), Reason);
    SetComponentTickEnabled(false);
    ReleaseKey();
    FPlatformMisc::RequestExitWithStatus(false, 1);
}

void UChamberHintsVerification::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaSeconds, TickType, TickFunction);
    Elapsed += DeltaSeconds;
    if (Elapsed > 12.0)
    {
        Fail(TEXT("timed out waiting for the player or hint progression"));
        return;
    }
    if (PressedKey.IsValid())
    {
        ReleaseKey();
        return;
    }

    const auto* Chamber = Cast<ATrainingChamberGameMode>(GetOwner());
    const auto* Layout = Chamber ? Chamber->GetLayout() : nullptr;
    if (!Layout || !IsValid(Chamber->GetHintTerminal()))
    {
        Fail(TEXT("missing hint terminal"));
        return;
    }
    if (!IsValid(Chamber->GetButton()))
    {
        Fail(TEXT("missing wall switch"));
        return;
    }
    auto* Controller = GetWorld()->GetFirstPlayerController();
    auto* Player = Controller ? Cast<ATrainingChamberCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player)
    {
        return;
    }
    const auto AimAt = [&](const UBoxComponent& Device, double Distance)
    {
        const FVector Center = Device.GetComponentLocation();
        const FVector From(Center.X + Distance, Center.Y, Layout->Spawn.Z);
        const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        const FRotator View = (Center - Eye).Rotation();
        Player->SetActorLocationAndRotation(From, FRotator(0, View.Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->StopMovementImmediately();
        Controller->SetControlRotation(View);
    };
    const auto& Hints = Chamber->GetHints();
    switch (Phase)
    {
    case EPhase::OpenTerminal:
        if (Elapsed < 0.25)
        {
            break;
        }
        if (Hints.AvailablePages() != 1 || Hints.Page() != 0 || Hints.HasNewEntry())
        {
            Fail(TEXT("the first page must be available at the terminal"));
            return;
        }
        AimAt(*Chamber->GetHintTerminal(), 150.0);
        Press(*Controller, EKeys::E);
        Phase = EPhase::CheckFirstPage;
        break;
    case EPhase::CheckFirstPage:
        if (Hints.Page() != 1)
        {
            Fail(TEXT("E did not open the physical hint terminal"));
            return;
        }
        Press(*Controller, EKeys::E);
        Phase = EPhase::CheckLockedPages;
        break;
    case EPhase::CheckLockedPages:
        if (Hints.Page() != 0)
        {
            Fail(TEXT("locked pages must stay hidden"));
            return;
        }
        Phase = EPhase::ArmSwitch;
        break;
    case EPhase::ArmSwitch:
        if (Elapsed >= 1.2)
        {
            AimAt(*Chamber->GetButton(), -120.0);
            Press(*Controller, EKeys::E);
            SwitchPressedAt = Elapsed;
            Phase = EPhase::WaitForSecondEntry;
        }
        break;
    case EPhase::WaitForSecondEntry:
        if (!Chamber->IsButtonLatched())
        {
            Fail(TEXT("the wall switch did not latch"));
            return;
        }
        if (Hints.AvailablePages() == 2)
        {
            if (Elapsed - SwitchPressedAt + DeltaSeconds < Layout->HintDelaySeconds || !Hints.HasNewEntry() || Hints.Page() != 0)
            {
                Fail(TEXT("first progress should delay the notice without opening a page"));
                return;
            }
            AimAt(*Chamber->GetHintTerminal(), 150.0);
            Press(*Controller, EKeys::E);
            Phase = EPhase::ReadSecondEntry;
        }
        break;
    case EPhase::ReadSecondEntry:
    case EPhase::ReadThirdEntry:
    {
        const int Target = Phase == EPhase::ReadSecondEntry ? 2 : 3;
        if (Hints.Page() != ExpectedPage)
        {
            Fail(TEXT("each press must advance exactly one terminal page"));
            return;
        }
        if (ExpectedPage < Target)
        {
            ++ExpectedPage;
            Press(*Controller, EKeys::E);
            break;
        }
        if (Hints.HasNewEntry())
        {
            Fail(TEXT("reading the unlocked page should clear the notice"));
            return;
        }
        if (Target == 2)
        {
            Controller->SetControlRotation(FRotator::ZeroRotator);
            Phase = EPhase::LookAway;
        }
        else
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("CaptureChamberHints")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("Chamber-Hints-Third-Page"), false, false);
            }
            Phase = EPhase::Reset;
        }
        break;
    }
    case EPhase::LookAway:
        if (Hints.Page() != 0)
        {
            Fail(TEXT("looking away must close the terminal"));
            return;
        }
        Phase = EPhase::WaitForThirdEntry;
        break;
    case EPhase::WaitForThirdEntry:
        if (Hints.AvailablePages() == 3)
        {
            if (!Hints.HasNewEntry() || Hints.Page() != 0)
            {
                Fail(TEXT("the third page should wait for deliberate terminal interaction"));
                return;
            }
            AimAt(*Chamber->GetHintTerminal(), 150.0);
            Press(*Controller, EKeys::E);
            ExpectedPage = 1;
            Phase = EPhase::ReadThirdEntry;
        }
        break;
    case EPhase::Reset:
        Press(*Controller, EKeys::R);
        Phase = EPhase::CheckReset;
        break;
    case EPhase::CheckReset:
        if (Hints.AvailablePages() != 1 || Hints.Page() != 0 || Hints.HasNewEntry() || Chamber->IsButtonLatched())
        {
            Fail(TEXT("reset did not restore the first hint and the original circuit"));
            return;
        }
        UE_LOG(LogHintVerification, Display, TEXT("Chamber hints verification passed: terminal, three pages, progress, notices and reset."));
        SetComponentTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}
#endif
