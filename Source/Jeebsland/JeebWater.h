#pragma once

#include "CoreMinimal.h"
#include "Terrain.h"

// Where the water is (2026-11-13, swimming: KnightMovement.cpp). One query that every machine answers the same way from shared constants:
// the server, the player's own machine and everyone else's. The water sheets themselves are drawn only on players' machines and have no
// collision (WorldTerrain.cpp, Pond.cpp), so swimming, wading and drowning come only from here, never from the drawn water.
//   - the pond in the original square: within PondWaterR of its middle, at PondWaterZ (its bowl is about 2 m deep in the middle)
//   - the rivers: within the drawn ribbon (half width + 250 cm) of each river's line, at RiverLevel
//   - the sea: the exact sheet WorldTerrain.cpp draws (SeaSheet*: from 30 m west of the westmost coast, out past the world), at SeaLevel
//   - the castle moat (2026-11-15, was missing: it is about 1.2 m deep): the ring Castle.cpp draws, at the castle level - MoatSurfaceDrop
namespace JeebWater
{
	// The castle moat's drawn ring (Castle.cpp: Zw = -47 from the castle's level, between these two radii from CastlePos)
	constexpr float MoatSurfaceDrop = 47.f;
	inline float MoatInner() { return JeebTerrain::CastleMoatIn - 160.f; }
	inline float MoatOuter() { return JeebTerrain::CastleMoatOut + 50.f; }
	inline float MoatZ() { static const float Z = JeebTerrain::CastleLevel() - MoatSurfaceDrop; return Z; }

	// The ocean sheet as WorldTerrain.cpp BuildWater draws it: a rectangle, its west edge 30 m west of the westmost point of the coast
	inline float SeaWestEdge()
	{
		static const float Y = []() { float W = 1.0e9f; for (float X = JeebWorld::MinX; X <= JeebWorld::MaxX; X += 2000.f) W = FMath::Min(W, JeebWorld::CoastY(X)); return W - 3000.f; }();
		return Y;
	}

	// No body of water is higher than this: anything standing above it needs no lookup (most players on land pay one compare)
	inline float TopLevel()
	{
		static const float Top = FMath::Max3(JeebTerrain::PondWaterZ(), JeebWorld::RiverLevel, MoatZ()) + 1.f;
		return Top;
	}

	// The water's surface over (X, Y), if water is drawn there (it may lie under the ground: callers compare it with what stands there)
	inline bool SurfaceAt(float X, float Y, float& OutZ)
	{
		using namespace JeebWorld;
		if (FVector2D::DistSquared(FVector2D(X, Y), JeebTerrain::PondCenter()) < FMath::Square(JeebTerrain::PondWaterR)) { OutZ = JeebTerrain::PondWaterZ(); return true; }
		if (NearRiver(X, Y, 250.f)) { OutZ = RiverLevel; return true; }   // (measured, then compared: see WorldMap.h NearRiver)
		const float Dc = FVector2D::Distance(FVector2D(X, Y), JeebTerrain::CastlePos());
		if (Dc > MoatInner() && Dc < MoatOuter()) { OutZ = MoatZ(); return true; }
		if (Y > SeaWestEdge()) { OutZ = SeaLevel; return true; }   // (the drawn sheet; land stands above it, so land stays dry)
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
