#pragma once

#include "CoreMinimal.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OakHill.h"

// What the ground is at a spot (2026-11-12, owner: "make the game look more professional"; a painted, Breath of the Wild ground
// everywhere, one ground with no straight seams). Four amounts, 0 .. 1 each:
//   Worn  trampled grass (low values) to packed dirt (high): roads, doorways, round foundations, the well, in front of stalls
//   Rock  steep slopes, high ground, the rocky south-west
//   Mud   river beds and the wet strip along their banks, the pond's edge
//   Sand  the sea shore
// One function for all three ground meshes (the original square GrassField, Oak Hill's patch, the world's 8 m chunks): each
// writes it into its vertex colours (R worn, G rock, B mud, A sand) and M_Ground (Tools/world_material.py) paints the layers in
// world space, so the meshes agree where they meet. The meadow's grass reads it too (GrassAmount, Trample), so tufts never stand
// on rock or mud. Visual only: never collision, never anything the server decides (a dedicated server skips it).
namespace JeebGround
{
	struct FMask
	{
		float Worn = 0.f, Rock = 0.f, Mud = 0.f, Sand = 0.f;
		FLinearColor Color() const { return FLinearColor(Worn, Rock, Mud, Sand); }
		static FMask Lerp(const FMask& A, const FMask& B, float T)
		{
			FMask M; M.Worn = FMath::Lerp(A.Worn, B.Worn, T); M.Rock = FMath::Lerp(A.Rock, B.Rock, T); M.Mud = FMath::Lerp(A.Mud, B.Mud, T); M.Sand = FMath::Lerp(A.Sand, B.Sand, T);
			return M;
		}
	};

	// The slope is measured over 16 m on every mesh (the world grid's own spacing), so a 2 m mesh and an 8 m mesh read the same
	// big landform and agree at their seam; the little pad blends and bumps never turn to rock.
	constexpr float SlopeStep = JeebWorld::WorldStep;
	// Z of the ground's normal there (1 flat, 0 a wall), from heights SlopeStep either side
	inline float Upness(float HxP, float HxM, float HyP, float HyM)
	{
		const float Gx = (HxP - HxM) / (2.f * SlopeStep), Gy = (HyP - HyM) / (2.f * SlopeStep);
		return 1.f / FMath::Sqrt(1.f + Gx * Gx + Gy * Gy);
	}
	inline float Upness(float X, float Y)
	{
		return Upness(JeebTerrain::HeightAt(X + SlopeStep, Y), JeebTerrain::HeightAt(X - SlopeStep, Y), JeebTerrain::HeightAt(X, Y + SlopeStep), JeebTerrain::HeightAt(X, Y - SlopeStep));
	}
	// Soft noise, about 0 .. 1 (fixed: the same everywhere on every machine)
	inline float Soft(float X, float Y, float Scale, float Off) { return 0.5f + 0.5f * FMath::PerlinNoise2D(FVector2D(X + Off, Y - Off * 0.7f) / Scale); }

	// ------------------------------------------------------------------ wear round buildings
	// Blackrock's cottages (ACottage: walls about 7 x 5 m, the door in the middle of its +X end), with their turn
	struct FHutSpot { FVector2D Pos; float Yaw; };
	inline const TArray<FHutSpot>& HutSpots()
	{
		static const TArray<FHutSpot> All = []()
		{
			TArray<FHutSpot> A;
			for (const JeebTerrain::FHutDef& H : JeebTerrain::VillageHuts()) A.Add({ H.Pos, H.Yaw });
			for (const FVector2D& S : JeebTerrain::HutSites())   // (each faces the nearest road, as Village.cpp turns it)
			{
				const FVector2D Near = JeebTerrain::NearestPathPoint(S.X, S.Y);
				A.Add({ S, float(FMath::RadiansToDegrees(FMath::Atan2(Near.Y - S.Y, Near.X - S.X))) });
			}
			return A;
		}();
		return All;
	}
	// How worn the ground is round buildings (0 .. 1), without the roads themselves. PathClearanceCm: the spot's distance to the
	// nearest road edge if already known (doorways are the side of a building that faces its road: the door turns to it).
	inline float BuildingWear(float X, float Y, float PathClearanceCm)
	{
		float W = 0.f;
		const float Ragged = 0.7f + 0.6f * Soft(X, Y, 260.f, 37.f);   // (edges that bulge and bite in, never a straight line)
		// Oak Hill: a thin drip line round every foundation, a trampled apron between each door side and its road, the well all
		// round, and the open fronts of the market stalls
		if (JeebOakHill::Enabled() && JeebOakHill::InBox(X, Y, 1000.f))
		{
			const float E = JeebOakHill::EastM(Y), N = JeebOakHill::NorthM(X);
			const float Road = FMath::Clamp(1.f - PathClearanceCm / 380.f, 0.f, 1.f);
			for (const JeebOakHill::FPlot& P : JeebOakHill::Plots())
			{
				if (P.Kind != JeebOakHill::EPlot::Building || !P.Model) continue;
				if (FMath::Square(E - P.Ce) + FMath::Square(N - P.Cn) > FMath::Square(P.Reach + 7.f)) continue;
				const float D = JeebOakHill::PlotSD(P, E, N);   // metres from its outline
				if (D > 6.f) continue;
				const bool bWell = FCString::Strcmp(P.Model, TEXT("OakHillWell")) == 0;
				const bool bStall = FCString::Strcmp(P.Model, TEXT("GeneralStore")) == 0 || FCString::Strcmp(P.Model, TEXT("PotionStall")) == 0 || FCString::Strcmp(P.Model, TEXT("BankStall")) == 0;
				const float Drip = 0.45f * (1.f - JeebWorld::Smooth(0.2f, 0.9f + 0.6f * Ragged, D));   // (thins and shortens the grass where the foundation stones sit)
				float Apron = 0.f;
				if (bWell) Apron = 0.62f * (1.f - JeebWorld::Smooth(0.6f, 1.6f + 1.4f * Ragged, D));
				else if (bStall) Apron = 0.78f * (1.f - JeebWorld::Smooth(1.0f, 2.5f + 2.5f * Ragged, D)) * FMath::Clamp(Road * 1.6f, 0.f, 1.f);
				else Apron = 0.62f * (1.f - JeebWorld::Smooth(0.6f, 1.6f + 1.6f * Ragged, D)) * Road;
				W = FMath::Max(W, FMath::Max(Drip, Apron));
			}
		}
		// Blackrock's cottages: the drip line round the walls and a trampled patch out of the door
		if (FMath::Abs(X) < 9000.f && FMath::Abs(Y) < 9000.f)
			for (const FHutSpot& H : HutSpots())
			{
				const FVector2D Dp(X - H.Pos.X, Y - H.Pos.Y);
				if (Dp.SizeSquared() > 900.f * 900.f) continue;
				const float C = FMath::Cos(FMath::DegreesToRadians(H.Yaw)), S = FMath::Sin(FMath::DegreesToRadians(H.Yaw));
				const FVector2D L(Dp.X * C + Dp.Y * S, -Dp.X * S + Dp.Y * C);   // (+X out of the door)
				const float D = JeebTerrain::RectSD(L, FVector2D::ZeroVector, FVector2D(360.f, 260.f)) / 100.f;
				const float Drip = 0.45f * (1.f - JeebWorld::Smooth(0.2f, 0.9f + 0.6f * Ragged, D));   // (thins and shortens the grass where the foundation stones sit)
				const float Door = L.X > 300.f ? 0.62f * (1.f - JeebWorld::Smooth(0.5f, 1.4f + 1.4f * Ragged, FVector2D(L.X - 360.f, FMath::Max(float(FMath::Abs(L.Y)) - 90.f, 0.f)).Size() / 100.f)) : 0.f;
				W = FMath::Max(W, FMath::Max(Drip, Door));
			}
		return FMath::Clamp(W, 0.f, 1.f);
	}

	// Where roads are (the original square and Oak Hill: the open world has none yet), so the slow road check is skipped elsewhere
	inline bool NearRoads(float X, float Y) { return (FMath::Abs(X) < 9800.f && FMath::Abs(Y) < 9800.f) || JeebOakHill::InBox(X, Y); }

	// ------------------------------------------------------------------ the whole mask
	// Z: the ground's height there; Up: the normal's Z from Upness (16 m wide). bRoads: also the roads' wear.
	inline FMask At(float X, float Y, float Z, float Up, bool bRoads = true)
	{
		FMask M;
		const FVector2D P(X, Y);
		// Rock: slopes from about 28 to 40 degrees (broken by noise, so it comes in patches and never along a contour), the rocky
		// south-west, and patches on the highest crests from about 34 m up (snow above 60 m is M_Ground's, off until the mountains are built)
		{
			const float N1 = Soft(X, Y, 2200.f, 11.f), N2 = Soft(X, Y, 700.f, 53.f);
			const float Steep = JeebWorld::Smooth(0.88f, 0.76f, Up + 0.035f * (N1 - 0.5f) + 0.02f * (N2 - 0.5f));
			const float Rocky = JeebWorld::RockMask(P) * JeebWorld::Smooth(0.45f, 0.75f, N1 * 0.7f + N2 * 0.3f) * 0.9f;
			const float High = JeebWorld::Smooth(3400.f, 4800.f, Z + 700.f * (N1 - 0.5f)) * JeebWorld::Smooth(0.4f, 0.7f, N2) * JeebWorld::Smooth(0.96f, 0.86f, Up) * 0.8f;   // (only where the crest slopes too: never a blob on a flat top)
			M.Rock = FMath::Clamp(FMath::Max3(Steep, Rocky, High), 0.f, 1.f);
		}
		// Mud: the river beds (under the water) and a wet, ragged strip along their banks; the pond's edge
		{
			float Half;
			const float Dr = JeebWorld::RiverDistance(X, Y, Half);
			if (Dr < Half + 700.f)
			{
				const float N = Soft(X, Y, 340.f, 71.f);
				M.Mud = (1.f - JeebWorld::Smooth(Half + 60.f + 200.f * N, Half + 200.f + 260.f * N, Dr)) * JeebWorld::Smooth(JeebWorld::RiverLevel + 110.f + 60.f * N, JeebWorld::RiverLevel + 25.f, Z);
				M.Mud = FMath::Max(M.Mud, JeebWorld::Smooth(JeebWorld::RiverLevel + 5.f, JeebWorld::RiverLevel - 40.f, Z) * JeebWorld::Smooth(Half + 400.f, Half, Dr));
			}
			const float Dp = FVector2D::Distance(P, JeebTerrain::PondCenter());
			if (Dp < JeebTerrain::PondWaterR + 300.f)
			{
				const float N = Soft(X, Y, 200.f, 83.f);
				M.Mud = FMath::Max(M.Mud, 1.f - JeebWorld::Smooth(JeebTerrain::PondWaterR - 60.f + 90.f * N, JeebTerrain::PondWaterR + 60.f + 140.f * N, Dp));
			}
		}
		// Sand: the beach along the sea
		if (Z < JeebWorld::SeaLevel + 260.f && Y > 40000.f)
		{
			const float N = Soft(X, Y, 900.f, 97.f);
			M.Sand = JeebWorld::Smooth(JeebWorld::SeaLevel + 170.f + 80.f * N, JeebWorld::SeaLevel + 50.f, Z) * JeebWorld::Smooth(-4500.f, -800.f, Y - JeebWorld::CoastY(X));
			M.Mud *= 1.f - M.Sand;
		}
		// Worn: beside the roads (1 on the edge, fading out over about 2.5 m, as the old ground had it), and round buildings
		float Pc = 1.0e6f;
		if (bRoads && NearRoads(X, Y))
		{
			Pc = JeebTerrain::PathClearance(X, Y);
			M.Worn = 1.f - FMath::SmoothStep(-20.f, 250.f, Pc);
		}
		M.Worn = FMath::Max(M.Worn, BuildingWear(X, Y, Pc));
		M.Rock *= 1.f - M.Worn;   // (a doorway or road on a steep spot stays earth)
		return M;
	}
	inline FMask At(float X, float Y, bool bRoads = true) { return At(X, Y, JeebTerrain::HeightAt(X, Y), Upness(X, Y), bRoads); }

	// ------------------------------------------------------------------ for the meadow's grass
	// How much grass grows (0 none .. 1 full) from a mask: none on rock, mud or sand, fading in softly round them
	inline float GrassAmount(const FMask& M)
	{
		return (1.f - JeebWorld::Smooth(0.25f, 0.65f, M.Rock)) * (1.f - JeebWorld::Smooth(0.15f, 0.5f, M.Mud)) * (1.f - JeebWorld::Smooth(0.15f, 0.5f, M.Sand));
	}
}
