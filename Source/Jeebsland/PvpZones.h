#pragma once

#include "CoreMinimal.h"

class AKnightCharacter;

// PvP zones (owner, 2026-11-23): the places where players may hurt each other. Everywhere else a blow, an arrow or a spell from another
// player does nothing. The rectangles are in Content/Data/PvpZones.csv (the first is the snowy north); the server decides every hit from them
// (AKnightCharacter::Hurt and the hit loops), and every machine reads the same file to draw the warning signs (PvpSigns.h), the red title and
// the icon on entering (KnightHUD.cpp). Other systems call IsInPvpZone.
//
// IsInPvpZone(Location): is this world position inside any PvP zone. (JeebPvp::MayHurt also lets players hurt each other in MoveLab, the move test area.)
bool IsInPvpZone(const FVector& Location);

namespace JeebPvp
{
	struct FZone
	{
		FString Name, Title;
		FVector2D Min, Max;   // cm, world X (north) and Y (east)
		float SignEvery = 0.f;   // cm between the warning signs along its edge, 0 none
	};
	const TArray<FZone>& Zones();
	// The zone this spot lies in (X, Y in cm), or INDEX_NONE; the index is into Zones()
	int32 ZoneAt(float X, float Y);
	// May a player's blow, arrow or spell from From hurt To? Both must stand in a PvP zone (MoveLab: always). Null From: yes (not a player's blow).
	bool MayHurt(const AKnightCharacter* From, const AKnightCharacter* To);
}
