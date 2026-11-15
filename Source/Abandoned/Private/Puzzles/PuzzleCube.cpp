#include "Puzzles/PuzzleCube.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

APuzzleCube::APuzzleCube()
{
    Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
    SetRootComponent(Body);
    Body->SetMobility(EComponentMobility::Movable);
    Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Body->SetLinearDamping(0.5f);
    Body->SetAngularDamping(2.0f);
    Body->SetUseCCD(true);

    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Visual->SetupAttachment(Body);
    Visual->SetCollisionProfileName(TEXT("NoCollision"));
}

bool APuzzleCube::IsValidType(EPuzzleCubeType Type)
{
    return Type == EPuzzleCubeType::Small || Type == EPuzzleCubeType::Large;
}

void APuzzleCube::Initialize(EPuzzleCubeType Type, UStaticMesh& Mesh)
{
    check(IsValidType(Type));
    const bool bSmall = Type == EPuzzleCubeType::Small;
    const double Size = bSmall ? 55.0 : 80.0;
    MassKg = bSmall ? 5.0f : 10.0f;
    Body->SetBoxExtent(FVector(Size * 0.5));
    Body->SetMassOverrideInKg(NAME_None, MassKg, true);
    Visual->SetStaticMesh(&Mesh);
    Visual->SetRelativeScale3D(FVector(Size / 100.0));
    Body->SetSimulatePhysics(true);
}

double APuzzleCube::GetHalfSize() const
{
    return Body->GetUnscaledBoxExtent().Z;
}

float APuzzleCube::GetStackMass() const
{
    float TotalMass = MassKg;
    for (const APuzzleCube* Above = StackedCube.Get(); Above; Above = Above->StackedCube.Get())
    {
        TotalMass += Above->MassKg;
    }
    return TotalMass;
}

bool APuzzleCube::CanSupportCube() const
{
    if (StackedCube.IsValid() || GetActorUpVector().Z < 0.995 ||
        Body->GetPhysicsLinearVelocity().SizeSquared() > FMath::Square(25.0) ||
        Body->GetPhysicsAngularVelocityInDegrees().SizeSquared() > FMath::Square(10.0))
    {
        return false;
    }
    if (Support.IsValid())
    {
        return true;
    }

    const FVector Center = GetActorLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(CubeSupport), false, this);
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Center,
        Center - FVector(0.0, 0.0, GetHalfSize() + 4.0), ECC_Visibility, Query))
    {
        return false;
    }
    const UPrimitiveComponent* Floor = Hit.GetComponent();
    return IsValid(Floor) && Floor->GetCollisionObjectType() == ECC_WorldStatic && Hit.ImpactNormal.Z > 0.9;
}

bool APuzzleCube::SnapTo(UBoxComponent& NewSupport, const FVector& Center, const FRotator& Rotation)
{
    APuzzleCube* LowerCube = Cast<APuzzleCube>(NewSupport.GetOwner());
    if (&NewSupport == Body || Support.IsValid() || StackedCube.IsValid() ||
        (LowerCube && !LowerCube->CanSupportCube()))
    {
        return false;
    }

    Body->SetSimulatePhysics(false);
    SetActorLocationAndRotation(Center, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
    if (!AttachToComponent(&NewSupport, FAttachmentTransformRules::KeepWorldTransform))
    {
        SetFreeBody();
        return false;
    }
    Support = &NewSupport;
    if (AActor* SupportOwner = NewSupport.GetOwner())
    {
        SupportOwner->OnEndPlay.AddUniqueDynamic(this, &APuzzleCube::OnSupportEndPlay);
    }
    if (LowerCube)
    {
        LowerCube->StackedCube = this;
        LowerCube->Body->SetSimulatePhysics(false);
    }
    return true;
}

void APuzzleCube::SetFreeBody()
{
    Body->SetSimulatePhysics(true);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    Body->WakeRigidBody();
}

void APuzzleCube::ReleaseSupport()
{
    UBoxComponent* Previous = Support.Get();
    Support.Reset();
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    AActor* SupportOwner = Previous ? Previous->GetOwner() : nullptr;
    if (SupportOwner)
    {
        SupportOwner->OnEndPlay.RemoveDynamic(this, &APuzzleCube::OnSupportEndPlay);
    }
    if (APuzzleCube* LowerCube = Cast<APuzzleCube>(SupportOwner))
    {
        LowerCube->StackedCube.Reset();
        if (!LowerCube->Support.IsValid())
        {
            LowerCube->SetFreeBody();
        }
    }
}

void APuzzleCube::ReleaseStack()
{
    if (APuzzleCube* Above = StackedCube.Get())
    {
        Above->ReleaseStack();
        Above->ReleaseSupport();
        Above->SetFreeBody();
    }
    StackedCube.Reset();
}

void APuzzleCube::DetachForPickup()
{
    ReleaseStack();
    ReleaseSupport();
    SetFreeBody();
}

void APuzzleCube::ResetPosition(const FVector& Center)
{
    DetachForPickup();
    SetActorLocationAndRotation(Center, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
}

void APuzzleCube::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Reason == EEndPlayReason::Destroyed || Reason == EEndPlayReason::RemovedFromWorld)
    {
        ReleaseStack();
        ReleaseSupport();
    }
    Super::EndPlay(Reason);
}

void APuzzleCube::OnSupportEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
    if (Reason == EEndPlayReason::Destroyed || Reason == EEndPlayReason::RemovedFromWorld)
    {
        DetachForPickup();
    }
}
