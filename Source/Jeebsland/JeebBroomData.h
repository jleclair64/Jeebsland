#pragma once

#include "CoreMinimal.h"

// The wizard's flying brooms and the magic costs of his items (2026-11-22): Content/Data/Brooms.csv and Content/Data/MagicCosts.csv,
// read once (Broom.cpp). Brooms are items 930 + their row (JeebItems::IsBroom); the flight is KnightCharacterBroom.cpp.
namespace JeebBroom
{
	struct FBroomRow { FString Key, Name, Blurb; int32 Price = 0; float SpeedMul = 1.f; float Drain = 3.f; };
	constexpr int32 IdBase = 930;
	constexpr int32 MaxBrooms = 10;
	int32 NumBrooms();
	const FBroomRow& Row(int32 Index);   // (a safe empty row past the end)
	inline bool IsBroomRange(int32 Id) { return Id >= IdBase && Id < IdBase + MaxBrooms; }

	// Magic costs (MagicCosts.csv). Item: "Wand", "Staff", "Book", "Ball", "Pipe"; Tier 0-5.
	float CastMul(const TCHAR* Item, int32 Tier);     // x the spell's magic cost (1 when the sheet says nothing)
	float GuardDrain(const TCHAR* Item, int32 Tier);  // magic a second while the orb guard is held (0 when the sheet says nothing)

	// Silver Point's broom stall: where it stands and which way it faces (the same on every machine; the server checks the distance)
	FVector StallSpot();
	float StallYaw();
	constexpr float StallReach = 450.f;   // cm from the stall's middle within which E opens it and the server sells
}
