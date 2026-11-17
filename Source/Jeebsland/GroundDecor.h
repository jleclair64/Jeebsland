#pragma once

#include "CoreMinimal.h"

class AActor;

// Grounding the buildings (2026-11-12): the buildings' footprints (walls and door), gathered for their foundations. The stones,
// shrubs and flowers once placed here were removed (owner, 2026-11-16): DressFoundations places nothing now (GroundDecor.cpp).
// The worn ground round foundations and doorways is the ground's own (GroundMask.h).
namespace JeebDecor
{
	struct FFootprint
	{
		FVector2D Centre;      // the walls' middle (cm)
		float Yaw = 0.f;       // the walls' turn (degrees)
		FVector2D Half;        // half the walls' size along their own X and Y (cm)
		float DoorYaw = 0.f;   // which way the door faces (world yaw, degrees)
		bool bDoor = true;     // false: no door known (then only the road check keeps the pieces off the way in)
	};
	// Adds the pieces to Owner as instanced components. Seed: the same pieces on every machine.
	void DressFoundations(AActor* Owner, const TArray<FFootprint>& Footprints, int32 Seed);
}
