#pragma once

#include "Application/Expedition/SuitTerminal.h"
#include "Components/SceneComponent.h"
#include "BeltTablet.generated.h"

class UFosTechBody;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;

UCLASS(Config=Game)
class UBeltTablet final : public USceneComponent
{
    GENERATED_BODY()
public:
    bool HasCasingAsset() const;
    void Equip(UFosTechBody& Body);
    bool IsEquipped() const { return Casing != nullptr; }
    bool InUse() const { return bRaised || Travel > 0.f; }
    float RaisedFraction() const { return Travel; }
    bool IsReading() const { return bShowing; }
    void Toggle();
    void Browse(bool Forward);
    void Holster(UFosTechBody& Body);
    void Update(UFosTechBody& Body, const FTransform& Eye, float DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
    bool IsOnBelt(const UFosTechBody& Body) const { return IsEquipped() && !InUse() && GetComponentTransform().Equals(BeltFrame(Body), 0.001); }
    void ReportFit(const UFosTechBody& Body, bool bCrouched) const;
#endif

private:
    FTransform BeltFrame(const UFosTechBody& Body) const;
    void DrawArchive();

    UPROPERTY(Config)
    TSoftObjectPtr<UStaticMesh> TabletAsset;
    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> Casing;
    UPROPERTY(Transient)
    TObjectPtr<UWidgetComponent> Field;
    FTransform BeltInHip;
    Abandoned::Application::Expedition::SuitTerminal Reader;
    Abandoned::Domain::Expedition::SuitArchive Records;
    bool bRaised = false;
    bool bShowing = false;
    float Travel = 0.f;
};
