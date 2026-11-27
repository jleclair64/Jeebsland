#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// The map rework (owner, 2026-11-23: "Adjust the map so the middle has more of a Scottish grasslands feel, Anchorage is more of a bay
// instead of just a river flowing into the ocean, and I want big cliffs in the northeast of the map where the land meets the ocean,
// similar to the cliffs in Ireland ... give it some character in parts"). Docs/MapRework.md has the log and the pictures.
//
// Every number that places and shapes the new landforms is here; WorldMap.h (the coast, the sea and the land's height), Terrain.h (the
// 2 m ground under them), GroundMask.h (rock, grass, sand) and OpenWorld.cpp (woods, the slope rules) read it. All of it is worked out
// from fixed numbers and fixed noise, the same on the server and on every player's machine. Nothing here moves a town, a road, a quest
// spot or a spawn point: the cliffs and stacks stand on a coast no road comes near.
namespace JeebRework
{
	// -NoMapRework (test builds only): the map as it was before any of this, for before-and-after pictures (Tools/MapBoard.ps1). Give
	// every machine of a test the same switch: the land is different with it.
	inline bool On()
	{
#if UE_BUILD_SHIPPING
		return true;
#else
		static const bool b = !FParse::Param(FCommandLine::Get(), TEXT("NoMapRework"));
		return b;
#endif
	}
	// a smooth 0 .. 1 ramp from A to B, either way round (as JeebWorld::Smooth)
	inline float Ramp(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / ((B - A) == 0.f ? 1.f : (B - A)), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	// ------------------------------------------------------------------ Anchorage's bay
	// Anchorage stood on a cape with the river running straight out to sea past it. Now the coast there curves in: a bay about 250 m
	// across between two headlands, its head 85 m east of the town's middle (the town, its level ground and the road to it are where
	// they were), the river emptying into its head, a shallow shelf inside (shallow water is the lighter, greener water) and two low
	// skerries off its mouth. The south headland is the foot of the hills south of the town.
	constexpr float BayX = -20800.f;          // the middle of the bay (X, cm)
	constexpr float BayHalf = 12500.f;        // to each headland
	constexpr float BayHeadY = 100500.f;      // the shore at the head of the bay (the town's level ground ends at Y 95,200)
	constexpr float BayHorn = 8500.f;         // how far the headlands stand out past the head
	inline float BayBlend(float X) { return On() ? Ramp(-39000.f, -33500.f, X) * Ramp(-3500.f, -9000.f, X) : 0.f; }
	// the bay's shore line (Y) at X, before it is blended into the old coast at both ends
	inline float BayLine(float X) { return BayHeadY + BayHorn * FMath::Pow(FMath::Min(FMath::Abs(X - BayX) / BayHalf, 1.3f), 2.4f); }
	// 0 .. 1: inside the bay (between the headlands, west of the line across its mouth)
	inline float BayShelter(float X, float Y) { return Ramp(BayHalf * 1.15f, BayHalf * 0.8f, FMath::Abs(X - BayX)) * Ramp(BayHeadY + BayHorn + 3500.f, BayHeadY + BayHorn - 1500.f, Y); }
	// the two headlands stand up a little, so the bay reads as sheltered from the town: cm added to the land (a rounded rise on each)
	inline float HeadlandRise(float X, float Y)
	{
		if (X > -2000.f || X < -41000.f || Y < 96000.f || !On()) return 0.f;
		const float A = FMath::Square((X + 8800.f) / 6000.f) + FMath::Square((Y - 105200.f) / 4500.f), B = FMath::Square((X + 33600.f) / 5000.f) + FMath::Square((Y - 104800.f) / 4500.f);
		return 1150.f * FMath::Exp(-A) + 900.f * FMath::Exp(-B);
	}
	// low rocks off the bay's mouth: X, Y, radius, height over the sea (cm)
	struct FSkerry { float X, Y, R, Top; };
	inline const TArray<FSkerry>& Skerries()
	{
		if (!On()) { static const TArray<FSkerry> None; return None; }
		static const TArray<FSkerry> S = { { -14200.f, 111300.f, 3000.f, 700.f }, { -27600.f, 111800.f, 3400.f, 900.f }, { -25000.f, 114000.f, 1800.f, 300.f } };
		return S;
	}

	// ------------------------------------------------------------------ the moor in the middle of the map
	// "The middle has more of a Scottish grasslands feel": the open country east of Oak Hill's forest, between the Silver Point and
	// Anchorage roads and out to the coast and the foot of the sea cliffs. The land keeps its shape (broad rolling hills already: no
	// height changes, so no road moves). What changes is what grows: hardly a tree (a few lone pines), long grass almost everywhere,
	// swathes of tawny deer grass, drifts of purple heather, and low outcrops of grey stone on the rises. It starts where Oak Hill's
	// forest ends (the true middle of the map is that forest, which stays as it is: see Moor).
	inline float Noise2(float X, float Y, float Scale, float Ox, float Oy) { return FMath::PerlinNoise2D(FVector2D(X / Scale + Ox, Y / Scale + Oy)); }
	constexpr float MoorMinX = -30000.f, MoorMaxX = 56000.f, MoorMinY = 55000.f, MoorMaxY = 104000.f;   // (a box round it, for quick rejects)
	// 0 .. 1: on the moor (two rounded lobes with a ragged edge: the heart east of the forest, and the cliff-top country to the north-east)
	inline float Moor(float X, float Y)
	{
		if (X < MoorMinX || X > MoorMaxX || Y < MoorMinY || Y > MoorMaxY || !On()) return 0.f;
		const float R1 = FMath::Sqrt(FMath::Square((X - 8000.f) / 36000.f) + FMath::Square((Y - 84000.f) / 18000.f));
		const float R2 = FMath::Sqrt(FMath::Square((X - 33000.f) / 20000.f) + FMath::Square((Y - 82000.f) / 18000.f));
		const float R = FMath::Min(R1, R2) + 0.16f * Noise2(X, Y, 14000.f, 3.3f, -7.7f) + 0.06f * Noise2(X, Y, 4300.f, -1.2f, 5.4f);
		// (never within 540 m of Oak Hill's plaza, -3550 18290: the forest round the town ends at 500 m, and its ruins and the bandits'
		// camps are placed by where its trees stand. Thinning it moved three ruins and Red Hal's Hold, so the forest keeps every tree.)
		return Ramp(1.02f, 0.72f, R) * Ramp(54000.f, 62000.f, FMath::Sqrt(FMath::Square(X + 3550.f) + FMath::Square(Y - 18290.f)));
	}
	// 0 .. 1: heather (drifts 15 to 60 m across, about a third of the moor) and tawny grass (broader swathes, never where the heather is)
	inline float Heather(float X, float Y)
	{
		const float M = Moor(X, Y);
		if (M <= 0.f) return 0.f;
		return M * Ramp(0.06f, 0.26f, 0.6f * Noise2(X, Y, 5200.f, 4.1f, -2.6f) + 0.4f * Noise2(X, Y, 1700.f, -8.3f, 6.6f) + 0.12f * Noise2(X, Y, 600.f, 2.2f, 9.1f));
	}
	inline float Tawny(float X, float Y)
	{
		const float M = Moor(X, Y);
		if (M <= 0.f) return 0.f;
		return M * Ramp(-0.04f, 0.24f, 0.6f * Noise2(X, Y, 7400.f, -1.9f, 9.2f) + 0.4f * Noise2(X, Y, 2300.f, 5.5f, 3.1f)) * (1.f - Heather(X, Y));
	}
	// the ground's colour on the moor, for M_Ground (the world mesh's second UV, V): + heather, - tawny grass, 0 plain
	inline float MoorTint(float X, float Y)
	{
		if (Moor(X, Y) <= 0.f) return 0.f;
		return Heather(X, Y) - Tawny(X, Y);
	}

	// ------------------------------------------------------------------ the north-east's sea cliffs
	// The coast from about 220 m to 1,015 m north of the world's origin (X, cm). The land is lifted in a long wedge that rises toward the
	// sea (as the Cliffs of Moher do) and ends in a wall 27 to 58 m high: sheer in places, stepped into two grassy ledges in others,
	// its line bent into headlands and bights and its face into buttresses and gullies. Two coves bring the land down to a beach (the
	// way down to the water), and here and there a narrow rocky beach runs along the wall's foot. North of about X 50,000 the tops keep
	// under 34 m, below where the northern range's snow starts to lie.
	constexpr float CliffFromX = 22000.f, CliffToX = 101500.f;
	constexpr float CliffWedge = 30000.f;                       // cm inland of the coast where the lift starts
	constexpr float CoveX[2] = { 47500.f, 76000.f };            // the two coves (the first lies east of Silver Point)
	inline float CliffCoves(float X)
	{
		float K = 1.f;
		for (float C : CoveX) K *= Ramp(1500.f, 8000.f, FMath::Abs(X - C));
		return K;
	}
	// (2026-11-26, the walking test: the first cove's way down was the old 35 m beach ramp squeezing a 28 m hill, with a 55 degree band and
	// an 11.7 m fall in it.) The first cove is now a real glen: within GlenHalf of its middle line the land may stand no higher than a ramp
	// that rises GlenSlope from the beach (about 19 degrees, well under the 45 he can walk), easing out to the cliffs either side.
	constexpr float GlenSlope = 0.34f, GlenCore = 1500.f, GlenHalf = 4200.f, GlenReach = 16000.f;
	inline float CoveGlen(float X) { return On() ? Ramp(GlenHalf, GlenCore, FMath::Abs(X - CoveX[0])) : 0.f; }
	// (2026-11-26, the walking test: a swimmer could not climb out at X 56,000 and X 60,000, where the roughened face bulged over him.)
	// The most a wall's face may gain outward per cm of height (about 7 degrees of overhang; he climbs up to 15): AWorldTerrain::BuildFine.
	constexpr float WallLean = 0.12f;
	// Along the coast at X: Top the wall's height over the sea (cm), A 0 .. 1 how much of a cliff it is (0 the old beach), Beach 0 .. 1 a
	// rocky beach at its foot, Ledge 0 sheer .. 1 stepped, Run the wall's width in plan (cm)
	// (Pale 1: the Pale Cove in the south-east, below: pale rock, a sand beach at the foot and a shallow shelf of light water)
	struct FSeaCliff { float Top = 0.f, A = 0.f, Beach = 0.f, Ledge = 0.f, Run = 1000.f, Pale = 0.f; };
	// ---- the Pale Cove (a character spot, after the owner's picture of pale rock walls and an arch over light water and sand): on the
	// south-east coast between Anchorage's bay and Palmer Square (260 m from that town, no road near), 180 m of pale cliffs 13 to 20 m
	// high round a small sandy cove, three pale stacks in the shallows and a rock arch from the north headland out to the nearest stack
	constexpr float PaleFromX = -74000.f, PaleToX = -56000.f, PaleCoveX = -65000.f;
	inline bool InPaleX(float X) { return X > PaleFromX && X < PaleToX && On(); }
	inline bool InCliffX(float X) { return ((X > CliffFromX && X < CliffToX) || (X > PaleFromX && X < PaleToX)) && On(); }
	inline FSeaCliff SeaCliffAt(float X)
	{
		FSeaCliff C;
		if (!InCliffX(X)) return C;
		if (X < PaleToX)
		{
			const float Dc = FMath::Abs(X - PaleCoveX);
			C.Top = (1650.f + 380.f * FMath::PerlinNoise1D(X / 6000.f + 12.9f) + 160.f * FMath::PerlinNoise1D(X / 1900.f + 3.3f)) * Ramp(PaleFromX, PaleFromX + 4500.f, X) * Ramp(PaleToX, PaleToX - 4500.f, X) * Ramp(1300.f, 3800.f, Dc);
			C.A = Ramp(250.f, 900.f, C.Top);
			C.Beach = Ramp(7500.f, 3500.f, Dc);
			C.Run = 600.f;
			C.Pale = 1.f;
			return C;
		}
		float Hc = 4300.f + 1300.f * FMath::PerlinNoise1D(X / 52000.f + 3.1f) + 500.f * FMath::PerlinNoise1D(X / 19000.f - 8.4f);
		Hc = FMath::Lerp(Hc, FMath::Min(Hc, 3400.f), Ramp(48000.f, 58000.f, X));
		// (second pass: the top's height also wanders over 20 to 60 m, so the skyline is never one ruled line)
		Hc += 330.f * FMath::PerlinNoise1D(X / 5600.f + 21.3f) + 150.f * FMath::PerlinNoise1D(X / 2100.f - 4.9f);
		C.Top = Hc * Ramp(CliffFromX, 31000.f, X) * Ramp(CliffToX, 93000.f, X) * CliffCoves(X);
		C.A = Ramp(250.f, 1100.f, C.Top);
		C.Beach = Ramp(0.08f, 0.4f, FMath::PerlinNoise1D(X / 14000.f + 31.7f));
		// (stepped only where the wall is tall enough for two real ledges. Second pass: it eases between sheer and stepped over some
		// 20 m, the ledges pinching out, where it used to jump 9 m sideways in one straight cut)
		C.Ledge = Ramp(-0.2f, 0.06f, FMath::PerlinNoise1D(X / 11000.f + 4.4f)) * Ramp(2300.f, 2900.f, C.Top);
		C.Run = FMath::Lerp(600.f, 1500.f, C.Ledge);
		return C;
	}
	// cm added to the coast line's Y there: headlands and bights along the cliffs (two waves at unrelated sizes), a bite at each cove
	inline float CoastWiggle(float X)
	{
		if (!InCliffX(X)) return 0.f;
		if (X < PaleToX)   // the Pale Cove: its bite, and a little wander
			return (-4200.f * (1.f - Ramp(0.f, 5500.f, FMath::Abs(X - PaleCoveX))) + 600.f * FMath::PerlinNoise1D(X / 5000.f + 2.2f)) * Ramp(PaleFromX, PaleFromX + 3500.f, X) * Ramp(PaleToX, PaleToX - 3500.f, X);
		float W = 2600.f * FMath::PerlinNoise1D(X / 31000.f + 7.3f) + 900.f * FMath::PerlinNoise1D(X / 9000.f - 2.1f);
		for (float C : CoveX) W -= 3200.f * (1.f - Ramp(0.f, 8000.f, FMath::Abs(X - C)));
		return W * Ramp(CliffFromX, 30000.f, X) * Ramp(CliffToX, 94000.f, X);
	}
	// The wall's profile: T 0 at its foot line .. 1 at its lip line; returns the share of its height there. It is cut in whole steps, not
	// a slope: the ground is a height field on a 2 m grid, and a steep slope's lip and foot fall between grid points differently from one
	// row to the next, which drew a saw-toothed lip and corrugated faces. With steps every grid point is exactly on a level, so the lip is
	// a clean line, each face is one cell wide (80 degrees and more) and the wall's line bends along the grid like chiselled blocks.
	// Sheer: one drop. Stepped (Ledge 1, a wall 15 m wide in plan): three drops with two grassy ledges about 5 m wide between them, at
	// heights that wander along the coast. A ledge is never narrower than two cells: narrower ones came and went from row to row (teeth).
	// (Second pass, 2026-11-25. The mesh no longer has to keep the wall on grid lines: AWorldTerrain::BuildFine moves the grid's points onto
	// each drop's true line. So the three drops' places and the ledges' heights now wander along the coast at unrelated sizes, a ledge
	// may pinch out to nothing, and Ledge runs smoothly from 0 (one drop) to 1 (three).)
	// where the three drops stand (T, from the foot): the lowest, the middle, the lip
	inline void CliffDrops(float Ledge, float X, float& T3, float& T2, float& T1)
	{
		T1 = FMath::Lerp(0.5f, 0.82f + 0.13f * FMath::PerlinNoise1D(X / 2300.f + 8.8f), Ledge);
		T2 = FMath::Lerp(0.5f, 0.45f + 0.24f * FMath::PerlinNoise1D(X / 3100.f - 2.7f), Ledge);
		T3 = FMath::Lerp(0.5f, 0.11f + 0.09f * FMath::PerlinNoise1D(X / 1700.f + 14.1f), Ledge);
		T2 = FMath::Min(T2, T1); T3 = FMath::Min(T3, T2);
	}
	inline float CliffStep(float T, float Ledge, float X)
	{
		float T3, T2, T1;
		CliffDrops(Ledge, X, T3, T2, T1);
		if (T >= T1) return 1.f;
		if (T >= T2) return 0.66f + 0.2f * FMath::PerlinNoise1D(X / 3700.f + 5.5f);
		if (T >= T3) return 0.3f + 0.2f * FMath::PerlinNoise1D(X / 2900.f + 1.2f);
		return 0.f;
	}
	// the lip's place: cm past the wall's foot line (negative: inland) where the top ends
	inline float CliffLip(const FSeaCliff& C, float X)
	{
		float T3, T2, T1;
		CliffDrops(C.Ledge, X, T3, T2, T1);
		return -C.Run * T1;
	}
	// How far a point on a wall's face stands out from the flat face (cm, + outward): beds that stand proud or are eaten back (they dip
	// a little along the coast and undulate), and bulges and hollows at two unrelated sizes. AWorldTerrain::BuildFine cuts every wall
	// face into bands 2 m high and moves their points by this, so a face is never a flat board and overhangs here and there.
	inline float WallRough(const FVector& Q)
	{
		const float Bed = Q.Z + 0.06f * (Q.X + Q.Y) + 170.f * FMath::PerlinNoise2D(FVector2D(Q.X / 2300.f + 1.7f, Q.Y / 2300.f - 3.9f));
		return 85.f * FMath::PerlinNoise1D(Bed / 430.f) + 125.f * FMath::PerlinNoise3D(Q / 1100.f + FVector(3.7f, -8.2f, 1.9f)) + 45.f * FMath::PerlinNoise3D(Q / 390.f + FVector(-11.f, 4.4f, 7.3f));
	}
	// Sea stacks off the wall: X along the coast, Off cm out from the coast line, R radius, Top height over the sea, Ph a turn for its outline
	struct FStack { float X, Off, R, Top, Ph; };
	inline const TArray<FStack>& Stacks()
	{
		if (!On()) { static const TArray<FStack> None; return None; }
		static const TArray<FStack> S = {
			{ 58600.f, 6000.f, 1500.f, 3100.f, 0.7f },
			{ 60500.f, 9000.f, 900.f, 1900.f, 2.9f },
			{ 83200.f, 5400.f, 1250.f, 2500.f, 4.4f },
			{ 34500.f, 6400.f, 1150.f, 2300.f, 1.8f },
			// the Pale Cove's (the first is the one the arch reaches: ArchStack)
			{ PaleCoveX + 5300.f, 3100.f, 750.f, 1500.f, 0.4f },
			{ PaleCoveX - 4800.f, 4600.f, 1200.f, 1700.f, 3.6f },
			{ PaleCoveX + 1500.f, 7600.f, 800.f, 1000.f, 5.1f },
			// (second pass: more of them, of every size, from stumps to towers)
			{ 59700.f, 3900.f, 600.f, 1150.f, 3.3f },
			{ 68000.f, 5600.f, 1050.f, 2150.f, 5.6f },
			{ 69500.f, 8200.f, 560.f, 800.f, 1.1f },
			{ 84700.f, 8000.f, 700.f, 1350.f, 2.5f },
			{ 36300.f, 8700.f, 620.f, 850.f, 4.9f },
			{ 90300.f, 5200.f, 820.f, 1600.f, 0.3f },
			{ 39800.f, 5000.f, 500.f, 600.f, 2.0f },
		};
		return S;
	}
	constexpr int32 ArchStack = 4;        // the stack the Pale Cove's arch reaches (AWorldTerrain::BuildArch spans from the cliff to it)
	constexpr float ArchWidth = 700.f;    // the arch's thickness in plan (cm), on average: it narrows over the opening and swells at its feet
	constexpr float ArchHole = 1050.f;    // the opening's height over the sea at its highest (cm): room to swim or wade through

	// ---- the Needles (a character spot, after the owner's picture of tall rock pillars in forest): a cluster of pale rock pillars 9 to
	// 22 m tall on wooded ground in the hills south of Anchorage. X, Y, radius, height over the ground round it, a turn for its outline.
	constexpr float NeedlesX = -50500.f, NeedlesY = 80500.f, NeedlesHalf = 5600.f;   // the cluster's middle and half its square (cm)
	inline const TArray<FStack>& Needles()
	{
		if (!On()) { static const TArray<FStack> None; return None; }
		static const TArray<FStack> S = {
			{ NeedlesX + 300.f, NeedlesY - 200.f, 1250.f, 2200.f, 0.9f },
			{ NeedlesX - 2600.f, NeedlesY + 1500.f, 800.f, 1500.f, 2.2f },
			{ NeedlesX + 2700.f, NeedlesY + 2300.f, 900.f, 1750.f, 4.0f },
			{ NeedlesX - 1500.f, NeedlesY - 3100.f, 700.f, 1100.f, 5.3f },
			{ NeedlesX + 3300.f, NeedlesY - 2200.f, 1150.f, 1350.f, 1.4f },
			{ NeedlesX - 3600.f, NeedlesY - 900.f, 650.f, 900.f, 3.1f },
			{ NeedlesX + 600.f, NeedlesY + 3900.f, 700.f, 1250.f, 0.2f },
		};
		return S;
	}
	inline bool NearNeedles(float X, float Y, float Margin) { return On() && FMath::Abs(X - NeedlesX) < NeedlesHalf + Margin && FMath::Abs(Y - NeedlesY) < NeedlesHalf + Margin; }
	// 0 .. 1: pale rock (the Pale Cove's cliffs, stacks and arch, the Needles): M_Ground lightens the stone there
	inline float PaleAt(float X, float Y) { return (InPaleX(X) || NearNeedles(X, Y, 800.f)) ? 1.f : 0.f; }
	constexpr float StackReach = 1.5f;   // (times its radius: where its outline's bulges end)
	// A stack's height over the sea at (Dx, Dy) from its middle (cm), or -1e9 outside it: a domed, grassy top and a sheer side
	// whose outline bulges and bites (never a circle)
	// (Second pass: every stack its own shape. Its outline is squeezed along one axis and bulges at four unrelated sizes; its top is
	// domed and tilts; a shoulder at about half height stands on part of its outline and low wave-cut rocks at its foot on another
	// part. BuildFine draws each drop on its true line and roughens the faces.)
	inline float StackHeight(const FStack& S, float Dx, float Dy)
	{
		float Di = FMath::Sqrt(Dx * Dx + Dy * Dy);
		if (Di > S.R * StackReach) return -1.0e9f;
		const float Ang = FMath::Atan2(Dy, Dx);
		Di *= 1.f + (0.22f + 0.2f * FMath::Sin(S.Ph * 3.1f)) * FMath::Square(FMath::Cos(Ang - S.Ph));
		const float Rr = S.R * (1.f + 0.2f * FMath::Sin(2.f * Ang + S.Ph) + 0.13f * FMath::Sin(3.f * Ang + 2.3f * S.Ph) + 0.08f * FMath::Sin(5.f * Ang + 0.7f * S.Ph) + 0.06f * FMath::Sin(8.f * Ang + 4.1f * S.Ph));
		const float U = Di / Rr;
		if (U >= 1.f) return -1.0e9f;
		const float Core = 0.52f + 0.13f * FMath::Sin(3.f * Ang + 1.3f * S.Ph);
		if (U < Core) return S.Top * (1.f - 0.3f * FMath::Square(U / Core) + 0.1f * (U / Core) * FMath::Cos(Ang - 1.9f * S.Ph));
		const float Mid = 0.8f + 0.1f * FMath::Sin(2.f * Ang + 2.9f * S.Ph);
		if (U < Mid && S.R >= 600.f && FMath::Sin(2.f * Ang + 1.7f * S.Ph) > -0.25f) return S.Top * (0.5f + 0.13f * FMath::Sin(3.f * Ang + 0.4f * S.Ph));
		if (FMath::Sin(3.f * Ang + 0.9f * S.Ph) > 0.05f) return FMath::Max(170.f, S.Top * (0.1f + 0.05f * FMath::Sin(5.f * Ang + S.Ph)));
		return -1.0e9f;
	}
}
