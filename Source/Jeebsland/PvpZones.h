#pragma once

#include "CoreMinimal.h"

class UWorld;

// PvP zones (2026-11-22, agent horse): the ONE place the game asks "may players hurt each other's things here?".
// Not built yet: another batch (agent pvpfall) adds the zones and fills this in. Until then nothing is inside a PvP zone, so
// horses can never be hurt (owner: horses only take damage from players inside PvP zones).
// Server-side rule: only the server's answer counts (AHorse asks it before any damage lands); players' machines may ask it for looks.
namespace JeebPvp
{
	inline bool IsInPvpZone(const UWorld* World, const FVector& Location)
	{
		(void)World; (void)Location;
		return false;   // TODO(pvpfall): true inside a PvP zone
	}
}
