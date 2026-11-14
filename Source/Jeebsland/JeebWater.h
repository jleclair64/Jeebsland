#pragma once

#include "CoreMinimal.h"
#include "Terrain.h"

// Where the water is (2026-11-13, swimming: KnightMovement.cpp). One query that every machine answers the same way from shared constants:
// the server, the player's own machine and everyone else's. The water sheets themselves are drawn only on players' machines and have no
// collision (WorldTerrain.cpp, Pond.cpp), so swimming, wading and drowning come only from here, never from the drawn water.
//   - the pond in the original square: within PondWaterR of its middle, at PondWaterZ (its bowl is about 2 m deep in the middle)
//   - the rivers: within the drawn ribbon (half width + 250 cm) of each river's line, at RiverLevel
//   - the sea: east of the coast, at SeaLevel
// (The castle moat is left out: about 1.2 m deep, never deep enough to swim.)
namespace JeebWater
{
	// No body of water is higher than this: anything standing above it needs no lookup (most players on land pay one compare)
	inline float TopLevel()
	{
		static const float Top = FMath::Max(JeebTerrain::PondWaterZ(), JeebWorld::RiverLevel) + 1.f;
		return Top;
	}

	// The water's surface over (X, Y), if water is drawn there (it may lie under the ground: callers compare it with what stands there)
	inline bool SurfaceAt(float X, float Y, float& OutZ)
	{
		using namespace JeebWorld;
		if (FVector2D::DistSquared(FVector2D(X, Y), JeebTerrain::PondCenter()) < FMath::Square(JeebTerrain::PondWaterR)) { OutZ = JeebTerrain::PondWaterZ(); return true; }
		float Half;
		if (RiverDistance(X, Y, Half) < Half + 250.f) { OutZ = RiverLevel; return true; }
		if (Y - CoastY(X) > -3500.f) { OutZ = SeaLevel; return true; }
		return false;
	}

	// How far below the water's surface the point P is (cm; 0 when it is not under water), and the surface there
	inline float DepthAt(const FVector& P, float& OutSurface)
	{
		OutSurface = -1.0e9f;
		if (P.Z >= TopLevel()) return 0.f;
		float S;
		if (!SurfaceAt(P.X, P.Y, S) || S <= P.Z) return 0.f;
		OutSurface = S;
		return S - P.Z;
	}

	// Dry ground (at least 10 cm above any water there) nearest to (X, Y) within MaxR: the server puts a drowned knight's sack there, since nobody
	// can loot under water. A fixed set of rings and spokes, run once per death. False when there is none (out at sea).
	inline bool NearestShore(float X, float Y, float MaxR, FVector& Out)
	{
		const float Here = JeebTerrain::HeightAt(X, Y);
		for (float R = 150.f; R <= MaxR; R += 150.f)
		{
			const int32 Spokes = FMath::Clamp(FMath::RoundToInt(R / 60.f), 12, 48);
			float Best = 1.0e9f;
			for (int32 i = 0; i < Spokes; ++i)
			{
				const float A = 2.f * PI * i / Spokes;
				const float Px = X + R * FMath::Cos(A), Py = Y + R * FMath::Sin(A);
				const float G = JeebTerrain::HeightAt(Px, Py);
				float S;
				if (SurfaceAt(Px, Py, S) && S > G - 10.f) continue;   // (wet)
				const float D = FMath::Abs(G - Here);   // (prefer the lowest bank: a gentle shore over a cliff top)
				if (D < Best) { Best = D; Out = FVector(Px, Py, G); }
			}
			if (Best < 1.0e9f) return true;
		}
		return false;
	}
}
