#include "TrainingChamber/TrainingChamber.h"
#include "Player/BeltTablet.h"
#include "Player/FosTechBody.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "EnhancedInputComponent.h"

ATrainingChamberCharacter::ATrainingChamberCharacter()
{
    Tablet = CreateDefaultSubobject<UBeltTablet>(TEXT("BeltTablet"));
    Tablet->SetupAttachment(GetRootComponent());
}

void ATrainingChamberCharacter::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    if (GetWorld()->IsGameWorld()) { Tablet->Equip(*SuitBody); }
}

void ATrainingChamberCharacter::BindAdditionalInput(UEnhancedInputComponent& Input)
{
    Super::BindAdditionalInput(Input);
    if (!HasTablet()) { return; }
    Input.BindAction(MapAction(EKeys::Tab), ETriggerEvent::Started, this, &ATrainingChamberCharacter::ToggleTablet);
    Input.BindAction(MapAction(EKeys::PageDown), ETriggerEvent::Started, Tablet.Get(), &UBeltTablet::Browse, true);
    Input.BindAction(MapAction(EKeys::PageUp), ETriggerEvent::Started, Tablet.Get(), &UBeltTablet::Browse, false);
}

bool ATrainingChamberCharacter::HasTablet() const { return Tablet->IsEquipped(); }
bool ATrainingChamberCharacter::IsReadingTablet() const { return Tablet->IsReading(); }

void ATrainingChamberCharacter::InspectBeltTablet(int32 SideView)
{
#if WITH_DEV_AUTOMATION_TESTS
    Tablet->ReportFit(*SuitBody, bIsCrouched);
    bFitSideView = SideView != 0 && HasTablet();
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
void ATrainingChamberCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& View)
{
    Super::CalcCamera(DeltaTime, View);
    if (bFitSideView)
    {
        const FVector Offset = GetActorQuat().RotateVector(FVector(76, 102, 21));
        View.Location = SuitBody->GetSocketLocation(TEXT("Hips")) + Offset;
        View.Rotation = (-Offset).Rotation();
        View.FOV = 49.f;
    }
}
#endif

void ATrainingChamberCharacter::ToggleTablet()
{
    if (!GetHeldBody()) { Tablet->Toggle(); }
}

void ATrainingChamberCharacter::Interact()
{
    if (!Tablet->InUse()) { Super::Interact(); }
}

void ATrainingChamberCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Tablet->Update(*SuitBody, FTransform(GetViewRotation(), Camera->GetComponentLocation()), DeltaSeconds);
}

void ATrainingChamberCharacter::HolsterTablet() { Tablet->Holster(*SuitBody); }

void ATrainingChamberCharacter::UnPossessed()
{
#if WITH_DEV_AUTOMATION_TESTS
    bFitSideView = false;
#endif
    HolsterTablet();
    Super::UnPossessed();
}
