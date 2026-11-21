#include "World/FosTech.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

bool Abandoned::FosTech::SetVisual(UStaticMeshComponent* Visual, const TCHAR* Path, double Height, double Yaw)
{
    return FitVisual(Visual, LoadObject<UStaticMesh>(nullptr, Path), Height, Yaw);
}

bool Abandoned::FosTech::FitVisual(UStaticMeshComponent* Visual, UStaticMesh* Mesh, double Height, double Yaw)
{
    if (!Visual) { return false; }
    if (!Mesh || Mesh->GetBounds().BoxExtent.Z <= UE_SMALL_NUMBER) { return false; }
    const auto Bounds = Mesh->GetBounds();
    const double Scale = Height / (2.0 * Bounds.BoxExtent.Z);
    const FRotator Rotation(0, Yaw, 0);
    const bool bRegisterAgain = Visual->IsRegistered() && Visual->Mobility == EComponentMobility::Static;
    if (bRegisterAgain) { Visual->UnregisterComponent(); }
    Visual->SetStaticMesh(Mesh);
    Visual->EmptyOverrideMaterials();
    Visual->SetRelativeTransform(FTransform(Rotation, Rotation.RotateVector(-Bounds.Origin * Scale), FVector(Scale)));
    if (bRegisterAgain) { Visual->RegisterComponent(); }
    return Visual->GetStaticMesh() == Mesh;
}
