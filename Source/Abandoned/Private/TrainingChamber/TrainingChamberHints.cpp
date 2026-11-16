#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"

#include "Components/BoxComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MovementProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogHintVerification, Log, All);
#endif

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
constexpr double VerificationHintDelay = 2.0;
#endif
}

void ATrainingChamberGameMode::ObserveHints(const ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    if (!IsValid(HintTerminal))
    {
        return;
    }
    using namespace Abandoned::Domain::Puzzles;
    HintNodes CorrectNodes = 0;
    if (Layout->UsesPlateBalance())
    {
        const float Target = Layout->BalanceTotalKg / Layout->GetPlateCount();
        for (int32 Index = 0; Index < PlateLoads.Num(); ++Index)
        {
            if (FMath::IsNearlyEqual(PlateLoads[Index], Target, 0.01f))
            {
                CorrectNodes |= PressurePlateHintNode(Index);
            }
        }
    }
    else if (Layout->bRequiresStack)
    {
        if (Cubes.ContainsByPredicate([](const APuzzleCube* Cube)
            { return IsValid(Cube) && Cube->GetStackMass() > Cube->GetMassKg(); }))
        {
            CorrectNodes |= BallastStackHintNode;
        }
    }
    else
    {
        CorrectNodes = IsButtonLatched() ? ArmedButtonHintNode : 0;
        for (int32 Index = 0; Index < Plates.Num(); ++Index)
        {
            if (IsPlatePressed(Index))
            {
                CorrectNodes |= PressurePlateHintNode(Index);
            }
        }
    }
    double Delay = Layout->HintDelaySeconds;
#if WITH_DEV_AUTOMATION_TESTS
    if (bVerifyHints)
    {
        Delay = VerificationHintDelay;
    }
#endif
    HintReader.Observe(CorrectNodes, bReachedExit, DeltaSeconds, Delay);
    if (HintReader.Page() != 0 && !Player.CanReadHintTerminal())
    {
        HintReader.Close();
    }
}

void ATrainingChamberHUD::DrawHintPage(const UTrainingChamberLayout& Layout, int Page, int AvailablePages)
{
    if (!Layout.HintPages.IsValidIndex(Page - 1) || Canvas->ClipX < 100.0f)
    {
        return;
    }
    UFont* Font = GEngine->GetSmallFont();
    const float Width = FMath::Min(520.0f, Canvas->ClipX - 48.0f);
    FTextSizingParameters Size(0.0f, 0.0f, Width - 24.0f, 0.0f, Font);
    TArray<FWrappedStringElement> Lines;
    Canvas->WrapString(Size, 0.0f, Layout.HintPages[Page - 1], Lines);
    const float Height = 70.0f + 18.0f * Lines.Num();
    const float X = 24.0f;
    const float Y = Canvas->ClipY - Height - 24.0f;
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.82f), X, Y, Width, Height);
    DrawText(FString::Printf(TEXT("HINT TERMINAL  /  %d of %d available"), Page, AvailablePages),
        FLinearColor::White, X + 12.0f, Y + 10.0f, Font);
    float LineY = Y + 34.0f;
    for (const auto& Line : Lines)
    {
        DrawText(Line.Value, FLinearColor::White, X + 12.0f, LineY, Font);
        LineY += 18.0f;
    }
    DrawText(Page < AvailablePages ? TEXT("[E] Next entry") : TEXT("[E] Close terminal"),
        FLinearColor::White, X + 12.0f, LineY + 8.0f, Font);
}

#if WITH_DEV_AUTOMATION_TESTS
void ATrainingChamberGameMode::VerifyHints(ATrainingChamberCharacter& Player, float DeltaSeconds)
{
    HintTestSeconds += DeltaSeconds;
    const auto Fail = [this](const TCHAR* Why)
    {
        UE_LOG(LogHintVerification, Error, TEXT("Chamber hints failed at step %d: %s."), HintTestStep, Why);
        bVerifyHints = false;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    };
    auto* PC = Cast<APlayerController>(Player.GetController());
    if (HintTestSeconds > 12.0 || !PC || !IsValid(HintTerminal) || !IsValid(Button))
    {
        Fail(TEXT("timeout or missing devices"));
        return;
    }
    const auto AimAt = [this, &Player](const UBoxComponent& Device, double Distance)
    {
        const FVector Center = Device.GetComponentLocation();
        const FVector From(Center.X + Distance, Center.Y, Layout->Spawn.Z);
        const FVector Eye = From + FVector(0, 0, Abandoned::MovementProfile::CameraHeight);
        Player.PlaceAt(FTransform((Center - Eye).Rotation(), From));
    };
    const auto E = [PC](EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, EKeys::E, Event, FPlatformTime::Cycles64()));
    };
    switch (HintTestStep)
    {
    case 0:
        if (HintTestSeconds >= 0.25)
        {
            if (HintReader.AvailablePages() != 1 || HintReader.Page() != 0 || HintReader.HasNewEntry())
            {
                Fail(TEXT("the first page must be available at the terminal"));
                return;
            }
            AimAt(*HintTerminal, 150.0);
            E(IE_Pressed);
            ++HintTestStep;
        }
        break;
    case 1:
        E(IE_Released);
        if (HintReader.Page() != 1)
        {
            Fail(TEXT("E did not open the physical hint terminal"));
            return;
        }
        Player.Interact();
        if (HintReader.Page() != 0)
        {
            Fail(TEXT("locked pages must stay hidden"));
            return;
        }
        ++HintTestStep;
        break;
    case 2:
        if (HintTestSeconds >= 1.2)
        {
            AimAt(*Button, -120.0);
            Player.Interact();
            if (!IsButtonLatched())
            {
                Fail(TEXT("the wall switch did not latch"));
                return;
            }
            ++HintTestStep;
        }
        break;
    case 3:
        if (HintReader.AvailablePages() == 2)
        {
            if (HintTestSeconds < 3.15 || !HintReader.HasNewEntry() || HintReader.Page() != 0)
            {
                Fail(TEXT("first progress should delay the notice without opening a page"));
                return;
            }
            AimAt(*HintTerminal, 150.0);
            Player.Interact();
            Player.Interact();
            if (HintReader.Page() != 2 || HintReader.HasNewEntry())
            {
                Fail(TEXT("reading the unlocked second page should clear the notice"));
                return;
            }
            UE_LOG(LogHintVerification, Display, TEXT("Chamber hints: first node progress restarted the delay and the terminal offered page two."));
            PC->SetControlRotation(FRotator::ZeroRotator);
            ++HintTestStep;
        }
        break;
    case 4:
        if (HintReader.AvailablePages() == 3)
        {
            if (!HintReader.HasNewEntry() || HintReader.Page() != 0)
            {
                Fail(TEXT("the third page should wait for deliberate terminal interaction"));
                return;
            }
            AimAt(*HintTerminal, 150.0);
            Player.Interact();
            Player.Interact();
            Player.Interact();
            if (HintReader.Page() != 3 || HintReader.HasNewEntry())
            {
                Fail(TEXT("the third page could not be read"));
                return;
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("CaptureChamberHints")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("Chamber-Hints-Third-Page"), false, false);
            }
            ++HintTestStep;
        }
        break;
    case 5:
        ResetChamber(Player);
        if (HintReader.AvailablePages() != 1 || HintReader.Page() != 0 || HintReader.HasNewEntry() || IsButtonLatched())
        {
            Fail(TEXT("reset did not restore the first hint and the original circuit"));
            return;
        }
        UE_LOG(LogHintVerification, Display, TEXT("Chamber hints verification passed: terminal, three pages, progress, notices and reset."));
        bVerifyHints = false;
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}
#endif
