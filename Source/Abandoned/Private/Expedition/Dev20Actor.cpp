#include "Expedition/Dev20Actor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Domain/Expedition/Dev20.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ADev20Actor::ADev20Actor()
{
    Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
    SetRootComponent(Body);
    Body->SetMobility(EComponentMobility::Movable);
    Body->InitBoxExtent(FVector(29, 23, 19));
    Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Body->SetUseCCD(true);
    Body->SetLinearDamping(0.5f);
    Body->SetAngularDamping(2.0f);
    static const ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static const ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Game/Abandoned/Materials/M_AbandonedSurface.M_AbandonedSurface"));
    Finish = Material.Object;
    const auto AddPart = [&](FName Name, FVector Offset, FVector Size)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(Body);
        Part->SetStaticMesh(Mesh.Object);
        Part->SetRelativeLocation(Offset);
        Part->SetRelativeScale3D(Size / 100.0);
        Part->SetCollisionProfileName(TEXT("NoCollision"));
        Parts.Add(Part);
    };
    AddPart(TEXT("Housing"), FVector::ZeroVector, FVector(53, 38, 32));
    AddPart(TEXT("HandleLeft"), FVector(0, -21, 11), FVector(39, 4, 5));
    AddPart(TEXT("HandleRight"), FVector(0, 21, 11), FVector(39, 4, 5));
    AddPart(TEXT("ChargeBar"), FVector(-2, 0, 17), FVector(40, 9, 4));

    auto* Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Serial"));
    Label->SetupAttachment(Body);
    Label->SetRelativeLocation(FVector(-26.9, 0, 3));
    Label->SetRelativeRotation(FRotator(0, 180, 0));
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(8.0f);
    Label->SetText(FText::FromString(UTF8_TO_TCHAR(Abandoned::Domain::Expedition::Dev20Id.data())));
}

void ADev20Actor::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    Body->SetMassOverrideInKg(NAME_None, Abandoned::Domain::Expedition::Dev20MassKg);
    Body->SetSimulatePhysics(true);
}

void ADev20Actor::BeginPlay()
{
    Super::BeginPlay();
    if (!Finish)
    {
        return;
    }
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        UMaterialInstanceDynamic* Material = Parts[Index]->CreateDynamicMaterialInstance(0, Finish);
        if (Material)
        {
            const bool bChargeBar = Index == Parts.Num() - 1;
            Material->SetVectorParameterValue(TEXT("DiffuseColor"), bChargeBar ? FLinearColor(0.95, 0.39, 0.045)
                : FLinearColor(0.12, 0.16, 0.19));
            Material->SetScalarParameterValue(TEXT("Roughness"), 0.5f);
            Material->SetScalarParameterValue(TEXT("Metallic"), 0.35f);
            Material->SetScalarParameterValue(TEXT("Glow"), bChargeBar ? 0.8f : 0.0f);
        }
    }
}

void ADev20Actor::RestoreAt(FVector Location)
{
    SetActorLocationAndRotation(Location, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
    Body->WakeRigidBody();
}
