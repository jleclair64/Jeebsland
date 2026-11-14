#pragma once
#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UWorld;

// TownLab's two rebuilt buildings next to the originals, on a grass yard past MoveLab's +Y test houses (2026-11-14), only with the
// test switch -TownLabBuildings: Oak Hill's house and common store as they are (/Game/Buildings, 300k triangles each) and as rebuilt
// (/Game/TownLab/Buildings: SM_TL_House and SM_TL_Store, about 45-50k with baked detail, Tools/BuildTownLabBuildings.ps1), the new
// ones with their decorations from TownLabDecor.h. Doors toward the floor; collision from each one's SM_*_Col.
//   -TownLabSet=old | new   only that pair (frame-cost comparisons); both by default
// Log JEEBTLB: each building's mesh, triangles (the non-Nanite fallback's LOD 0 too), materials, and the decor's draws.
namespace JeebTownLab
{
	void BuildLineup(UWorld* World, AActor* Owner, USceneComponent* Root, float FloorZ);
}
