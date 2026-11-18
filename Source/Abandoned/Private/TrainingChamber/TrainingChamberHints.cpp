#include "TrainingChamber/TrainingChamber.h"
#include "TrainingChamber/TrainingChamberLayout.h"

#include "Components/BoxComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

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
        const TArray<float>& PlateLoads = GetPlateLoads();
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
        for (int32 Index = 0; Index < GetPlateLoads().Num(); ++Index)
        {
            if (IsPlatePressed(Index))
            {
                CorrectNodes |= PressurePlateHintNode(Index);
            }
        }
    }
    HintReader.Observe(CorrectNodes, bReachedExit, DeltaSeconds, Layout->HintDelaySeconds);
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
