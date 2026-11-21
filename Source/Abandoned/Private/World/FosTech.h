#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UStaticMeshComponent;

namespace Abandoned::FosTech
{
inline constexpr TCHAR SmallCube[] = TEXT("/Game/Abandoned/Meshy/DEV_01A/DEV-01A/StaticMeshes/DEV-01A.DEV-01A");
inline constexpr TCHAR LargeCube[] = TEXT("/Game/Abandoned/Meshy/DEV_01B/DEV-01B/StaticMeshes/DEV-01B.DEV-01B");
inline constexpr TCHAR Button[] = TEXT("/Game/Abandoned/Meshy/DEV_03/DEV-03/StaticMeshes/DEV-03.DEV-03");
inline constexpr TCHAR Switch[] = TEXT("/Game/Abandoned/Meshy/DEV_04/DEV-04/StaticMeshes/DEV-04.DEV-04");
inline constexpr TCHAR Emitter[] = TEXT("/Game/Abandoned/Meshy/DEV_05/DEV-05/StaticMeshes/DEV-05.DEV-05");
inline constexpr TCHAR Receiver[] = TEXT("/Game/Abandoned/Meshy/DEV_06/DEV-06/StaticMeshes/DEV-06.DEV-06");
inline constexpr TCHAR Relay[] = TEXT("/Game/Abandoned/Meshy/DEV_07/DEV-07/StaticMeshes/DEV-07.DEV-07");
inline constexpr TCHAR Frame[] = TEXT("/Game/Abandoned/Meshy/DEV_12/DEV-12/StaticMeshes/DEV-12.DEV-12");
inline constexpr TCHAR Leaf[] = TEXT("/Game/Abandoned/Meshy/DEV_13/DEV-13/StaticMeshes/DEV-13.DEV-13");
inline constexpr TCHAR PlateBase[] = TEXT("/Game/Abandoned/Meshy/DEV_02/DEV-02/StaticMeshes/PlateBase.PlateBase");
inline constexpr TCHAR PlateTop[] = TEXT("/Game/Abandoned/Meshy/DEV_02/DEV-02/StaticMeshes/PlateTop.PlateTop");

// Size in centimetres along the mesh's original vertical axis. Preserve its proportions.
bool FitVisual(UStaticMeshComponent* Visual, UStaticMesh* Mesh, double Height, double Yaw = 0.0);
bool SetVisual(UStaticMeshComponent* Visual, const TCHAR* Path, double Height, double Yaw = 0.0);
}
