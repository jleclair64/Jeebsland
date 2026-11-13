#pragma once

#include "CoreMinimal.h"

class AActor;

// Grounding the buildings (2026-11-12, owner: buildings must not look pasted on): a few small pieces at each building's foundation,
// separate from the building models (owner rule: a model is only the shell): loose rounded stones in the painted rock (M_Stone),
// small leafy shrubs (M_Bush) and a few flowers, in little clusters along the walls, never on the door side, on a road or in the
// gap to a neighbour. Visual only: no collision, nothing on a dedicated server, the same on every machine (seeded).
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
