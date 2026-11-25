#include "Player/BeltTablet.h"

#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Expedition/VanguardSession.h"
#include "Expedition/VanguardRouteLayout.h"
#include "Materials/MaterialInterface.h"
#include "Infrastructure/Expedition/UnrealEntryCourse.h"
#include "Misc/PackageName.h"
#include "Player/FosTechBody.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
using Abandoned::Domain::Expedition::SuitRecord;
constexpr double Width = 24.0;
const FLinearColor Cyan(0.34f, 0.93f, 1.f);
struct FEntry { const TCHAR* Title; const TCHAR* Source; const TCHAR* Text; };
FEntry Entry(SuitRecord Id)
{
    switch (Id)
    {
    case SuitRecord::ForecastSynchronized:
        return {TEXT("Прогноз вспышек"), TEXT("Наружный дозиметр"), TEXT("Прогноз получен. Когда начнётся вспышка, укройтесь под перекрытием.")};
    case SuitRecord::Decontaminated:
        return {TEXT("Дезактивация"), TEXT("Шлюз станции"), TEXT("Цикл очистки завершён. Остаточная доза сброшена.")};
    case SuitRecord::OxygenRestored:
        return {TEXT("Заправка баллона"), TEXT("Авангард · пост снабжения"), TEXT("Подача кислорода завершена. Давление в баллоне восстановлено.")};
    }
    return {};
}

TSharedRef<STextBlock> Label(const FString& Text, int32 Size, bool Bold = false, FLinearColor Ink = FLinearColor::White)
{
    return SNew(STextBlock).Text(FText::FromString(Text)).Font(FCoreStyle::GetDefaultFontStyle(Bold ? "Bold" : "Regular", Size))
        .ColorAndOpacity(Ink).AutoWrapText(true);
}

void SetupVisual(UPrimitiveComponent& Part)
{
    Part.SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part.SetCanEverAffectNavigation(false);
    Part.SetCastShadow(false);
    Part.SetOnlyOwnerSee(true);
    Part.SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
}
}

bool UBeltTablet::HasCasingAsset() const
{
    return !TabletAsset.IsNull() && FPackageName::DoesPackageExist(TabletAsset.ToSoftObjectPath().GetLongPackageName());
}

void UBeltTablet::Equip(UFosTechBody& Body)
{
    if (!Body.IsCustomManny() || !Body.HasCubeGripPose()) { return; }
    if (!HasCasingAsset()) { return; }
    UStaticMesh* Mesh = TabletAsset.LoadSynchronous();
    if (!Mesh || Mesh->GetBounds().BoxExtent.Z <= 0) { return; }
    Casing = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("RuggedTabletCasing"));
    Casing->SetupAttachment(this);
    SetupVisual(*Casing);
    Casing->SetStaticMesh(Mesh);
    const double Scale = Width / (2.0 * Mesh->GetBounds().BoxExtent.Z);
    // FBX local +Y is the display normal. Rotate its long Z axis into the landscape frame.
    const FQuat Landscape = FRotationMatrix::MakeFromXY(FVector::UpVector, FVector::ForwardVector).ToQuat();
    Casing->SetRelativeTransform(FTransform(Landscape, -Landscape.RotateVector(Mesh->GetBounds().Origin * Scale), FVector(Scale)));
    Casing->RegisterComponent();

    Field = NewObject<UWidgetComponent>(GetOwner(), TEXT("TabletArchiveField"));
    Field->SetupAttachment(this);
    SetupVisual(*Field);
    Field->SetWidgetSpace(EWidgetSpace::World);
    Field->SetDrawSize(FVector2D(960, 428));
    Field->SetRelativeScale3D(FVector(0.0525));
    Field->SetRelativeLocation(FVector(5, 0, 18.5));
    Field->SetRelativeRotation(FRotator(-12, 0, 0));
    Field->SetBlendMode(EWidgetBlendMode::Transparent);
    Field->SetWindowFocusable(false);
    Field->SetManuallyRedraw(true);
    Field->SetVisibility(false);
    if (UMaterialInterface* Light = GetDefault<UVanguardMantleVisuals>()->WristSurface.LoadSynchronous())
    {
        Field->SetMaterial(0, Light);
    }
    Field->RegisterComponent();

    const FTransform Hip = Body.GetSocketTransform(TEXT("Hips"));
    const FQuat Wearer = GetOwner()->GetActorQuat();
    const FVector Clip = Hip.GetLocation() + Wearer.RotateVector(FVector(3, 18, 8));
    const FQuat Facing = Wearer * FRotator(4, 78, 81).Quaternion();
    BeltInHip = FTransform(Facing, Clip).GetRelativeTransform(Hip);
    SetWorldTransform(BeltFrame(Body));
    const auto Saved = Abandoned::Infrastructure::Expedition::UnrealCheckpointStore{}.Read();
    if (Saved.Issue == Abandoned::Application::Expedition::CheckpointIssue::None) { Records = Saved.Archive; }
}

FTransform UBeltTablet::BeltFrame(const UFosTechBody& Body) const
{
    FTransform Frame = BeltInHip * Body.GetSocketTransform(TEXT("Hips"));
    Frame.SetScale3D(FVector::OneVector);
    return Frame;
}

void UBeltTablet::Toggle()
{
    if (!IsEquipped()) { return; }
    bRaised = !bRaised;
    if (bRaised)
    {
        Reader.OpenArchive();
        if (const UVanguardSession* Session = UVanguardSession::Find(*this))
        {
            const auto& Live = Session->Suit.Records();
            for (std::size_t I = 0; I < Live.Size(); ++I) { Records.Reveal(*Live.At(I)); }
        }
    }
    else { Reader.Close(); }
}

void UBeltTablet::Browse(bool Forward)
{
    if (!bShowing || !bRaised) { return; }
    Reader.MoveArchive(Forward, Records);
    DrawArchive();
}

void UBeltTablet::Holster(UFosTechBody& Body)
{
    if (!IsEquipped()) { return; }
    if (InUse()) { Body.PoseTablet(GetComponentTransform(), Width / 2, 0.f); }
    bRaised = bShowing = false;
    Travel = 0.f;
    Reader.Close();
    Field->SetVisibility(false);
    SetWorldTransform(BeltFrame(Body));
}

void UBeltTablet::Update(UFosTechBody& Body, const FTransform& Eye, float DeltaSeconds)
{
    if (!IsEquipped()) { return; }
    const bool bPose = InUse();
    Travel = FMath::FInterpConstantTo(Travel, bRaised ? 1.f : 0.f, DeltaSeconds, 1.65f);
    const float Lift = FMath::SmoothStep(0.25f, 1.f, Travel);
    const FTransform Belt = BeltFrame(Body);
    const FTransform Read(Eye.GetRotation() * FRotator(12, 180, 0).Quaternion(), Eye.TransformPosition(FVector(36, 0, -15)));
    FTransform Frame;
    Frame.Blend(Belt, Read, Lift);
    Frame.AddToTranslation(GetOwner()->GetActorForwardVector() * (12.0 * FMath::Sin(PI * Lift)));
    SetWorldTransform(Frame);
    if (bPose) { Body.PoseTablet(Frame, Width / 2, FMath::SmoothStep(0.f, 0.30f, Travel)); }
    if (bPose && Travel == 0.f) { SetWorldTransform(BeltFrame(Body)); }
    const bool bProject = bRaised && Travel >= 0.98f;
    if (bProject != bShowing)
    {
        bShowing = bProject;
        if (bShowing) { DrawArchive(); }
        Field->SetVisibility(bShowing);
        Field->RequestRenderUpdate();
    }
}

void UBeltTablet::DrawArchive()
{
    UVanguardSession* Session = UVanguardSession::Find(*this);
    auto Chosen = Records.At(Reader.ArchiveIndex());
    if (Chosen && Session) { Session->Suit.MarkRead(*Chosen); }
    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    for (std::size_t I = 0; I < Records.Size(); ++I)
    {
        const SuitRecord Id = *Records.At(I);
        const bool Selected = I == Reader.ArchiveIndex();
        List->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Label(Entry(Id).Title, 22, Selected)];
        const TCHAR* State = Session && Session->Suit.WasRead(Id) ? TEXT("Прочитано") : TEXT("Новая запись");
        List->AddSlot().AutoHeight()[Label(State, 16, false, Cyan)];
    }
    const FEntry Detail = Chosen ? Entry(*Chosen) : FEntry{TEXT("Записей пока нет"), TEXT("Журнал экспедиции"), TEXT("Полученные сообщения появятся здесь.")};
    const auto White = FCoreStyle::Get().GetBrush("WhiteBrush");
    Field->SetSlateWidget(SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(.08f,.38f,.44f,.55f)).Padding(2)
        [SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor(0.002f, 0.006f, 0.010f, 0.96f)).Padding(FMargin(28,18))
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("ФОСТЕХ   /   АРХИВ ЭКСПЕДИЦИИ"), 27, true, Cyan)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,10)[SNew(SSeparator)]
        + SVerticalBox::Slot().FillHeight(1.f)[SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(.35f).Padding(0,0,18,0)[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("Записи  ·  %d"), static_cast<int32>(Records.Size())), 23, true)]
                + SVerticalBox::Slot().FillHeight(1.f)[List]]
            + SHorizontalBox::Slot().AutoWidth()[SNew(SSeparator).Orientation(Orient_Vertical)]
            + SHorizontalBox::Slot().FillWidth(.65f).Padding(24,0,0,0)[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Label(Detail.Source, 21, false, Cyan)]
                + SVerticalBox::Slot().AutoHeight().Padding(0,8)[Label(Detail.Title, 33, true)]
                + SVerticalBox::Slot().AutoHeight()[Label(Detail.Text, 25)]]]
        + SVerticalBox::Slot().AutoHeight()[Label(Records.Size() > 1 ? TEXT("PgUp / PgDn   Перелистать                 Tab   Убрать КПК") : TEXT("Tab   Убрать КПК"), 19)]
        ]]);
    Field->RequestRenderUpdate();
}

#if WITH_DEV_AUTOMATION_TESTS
void UBeltTablet::ReportFit(const UFosTechBody& Body, bool bCrouched) const
{
    if (!IsEquipped()) { UE_LOG(LogTemp, Error, TEXT("Tablet fit: no casing")); return; }
    const FTransform Frame = GetComponentTransform();
    const FTransform Belt = BeltFrame(Body);
    UE_LOG(LogTemp, Display, TEXT("Tablet fit: crouched=%d raised=%d field=%d travel=%.2f belt_cm=%.3f belt_deg=%.3f records=%d index=%d"),
        bCrouched, bRaised, Field->IsVisible(), Travel, FVector::Distance(Frame.GetLocation(), Belt.GetLocation()),
        FMath::RadiansToDegrees(Frame.GetRotation().AngularDistance(Belt.GetRotation())),
        static_cast<int32>(Records.Size()), static_cast<int32>(Reader.ArchiveIndex()));
    if (!bShowing) { return; }
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FVector Pad = Frame.InverseTransformPosition(Body.MannyPalmContact(Side));
        UE_LOG(LogTemp, Display, TEXT("Tablet grip: side=%d pad_cm=(%.2f,%.2f,%.2f) edge_cm=%.2f"),
            Side, Pad.X, Pad.Y, Pad.Z, (Side == 0 ? Pad.Y : -Pad.Y) - Width / 2);
    }
}
#endif
