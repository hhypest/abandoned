#pragma once

#include "CoreMinimal.h"
#include "SceneLayout.generated.h"

class UMaterialInterface;

USTRUCT()
struct FSceneBlock
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Center = FVector::ZeroVector;

    UPROPERTY()
    FVector Size = FVector::ZeroVector;

    UPROPERTY()
    bool bCollision = true;

    UPROPERTY()
    FName Finish = NAME_None;

    bool IsValid() const
    {
        return !Center.ContainsNaN() && !Size.ContainsNaN() && Size.GetMin() > 0.0;
    }
};

USTRUCT()
struct FSceneFinish
{
    GENERATED_BODY()

    UPROPERTY()
    FName Name = NAME_None;

    UPROPERTY()
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY()
    float Roughness = 0.5f;

    UPROPERTY()
    float Metallic = 0.0f;

    UPROPERTY()
    float Glow = 0.0f;

    UPROPERTY()
    TSoftObjectPtr<UMaterialInterface> Material;

    bool IsValid() const
    {
        return !Name.IsNone() && FMath::IsFinite(Roughness) && FMath::IsWithinInclusive(Roughness, 0.0f, 1.0f) &&
            FMath::IsFinite(Metallic) && FMath::IsWithinInclusive(Metallic, 0.0f, 1.0f) &&
            FMath::IsFinite(Glow) && Glow >= 0.0f && FMath::IsFinite(Tint.R) && FMath::IsFinite(Tint.G) &&
            FMath::IsFinite(Tint.B) && FMath::IsFinite(Tint.A);
    }
};

USTRUCT()
struct FSceneLight
{
    GENERATED_BODY()

    UPROPERTY()
    FVector Position = FVector::ZeroVector;

    UPROPERTY()
    float Intensity = 0.0f;

    UPROPERTY()
    float Radius = 0.0f;

    UPROPERTY()
    FLinearColor Color = FLinearColor::White;

    bool IsValid() const
    {
        return !Position.ContainsNaN() && FMath::IsFinite(Intensity) && Intensity >= 0.0f &&
            FMath::IsFinite(Radius) && Radius > 0.0f &&
            FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
    }
};
