#pragma once
// Bushes and hedges that block an NPC's sight (2026-11-26, Docs/StealthPlan.md). The bushes are visual only (no collision, and the dedicated
// server draws none), so the line trace of UStealthSense never saw them. Every machine that decides stealth keeps this small list instead:
// the wild bushes (Bushes.cpp, placed on the server too, data only) and the fill's bush and hedge pieces (Fill.cpp). A sight line is a
// segment test against upright rotated boxes in a 10 m hash grid: a few boxes per scan, no per-frame cost.
#include "CoreMinimal.h"

namespace JeebSightCover
{
	enum ESource : int32 { Wild = 0, Fill = 1, Test = 2, NumSources };

	void Reset(ESource Src);
	// An upright box centred (X, Y), its floor at Z, Height tall, half sizes HalfX / HalfY turned Yaw degrees
	void Add(ESource Src, float X, float Y, float Z, float Height, float HalfX, float HalfY, float YawDeg);
	// True when the segment A to B passes through any of them (below their tops)
	bool Blocked(const FVector& A, const FVector& B);
	int32 Count(ESource Src);
}
