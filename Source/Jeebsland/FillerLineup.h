#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UWorld;

// Oak Hill's filler props (barrels, crates, sacks, carts, pots, firewood, walls, fences, lamp posts, lanterns, benches, signs, planters, rocks, bushes, logs,
// stumps, market stalls, banners, wells, torches: Art/Filler/Clean, Tools\BuildFillerLineup.ps1 -> /Game/Filler) set out in tidy labelled rows on a grass yard
// past the MoveLab floor's -Y edge, for the owner to look at in the game (2026-11-13). Not in Oak Hill yet. Nothing here is solid. /Game/Filler is never cooked,
// so a packaged game builds no yard (BuildYard skips when the pieces are missing).
namespace JeebFiller
{
	// Makes the yard under Owner (the MoveLab floor actor, its root Root, ground at world Z = FloorZ)
	void BuildYard(UWorld* World, AActor* Owner, USceneComponent* Root, float FloorZ);
}
