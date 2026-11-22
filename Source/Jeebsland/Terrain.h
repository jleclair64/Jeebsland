#pragma once

#include "CoreMinimal.h"
#include "WorldMap.h"
#include "OakHill.h"
#include "TownLab.h"
#include "Hills.h"
#include "SteeleCreek.h"
#include "OpenWorld.h"      // the open world outside Oak Hill's hills (2026-11-21): the tall northern range, rolling country, the roads between the towns
#include "BridgeGround.h"   // the stone bridge over the river west of Oak Hill (2026-11-18): its embankments and the roads to it

// The forest's roads round Oak Hill (OakForest.cpp), made paths below
namespace JeebOakForest { const TArray<TArray<FVector2D>>& RoadLines(); }

// Shared description of the land, so the ground mesh, trees, rocks, huts, paths and walls all agree.
namespace JeebTerrain
{
	// ------------------------------------------------------------------ places
	// The old village site (Blackrock until 2026-11-22, when it was taken out of the game): a level, empty square kept free
	// for the town that will replace it (its pad, the roads to its gates and the tree-free ground stay; no walls, huts or props)
	inline FVector2D VillageCenter() { return FVector2D(-2900.f, -300.f); }

	// A little graveyard in the meadow
	inline FVector GraveyardPos() { return FVector(-2300.f, 2750.f, 0.f); }

		

	// Where the first cottage stands (inside the flat part of the meadow)
	inline FVector CottagePos() { return FVector(1500.f, -1600.f, 0.f); }

	// The village wall is a square this far from the centre to each side (so 4000 wide)
	inline float VillageHalf() { return 2000.f; }

	// The pig pen (north of the start)
	inline FVector2D PenCenter() { return FVector2D(300.f, 2700.f); }
	inline FVector2D PenHalf() { return FVector2D(800.f, 550.f); }

	// (the market with the store, smithy and bank that stood south of the start went 2026-11-22)

	// The fishing pond, south of the start. The ground dips into a bowl; the water is a flat disc a little below the rim.
	inline FVector2D PondCenter() { return FVector2D(-300.f, -2900.f); }
	constexpr float PondBowl = 900.f;       // the ground is shaped inside this radius
	constexpr float PondFade = 1250.f;      // and blends back to the hills out to here
	constexpr float PondDepth = 240.f;      // how far the middle of the bowl sinks (2026-11-13: was 110, 74 cm of water; now about 2 m in the middle, deep enough to swim: JeebWater.h)
	constexpr float PondWaterDrop = 36.f;   // water surface sits this far below the rim
	constexpr float PondWaterR = 650.f;     // radius of the water disc (the shoreline is a bit inside this)

	// The castle in the south-east: a square wall with a corner tower at each corner, a moat all the way round,
	// a bridge and gate on the side facing the village (-X), and the spire tower in the courtyard.
	inline FVector2D CastlePos() { return FVector2D(7300.f, -3000.f); }
	constexpr float CastleWall = 700.f;        // the wall's centre line, this far from the middle
	constexpr float CastleWallT = 70.f;        // half the wall's thickness
	constexpr float CastleWallH = 520.f;       // the walkway on top of the wall is this high above the ground
	constexpr float CastleTower = 200.f;       // half the width of a corner tower
	constexpr float CastleGateHalf = 130.f;    // half the width of the gate (and the bridge)
	constexpr float CastleMoatIn = 1400.f;     // the moat is a ring between these two radii
	constexpr float CastleMoatOut = 1850.f;
	constexpr float CastleMoatDepth = 170.f;
	constexpr float CastleFlat = 2000.f;       // level ground out to here, then it blends back into the hills
	constexpr float CastleFade = 2700.f;

	// Signed distance from a point to a rectangle (negative inside)
	inline float RectSD(const FVector2D& P, const FVector2D& C, const FVector2D& H)
	{
		const FVector2D D(FMath::Abs(P.X - C.X) - H.X, FMath::Abs(P.Y - C.Y) - H.Y);
		return FVector2D(FMath::Max(D.X, 0.f), FMath::Max(D.Y, 0.f)).Size() + FMath::Min(FMath::Max(D.X, D.Y), 0.f);
	}
	// True if a prop of this radius would touch the pig pen and its fence, the pond or the castle
	inline bool PropBlocked(float X, float Y, float Radius)
	{
		const FVector2D P(X, Y);
		// (the village wall it once kept props off went with Blackrock, 2026-11-22)
		if (RectSD(P, PenCenter(), PenHalf()) < Radius + 130.f) return true;
		if (FVector2D::Distance(P, PondCenter()) < PondBowl + Radius) return true;
		if (FVector2D::Distance(P, CastlePos()) < CastleFlat + Radius) return true;
		return false;
	}

	// Walkway along the inside of the wall: its top surface is this high above the village ground
	inline float DeckHeight() { return 230.f; }

	// Huts inside the walls (at least 5 m apart), doors facing the plaza
	struct FHutDef { FVector2D Pos; float Yaw; };
	inline const TArray<FHutDef>& VillageHuts()
	{
		static const TArray<FHutDef> All;   // (none: the five huts inside Blackrock's walls went with it, 2026-11-22)
		return All;
	}

	// Ladders up to the walkway. Pos is the ladder itself; Out points away from the wall (the side you climb from).
	struct FLadder { FVector2D Pos; FVector2D Out; float Top = -1.f; float Bottom = -1.f; bool bAbsolute = false; };   // Bottom > 0: the ladder starts from a deck at this height, not the ground   // Top < 0: the village walkway; otherwise the height of the deck   // bAbsolute: Top and Bottom are exact heights whatever their sign (MoveLab's ladder)
	// Ladders added at run time (MoveLab builds one on its flat floor): climbed like the others, never part of the real world. Emptied when a world builds.
	inline TArray<FLadder>& ExtraLadders() { static TArray<FLadder> All; return All; }
	inline const TArray<FLadder>& Ladders()
	{
		static const TArray<FLadder> All;   // (none: the four ladders up Blackrock's wall walk went with it, 2026-11-22)
		return All;
	}

	// Benches around the village fountain: where you can sit (position, and the direction you face)
	struct FSeat { FVector2D Pos; float Yaw; };
	inline const TArray<FSeat>& Seats()
	{
		static const TArray<FSeat> All;   // (none: the four benches round the fountain went with Blackrock, 2026-11-22)
		return All;
	}

	// Huts scattered outside the village
	inline const TArray<FVector2D>& HutSites()
	{
		static const TArray<FVector2D> Sites = {
			FVector2D(3000.f, 1500.f), FVector2D(4700.f, -1200.f), FVector2D(2850.f, -3900.f), FVector2D(-600.f, -4500.f),
			FVector2D(-3900.f, -3400.f), FVector2D(-6300.f, 800.f), FVector2D(200.f, 4400.f), FVector2D(3600.f, 2900.f)
		};
		return Sites;
	}

		// Places where grass and fallen leaves must not grow: under every roof, in the courtyard and moat, on the water
	inline bool NoGroundCover(float X, float Y)
	{
		const FVector2D P(X, Y);
		for (const FVector2D& S : HutSites()) if (FVector2D::Distance(P, S) < 480.f) return true;
		for (const FHutDef& H : VillageHuts()) if (FVector2D::Distance(P, H.Pos) < 480.f) return true;
		if (FVector2D::Distance(P, FVector2D(CottagePos().X, CottagePos().Y)) < 480.f) return true;
		if (FVector2D::Distance(P, CastlePos()) < CastleMoatOut + 80.f) return true;
				if (FVector2D::Distance(P, PondCenter()) < PondWaterR) return true;
		if (JeebWorld::InWater(X, Y)) return true;   // the river (it runs through the east side of the square)
		if (JeebOakHill::OnBuilding(X, Y)) return true;   // Oak Hill's buildings
		if (JeebSteele::OnBuilding(X, Y)) return true;    // Steele Creek's (SteeleCreek.h)

		return false;
	}

	// ------------------------------------------------------------------ land
	// Big rolling hills. The meadow around the start is flat; hills grow outward from there.
	inline float NaturalHeight(float X, float Y)
	{
		const float R = FMath::Sqrt(X * X + Y * Y);
		const float T = FMath::Clamp((R - 2500.f) / 3000.f, 0.f, 1.f);
		const float Blend = T * T * (3.f - 2.f * T);

		const float N1 = FMath::Sin(X * 0.00078f + 0.7f) * FMath::Cos(Y * 0.00071f + 1.3f);
		const float N2 = FMath::Sin((X + Y) * 0.0013f + 2.1f) * FMath::Cos((X - Y) * 0.0011f);
		const float N = 0.65f * N1 + 0.35f * N2; // between -1 and 1

		return 6.f + Blend * 520.f * (0.5f + 0.5f * N);
	}

	inline float CastleLevel() { return NaturalHeight(CastlePos().X, CastlePos().Y); }

	// Level building plots: the village and every hut sit on flat ground that blends back into the hills
	struct FPad { FVector2D Center; float FlatRadius; float FadeRadius; float Level; };
	inline const TArray<FPad>& Pads()
	{
		static const TArray<FPad> All = []()
		{
			TArray<FPad> A;
			const FVector2D VC = VillageCenter();
			A.Add(FPad{ VC, 2900.f, 3700.f, NaturalHeight(VC.X, VC.Y) });
			A.Add(FPad{ CastlePos(), CastleFlat, CastleFade, CastleLevel() });
			for (const FVector2D& S : HutSites())
			{
				A.Add(FPad{ S, 520.f, 950.f, NaturalHeight(S.X, S.Y) });
			}
			return A;
		}();
		return All;
	}

	inline float HeightNoPond(float X, float Y)
	{
		float H = NaturalHeight(X, Y);
		for (const FPad& P : Pads())
		{
			const float D = FVector2D::Distance(FVector2D(X, Y), P.Center);
			if (D < P.FadeRadius)
			{
				const float T = FMath::Clamp((D - P.FlatRadius) / (P.FadeRadius - P.FlatRadius), 0.f, 1.f);
				const float Wgt = 1.f - T * T * (3.f - 2.f * T);
				H = FMath::Lerp(H, P.Level, Wgt);
			}
		}
		return H;
	}

	// Height of the ground at the rim of the pond (the water sits PondWaterDrop below this)
	inline float PondRim() { static const float Rim = HeightNoPond(PondCenter().X, PondCenter().Y); return Rim; }
	inline float PondWaterZ() { return PondRim() - PondWaterDrop; }

	// The original square round the start (the meadow, the old village site, the castle, the pond), exactly as it was built
	inline float HomeHeight(float X, float Y)
	{
		float H = HeightNoPond(X, Y);
		const float D = FVector2D::Distance(FVector2D(X, Y), PondCenter());
		if (D < PondFade)
		{
			const float T = FMath::Clamp((D - PondBowl) / (PondFade - PondBowl), 0.f, 1.f);
			const float W = 1.f - T * T * (3.f - 2.f * T);   // level with the rim inside the bowl, back to the hills outside
			H = FMath::Lerp(H, PondRim(), W);
			const float U = FMath::Clamp(D / PondBowl, 0.f, 1.f);
			H -= PondDepth * (1.f - U * U * (3.f - 2.f * U));
		}
		// The castle moat: a ring dug into the level ground, with sloping banks
		const float Dc = FVector2D::Distance(FVector2D(X, Y), CastlePos());
		if (Dc > CastleMoatIn - 120.f && Dc < CastleMoatOut + 120.f)
		{
			const float Edge = FMath::Min(Dc - (CastleMoatIn - 120.f), (CastleMoatOut + 120.f) - Dc);
			const float Tm = FMath::Clamp(Edge / 240.f, 0.f, 1.f);
			H -= CastleMoatDepth * Tm * Tm * (3.f - 2.f * Tm);
		}
		return H;
	}

	// The ground anywhere in the world (2026-10-30): the original square round the start, blending over its outer 40 m into the
	// whole world's land (WorldMap.h: mountains, the rocky south-west, level town sites), then the sea, the coast and the river
	// valleys and channels everywhere (the river runs down through the east side of the original square).
	constexpr float GroundHalfForBlend = 16000.f;
	inline float HeightAt(float X, float Y, bool bCap = true, int32 Hills = 2, bool bPads = true, bool bOpen = true);
	float RoadEaseDelta(float X, float Y);   // (RoadSlope.cpp: Oak Hill's roads eased to a walk, 2026-11-20)

	// ------------------------------------------------------------------ building pads
	// (2026-11-18, owner: "Houses or structures will be half hidden underground because they were placed on hills ... feel free to just
	// change the terrain to make it flat where you want the house/structures to go", and "entrances that are underground".) Every
	// building of Oak Hill (houses, shops, stalls, the well, the Town Hall, the church, the forts, the watchtower, the ruins) and of Steele
	// Creek stands on a pad: its walls' box and PadFlat round it, plus an apron PadApron long in front of its door, all level at the
	// building's own height, blending back into the land over PadBlendBy times the height step (at least PadBlendMin, at most
	// PadBlendMax). Read from the towns' plot data when the world is first built, so a building moved or added there gets its pad by
	// itself. The level: a levelled Oak Hill plot keeps its own pad's height (the ground at its middle), a Steele building its plan's
	// Level, and an unlevelled one (the war buildings) the middle height of the ground under its walls and apron. Pads mix by weights that
	// grow without end on a flat (w / (1 - w), as Oak Hill's own), so every flat stays exactly level; none reaches into the water.
	constexpr float PadFlat = 200.f, PadApron = 400.f, PadBlendBy = 6.f, PadBlendMin = 400.f, PadBlendMax = 2000.f;
	struct FBuildingPad
	{
		const TCHAR* Name = nullptr;
		FVector2D Mid = FVector2D::ZeroVector, Half = FVector2D::ZeroVector;   // the walls' box (game cm; Half in its own frame)
		float Cs = 1.f, Sn = 0.f;                                            // its turn
		FVector2D ApronMid = FVector2D::ZeroVector, ApronHalf = FVector2D::ZeroVector;   // the doorstep (its own frame, the same turn)
		float Level = 0.f;
		float Reach = 0.f;   // cm from Mid past which it changes nothing
		bool bSteele = false;
	};
	inline float BoxSD(float LX, float LY, const FVector2D& H)
	{
		const FVector2D Q(FMath::Abs(LX) - H.X, FMath::Abs(LY) - H.Y);
		return FVector2D(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f)).Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.f);
	}
	// Signed distance (cm) past a pad's flat: its walls' box + PadFlat, or the doorstep
	inline float PadSD(const FBuildingPad& P, float X, float Y)
	{
		const float DX = X - P.Mid.X, DY = Y - P.Mid.Y;
		const float LX = DX * P.Cs + DY * P.Sn, LY = -DX * P.Sn + DY * P.Cs;
		return FMath::Min(BoxSD(LX, LY, P.Half) - PadFlat, BoxSD(LX - P.ApronMid.X, LY - P.ApronMid.Y, P.ApronHalf));
	}
	// A pad from a model's walls (its own frame, Blender's: WX0..WX1, WY0..WY1, cm before Scale), its centre, turn, scale and door yaw
	inline FBuildingPad MakePad(const TCHAR* Name, const JeebOakHill::FModel& M, const FVector2D& Centre, float Yaw, float Scale)
	{
		FBuildingPad P; P.Name = Name;
		P.Cs = FMath::Cos(FMath::DegreesToRadians(Yaw)); P.Sn = FMath::Sin(FMath::DegreesToRadians(Yaw));
		// (Unreal mirrors Blender's Y: the walls run from (WX0, -WY1) to (WX1, -WY0) in the model's frame)
		const FVector2D Lo(M.WX0 * Scale, -M.WY1 * Scale), Hi(M.WX1 * Scale, -M.WY0 * Scale);
		const FVector2D Ml = (Lo + Hi) * 0.5f;
		P.Mid = Centre + FVector2D(Ml.X * P.Cs - Ml.Y * P.Sn, Ml.X * P.Sn + Ml.Y * P.Cs);
		P.Half = (Hi - Lo) * 0.5f;
		// the door's side in the box's own frame (DoorYaw 90 is the model's +Y, every model so far), and the doorstep in front of it
		const float Dy = FMath::DegreesToRadians(M.DoorYaw);
		const FVector2D Dir(FMath::Cos(Dy), FMath::Sin(Dy));
		if (FMath::Abs(Dir.Y) >= FMath::Abs(Dir.X))
		{
			const float S = FMath::Sign(Dir.Y);
			P.ApronMid = FVector2D(0.f, S * (P.Half.Y + PadApron * 0.5f));
			P.ApronHalf = FVector2D(FMath::Min(P.Half.X, 250.f) + 60.f, PadApron * 0.5f);
		}
		else
		{
			const float S = FMath::Sign(Dir.X);
			P.ApronMid = FVector2D(S * (P.Half.X + PadApron * 0.5f), 0.f);
			P.ApronHalf = FVector2D(PadApron * 0.5f, FMath::Min(P.Half.Y, 250.f) + 60.f);
		}
		P.Reach = P.Half.Size() + PadApron + PadFlat + PadBlendMax + 100.f;
		return P;
	}
	// The middle height of the ground (before any pad) under a pad's walls and doorstep, never lower than 90 cm over the rivers
	inline float PadGroundLevel(const FBuildingPad& P)
	{
		TArray<float> H;
		for (int32 a = 0; a <= 6; ++a) for (int32 b = 0; b <= 6; ++b)
		{
			const FVector2D L(FMath::Lerp(-P.Half.X, P.Half.X, a / 6.f), FMath::Lerp(-P.Half.Y, P.Half.Y, b / 6.f));
			const FVector2D W = P.Mid + FVector2D(L.X * P.Cs - L.Y * P.Sn, L.X * P.Sn + L.Y * P.Cs);
			H.Add(HeightAt(W.X, W.Y, true, 2, false));
		}
		for (int32 a = 0; a <= 2; ++a)
		{
			const FVector2D L = P.ApronMid + FVector2D(FMath::Lerp(-P.ApronHalf.X, P.ApronHalf.X, a / 2.f), FMath::Lerp(-P.ApronHalf.Y, P.ApronHalf.Y, a / 2.f));
			const FVector2D W = P.Mid + FVector2D(L.X * P.Cs - L.Y * P.Sn, L.X * P.Sn + L.Y * P.Cs);
			H.Add(HeightAt(W.X, W.Y, true, 2, false));
		}
		H.Sort();
		return FMath::Max(H[H.Num() / 2], JeebWorld::RiverLevel + 90.f);
	}
	inline const TArray<FBuildingPad>& BuildingPads()
	{
		static const TArray<FBuildingPad> All = []()
		{
			TArray<FBuildingPad> A;
			if (JeebOakHill::Enabled())
				for (const JeebOakHill::FPlot& Pl : JeebOakHill::Plots())
				{
					if (Pl.Kind != JeebOakHill::EPlot::Building || !Pl.Model) continue;
					const JeebOakHill::FModel* M = JeebOakHill::FindModel(Pl.Model);
					if (!M || !(M->WX1 > M->WX0)) continue;
					// (where ATownLabTown and AOakHillTown put it: the plot's centre, its BuildingYaw, the model's own scale)
					const FVector2D C = JeebOakHill::FromPx(Pl.Px, Pl.Py);
					FBuildingPad P = MakePad(Pl.Name, *M, C, JeebOakHill::BuildingYaw(Pl, M), M->Scale);
					P.Level = Pl.bLevel ? HeightAt(C.X, C.Y, true, 2, false) : PadGroundLevel(P);
					A.Add(P);
				}
			if (JeebSteele::Enabled())
				for (const JeebSteele::FPlot& Pl : JeebSteele::Plots())
				{
					if (!Pl.Model) continue;
					FBuildingPad P = MakePad(Pl.Def.Name, *Pl.Model, Pl.Centre, Pl.Yaw, Pl.Scale);
					P.Level = Pl.Level;   // (ASteeleCreekTown sets each building at its plan's Level)
					P.bSteele = true;
					A.Add(P);
				}
			return A;
		}();
		return All;
	}
	// The ground with every pad on it (H: the ground without them)
	inline float ApplyPads(float X, float Y, float H)
	{
		const bool bOak = JeebOakHill::InBox(X, Y, PadBlendMax + 1000.f), bSteele = JeebSteele::InBox(X, Y, PadBlendMax + 1000.f);
		if (!bOak && !bSteele) return H;
		const TArray<FBuildingPad>& Ps = BuildingPads();
		float Sum = H, Weight = 1.f;
		bool bAny = false;
		float Dry = -1.f;
		for (const FBuildingPad& P : Ps)
		{
			if ((P.bSteele ? !bSteele : !bOak) || FVector2D::DistSquared(P.Mid, FVector2D(X, Y)) > FMath::Square(P.Reach)) continue;
			const float Blend = FMath::Clamp(PadBlendBy * FMath::Abs(H - P.Level), PadBlendMin, PadBlendMax);
			const float D = PadSD(P, X, Y);
			if (D >= Blend) continue;
			float W = 1.f - JeebWorld::Smooth(0.f, Blend, D);
			// (never into the water: outside a building's own walls a pad gives way from 3 m past the waterline to the water's edge;
			// under the walls it holds, so a building put on a bank (the forts) is never sunk into it)
			{
				const float DX = X - P.Mid.X, DY = Y - P.Mid.Y;
				const float Wall = BoxSD(DX * P.Cs + DY * P.Sn, -DX * P.Sn + DY * P.Cs, P.Half);
				if (Wall > 0.f)
				{
					if (Dry < 0.f) Dry = JeebWorld::Smooth(100.f, 400.f, JeebWorld::WaterEdge(X, Y));
					W *= FMath::Max(Dry, 1.f - JeebWorld::Smooth(0.f, 150.f, Wall));
				}
			}
			if (W <= 0.f) continue;
			const float Odds = FMath::Min(W / FMath::Max(1.f - W, 1e-5f), 1.0e5f);
			Sum += Odds * P.Level; Weight += Odds;
			bAny = true;
		}
		return bAny ? Sum / Weight : H;
	}

	// ------------------------------------------------------------------ finer ground where the world's 8 m mesh is too coarse
	// (2026-11-18) Steele Creek (its pads and doorsteps), the lake (its shore, beach and island) and the split north of Oak Hill are drawn
	// on a 2 m grid (AWorldTerrain), each patch a box on the 8 m grid's lines; its edge points lie on the 8 m mesh's edges, so the two
	// meshes meet without a crack.
	constexpr float FineStep = 200.f;
	struct FFinePatch { float X0, X1, Y0, Y1; };
	inline const TArray<FFinePatch>& FinePatches()
	{
		static const TArray<FFinePatch> All = []()
		{
			TArray<FFinePatch> A;
			const float S = JeebWorld::WorldStep;
			auto Add = [&](float X0, float X1, float Y0, float Y1)
			{
				FFinePatch P;
				P.X0 = JeebWorld::MinX + FMath::FloorToFloat((X0 - JeebWorld::MinX) / S) * S; P.X1 = JeebWorld::MinX + FMath::CeilToFloat((X1 - JeebWorld::MinX) / S) * S;
				P.Y0 = JeebWorld::MinY + FMath::FloorToFloat((Y0 - JeebWorld::MinY) / S) * S; P.Y1 = JeebWorld::MinY + FMath::CeilToFloat((Y1 - JeebWorld::MinY) / S) * S;
				A.Add(P);
			};
			if (JeebSteele::Enabled()) Add(JeebSteele::PosX - 11000.f, JeebSteele::PosX + 11000.f, JeebSteele::PosY - 11000.f, JeebSteele::PosY + 11000.f);
			const FBox2D L = FBox2D(JeebWorld::Lake().Shore).ExpandBy(2400.f);
			Add(L.Min.X, L.Max.X, L.Min.Y, L.Max.Y);
			Add(16000.f, 32800.f, 3200.f, 14400.f);   // (the split's north part: the south part lies on the square's 2 m ground and Oak Hill's 1 m)
			return A;
		}();
		return All;
	}
	inline const FFinePatch* FinePatchAt(float X, float Y)
	{
		for (const FFinePatch& P : FinePatches()) if (X >= P.X0 && X < P.X1 && Y >= P.Y0 && Y < P.Y1) return &P;
		return nullptr;
	}
	inline bool InFineCell(float X0, float Y0, float Step)
	{
		for (const FFinePatch& P : FinePatches()) if (X0 >= P.X0 && X0 + Step <= P.X1 && Y0 >= P.Y0 && Y0 + Step <= P.Y1) return true;
		return false;
	}
	// Is the 2 m cell with its low corner at (X0, Y0) drawn by a fine patch?
	inline bool FineCovers(float X0, float Y0)
	{
		for (const FFinePatch& Q : FinePatches()) if (X0 >= Q.X0 - 1.f && X0 + FineStep <= Q.X1 + 1.f && Y0 >= Q.Y0 - 1.f && Y0 + FineStep <= Q.Y1 + 1.f) return true;
		return false;
	}
	// Is the 2 m cell at (X0, Y0) the world's 8 m ground, as a fine patch sees it (not the square's own 2 m ground, not a fine patch)?
	inline bool CoarseBesideFine(float X0, float Y0)
	{
		const float Hh = 16000.f;   // (GroundHalf: the original square)
		if (X0 >= -Hh && X0 + FineStep <= Hh && Y0 >= -Hh && Y0 + FineStep <= Hh) return false;
		return !FineCovers(X0, Y0);
	}
	// A 2 m grid point that may border the world's 8 m mesh (fine patches, the square's ground): its height as drawn. Where any of the
	// four 2 m cells round it is 8 m ground (bCoarse says which) it takes the 8 m mesh's own straight edge, so the two meet without a
	// crack; everywhere else (inside, and where two 2 m grounds meet) the true height, the same on both sides. (2026-11-20, owner's river
	// bank slits: a patch's edge followed the 8 m line even where it met the square's 2 m ground or another patch, which drew the true
	// bank there, and the two pulled apart into dark slits across the banks.)
	template <typename FCoarse, typename FHt> inline float EdgeVertexZWith(float X, float Y, FCoarse bCoarse, FHt Ht)
	{
		const float S = JeebWorld::WorldStep, F = FineStep;
		if (!(bCoarse(X - F, Y - F) || bCoarse(X, Y - F) || bCoarse(X - F, Y) || bCoarse(X, Y))) return Ht(X, Y);
		const float Tx = (X - JeebWorld::MinX) / S, Ty = (Y - JeebWorld::MinY) / S;
		const bool bOnX = FMath::Abs(Tx - FMath::RoundToFloat(Tx)) * S < 1.f, bOnY = FMath::Abs(Ty - FMath::RoundToFloat(Ty)) * S < 1.f;
		if (bOnX && !bOnY)
		{
			const float V0 = JeebWorld::MinY + FMath::FloorToFloat(Ty) * S;
			return FMath::Lerp(Ht(X, V0), Ht(X, V0 + S), (Y - V0) / S);
		}
		if (bOnY && !bOnX)
		{
			const float V0 = JeebWorld::MinX + FMath::FloorToFloat(Tx) * S;
			return FMath::Lerp(Ht(V0, Y), Ht(V0 + S, Y), (X - V0) / S);
		}
		return Ht(X, Y);
	}
	template <typename FCoarse> inline float EdgeVertexZ(float X, float Y, FCoarse bCoarse) { return EdgeVertexZWith(X, Y, bCoarse, [](float A, float B) { return HeightAt(A, B); }); }
	// A fine patch's grid point (game cm) as drawn: the true height inside, along an edge on the 8 m mesh that mesh's own straight edge
	inline float FineVertexZ(const FFinePatch& P, float X, float Y)
	{
		return EdgeVertexZ(X, Y, CoarseBesideFine);
	}
	inline float FineMeshHeight(const FFinePatch& P, float X, float Y)
	{
		const float GX = (X - P.X0) / FineStep, GY = (Y - P.Y0) / FineStep;
		const float I = FMath::FloorToFloat(GX), J = FMath::FloorToFloat(GY);
		const float Fx = GX - I, Fy = GY - J;
		const float X0 = P.X0 + I * FineStep, Y0 = P.Y0 + J * FineStep;
		const float A = FineVertexZ(P, X0, Y0), B = FineVertexZ(P, X0 + FineStep, Y0), C = FineVertexZ(P, X0, Y0 + FineStep);
		if (Fx + Fy <= 1.f) return A + (B - A) * Fx + (C - A) * Fy;
		const float D = FineVertexZ(P, X0 + FineStep, Y0 + FineStep);
		return D + (C - D) * (1.f - Fx) + (B - D) * (1.f - Fy);
	}

	// (bCap false: without the rivers' bank cap, for the bank scan's "what did the cap cut" only)
	// Hills: the forested hills round Oak Hill (Hills.h, 2026-11-16), added to the land before the rivers so their channels stay:
	// 2 as built, 1 before the forest roads are levelled (what RoadLines routes on), 0 none (the roads' base)
	// bPads: the building pads (ApplyPads) on top; only on the finished ground (Hills 2)
	// The land before the open world's grid, the sea and the rivers: the original square blending into the world's land, with Oak Hill's
	// hills as built (JeebOpen's grid is worked out on this)
	inline float LandBeforeWater(float X, float Y)
	{
		const float Cheb = FMath::Max(FMath::Abs(X), FMath::Abs(Y));
		float H;
		if (Cheb < 12000.f) H = HomeHeight(X, Y);
		else if (Cheb >= GroundHalfForBlend - 400.f) H = JeebWorld::LandHeight(X, Y);
		else H = FMath::Lerp(HomeHeight(X, Y), JeebWorld::LandHeight(X, Y), FMath::SmoothStep(12000.f, GroundHalfForBlend - 400.f, Cheb));
		return H + JeebHills::Offset(X, Y);
	}
	// (bOpen false: without JeebOpen's grid, which only the full land takes, Hills == 2. Oak Hill's hills and roads are worked out
	// without it, so they stay exactly as they were)
	inline float HeightAt(float X, float Y, bool bCap, int32 Hills, bool bPads, bool bOpen)
	{
		const float Cheb = FMath::Max(FMath::Abs(X), FMath::Abs(Y));
		float H;
		if (Cheb < 12000.f) H = HomeHeight(X, Y);
		else if (Cheb >= GroundHalfForBlend - 400.f) H = JeebWorld::LandHeight(X, Y);
		else H = FMath::Lerp(HomeHeight(X, Y), JeebWorld::LandHeight(X, Y), FMath::SmoothStep(12000.f, GroundHalfForBlend - 400.f, Cheb));
		if (Hills == 2) { H += JeebHills::Offset(X, Y); if (bOpen) H += JeebOpen::Offset(X, Y); }
		else if (Hills == 1) H += JeebHills::Raw(X, Y);
		float RiverD = 1.0e9f, RiverHalf = 600.f;
		H = JeebWorld::SeaAndRivers(X, Y, H, &RiverD, &RiverHalf);
		// nothing steeper than a walk up from any river (2026-11-15, WorldMap.h UnderBankCap); in Oak Hill the cap trims its hills
		// before its level plots go on (OakHill.h Apply), so no building's ground is cut
		if (JeebOakHill::Covers(X, Y)) H = JeebOakHill::Apply(X, Y, H, RiverD, RiverHalf, bCap);   // Oak Hill's own hills and level plots (2026-11-11)
		else if (JeebSteele::Covers(X, Y)) H = JeebSteele::Apply(X, Y, H, RiverD, RiverHalf, bCap);   // Steele Creek's gentle tilt and level pads (2026-11-18)
		else if (bCap) H = JeebWorld::UnderBankCap(X, Y, H, RiverD);
		if (bPads && Hills == 2) H = ApplyPads(X, Y, H);   // every building's level pad and doorstep (2026-11-18)
		if (JeebBridge::InBox(X, Y)) H = JeebBridge::ApplyGround(X, Y, H);   // the bridge's embankments and pier footings (Bridge.cpp)
		if (bCap && bPads && Hills == 2 && JeebOakHill::Covers(X, Y)) H += RoadEaseDelta(X, Y);   // no road in Oak Hill steeper than a walk (RoadSlope.cpp, 2026-11-20)
		return JeebTownLab::Active() ? JeebTownLab::Apply(X, Y, H) : H;   // (the TownLab map only: the rebuilt Oak Hill 300 m east, TownLab.h)
	}

	// The height of the ground mesh as actually drawn (GrassField: a 200 cm grid, each cell split along its B-C diagonal).
	// Between grid points the mesh is flat triangles, which can sit a little above or below HeightAt; anything that must
	// lie exactly on the ground (the roads) uses this instead.
	constexpr float GroundHalf = 16000.f;
	constexpr float GroundStep = 200.f;
	inline float GroundMeshHeight(float X, float Y)
	{
		const float GX = (X + GroundHalf) / GroundStep, GY = (Y + GroundHalf) / GroundStep;
		const float I = FMath::FloorToFloat(GX), J = FMath::FloorToFloat(GY);
		const float Fx = GX - I, Fy = GY - J;
		const float X0 = -GroundHalf + I * GroundStep, Y0 = -GroundHalf + J * GroundStep;
		const float A = HeightAt(X0, Y0), B = HeightAt(X0 + GroundStep, Y0), C = HeightAt(X0, Y0 + GroundStep);
		if (Fx + Fy <= 1.f) return A + (B - A) * Fx + (C - A) * Fy;            // triangle A, C, B
		const float D = HeightAt(X0 + GroundStep, Y0 + GroundStep);
		return D + (C - D) * (1.f - Fx) + (B - D) * (1.f - Fy);                 // triangle B, C, D
	}
	// The world's 8 m ground mesh as drawn outside the square and Oak Hill (AWorldTerrain::BuildLand: each cell split along its B-C
	// diagonal, as above)
	inline float WorldMeshHeight(float X, float Y)
	{
		const float S = JeebWorld::WorldStep;
		const float GX = (X - JeebWorld::MinX) / S, GY = (Y - JeebWorld::MinY) / S;
		const float I = FMath::FloorToFloat(GX), J = FMath::FloorToFloat(GY);
		const float Fx = GX - I, Fy = GY - J;
		const float X0 = JeebWorld::MinX + I * S, Y0 = JeebWorld::MinY + J * S;
		const float A = HeightAt(X0, Y0), B = HeightAt(X0 + S, Y0), C = HeightAt(X0, Y0 + S);
		if (Fx + Fy <= 1.f) return A + (B - A) * Fx + (C - A) * Fy;
		const float D = HeightAt(X0 + S, Y0 + S);
		return D + (C - D) * (1.f - Fx) + (B - D) * (1.f - Fy);
	}
	// The ground as drawn anywhere: Oak Hill's own 1 m ground (HeightAt), the original square's 2 m mesh, else the world's 8 m mesh
	inline float DrawnHeight(float X, float Y)
	{
		if (JeebOakHill::Enabled() && JeebOakHill::InBox(X, Y)) return HeightAt(X, Y);
		if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) < GroundHalf) return GroundMeshHeight(X, Y);
		if (const FFinePatch* P = FinePatchAt(X, Y)) return FineMeshHeight(*P, X, Y);   // (Steele Creek, the lake, the split: 2 m)
		return WorldMeshHeight(X, Y);
	}

	// (2026-11-21) DrawnHeight with the meshes' grid-point heights read through Ht (the grass caches them per square). The world's
	// 8 m mesh sits far off HeightAt on cut roads and river banks; grass put at HeightAt floated over it (owner's picture by the river).
	template <typename FHt> inline float DrawnHeightWith(float X, float Y, FHt Ht)
	{
		if (JeebOakHill::Enabled() && JeebOakHill::InBox(X, Y)) return Ht(X, Y);
		float OX, OY, S;
		const FFinePatch* P = nullptr;
		if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) < GroundHalf) { OX = OY = -GroundHalf; S = GroundStep; }
		else if ((P = FinePatchAt(X, Y)) != nullptr) { OX = P->X0; OY = P->Y0; S = FineStep; }
		else { OX = JeebWorld::MinX; OY = JeebWorld::MinY; S = JeebWorld::WorldStep; }
		auto V = [&](float A, float B) { return P ? EdgeVertexZWith(A, B, CoarseBesideFine, Ht) : Ht(A, B); };
		const float GX = (X - OX) / S, GY = (Y - OY) / S;
		const float I = FMath::FloorToFloat(GX), J = FMath::FloorToFloat(GY);
		const float Fx = GX - I, Fy = GY - J;
		const float X0 = OX + I * S, Y0 = OY + J * S;
		const float B = V(X0 + S, Y0), C = V(X0, Y0 + S);
		if (Fx + Fy <= 1.f) { const float A = V(X0, Y0); return A + (B - A) * Fx + (C - A) * Fy; }
		const float D = V(X0 + S, Y0 + S);
		return D + (C - D) * (1.f - Fx) + (B - D) * (1.f - Fy);
	}

	// 0..1 map of where the woods are. Low values are open valleys and meadows.
	inline float WoodsDensity(float X, float Y)
	{
		const float V = 0.6f * FMath::Sin(X * 0.00052f + 1.3f) * FMath::Sin(Y * 0.00047f + 0.4f)
		              + 0.4f * FMath::Sin((X + Y) * 0.00085f + 2.1f) * FMath::Cos((X - Y) * 0.0006f)
		              + 0.25f * FMath::Sin(X * 0.0016f) * FMath::Sin(Y * 0.0013f);
		return 0.5f + 0.5f * V / 1.25f;
	}

	// Where pines like to grow
	inline float PineZone(float X, float Y)
	{
		return 0.5f + 0.5f * FMath::Sin(X * 0.00031f + 4.f) * FMath::Cos(Y * 0.00037f + 1.f);
	}

	// ------------------------------------------------------------------ paths
	struct FPathDef
	{
		TArray<FVector2D> Pts; // already smoothed into a dense list of points
		float Width = 260.f;
		bool bClosed = false;
		// Half the road's width on its left and right at each point. Each side wanders on its own, and here and there
		// the grass has crept in and the road narrows to about 60% (Width is the usual, full width).
		TArray<float> HalfL, HalfR;
		// Where it lies and its widest half: a point further than this from the box can skip the road (2026-11-11, PathClearance)
		FBox2D Bounds = FBox2D(ForceInit);
		float MaxHalf = 0.f;
		uint8 Kind = 0;   // 0 the original square's packed earth, 1 Oak Hill's cobbled streets, 2 the forest's dirt roads round Oak Hill (OakForest.h)
	};

	// Fills HalfL / HalfR from the distance along the road (Seed makes each road different)
	inline void ShapeRoadEdges(FPathDef& P, int32 Seed)
	{
		FRandomStream R(Seed * 7919 + 13);
		const float Ph[6] = { R.FRandRange(0.f, 6.28f), R.FRandRange(0.f, 6.28f), R.FRandRange(0.f, 6.28f), R.FRandRange(0.f, 6.28f), R.FRandRange(0.f, 6.28f), R.FRandRange(0.f, 6.28f) };
		float Total = 0.f;
		for (int32 i = 1; i < P.Pts.Num(); ++i) Total += FVector2D::Distance(P.Pts[i], P.Pts[i - 1]);
		const bool bVary = Total > 1500.f && P.Width <= 300.f;   // yards and the plaza keep their full width
		float S = 0.f;
		for (int32 i = 0; i < P.Pts.Num(); ++i)
		{
			if (i > 0) S += FVector2D::Distance(P.Pts[i], P.Pts[i - 1]);
			float Over = 0.f, WobL = 0.f, WobR = 0.f;
			if (bVary)
			{
				const float N = 0.5f + 0.5f * (0.6f * FMath::Sin(S / 1700.f + Ph[0]) + 0.4f * FMath::Sin(S / 690.f + Ph[1]));
				Over = FMath::SmoothStep(0.62f, 0.92f, N);                                     // overgrown stretches
				WobL = 0.07f * FMath::Sin(S / 330.f + Ph[2]) + 0.035f * FMath::Sin(S / 110.f + Ph[3]);
				WobR = 0.07f * FMath::Sin(S / 290.f + Ph[4]) + 0.035f * FMath::Sin(S / 125.f + Ph[5]);
			}
			const float Base = P.Width * 0.5f * (1.f - 0.40f * Over);
			P.HalfL.Add(Base * (1.f + WobL));
			P.HalfR.Add(Base * (1.f + WobR));
		}
	}

	inline TArray<FVector2D> SmoothPath(const TArray<FVector2D>& C, bool bClosed)
	{
		TArray<FVector2D> Out;
		const int32 N = C.Num();
		auto At = [&](int32 i) -> FVector2D
		{
			if (bClosed) return C[((i % N) + N) % N];
			return C[FMath::Clamp(i, 0, N - 1)];
		};
		const int32 Segs = bClosed ? N : N - 1;
		for (int32 i = 0; i < Segs; ++i)
		{
			const FVector2D P0 = At(i - 1), P1 = At(i), P2 = At(i + 1), P3 = At(i + 2);
			const int32 Steps = FMath::Max(2, FMath::CeilToInt(FVector2D::Distance(P1, P2) / 60.f));
			for (int32 s = 0; s < Steps; ++s)
			{
				const float T = float(s) / Steps, T2 = T * T, T3 = T2 * T;
				Out.Add(0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3));
			}
		}
		if (!bClosed) Out.Add(C.Last());
		return Out;
	}

	inline const TArray<FPathDef>& PathDefs();
	// Near any of the forest's roads (a box test: the fill marks roads only where it asks for them)
	inline bool ForestRoadNear(const FBox2D& Box)
	{
		for (const FPathDef& P : PathDefs()) if (P.Kind == 2 && P.Bounds.ExpandBy(P.MaxHalf + 100.f).Intersect(Box)) return true;
		return false;
	}
	inline const TArray<FPathDef>& PathDefs()
	{
		static const TArray<FPathDef> Defs = []()
		{
			TArray<FPathDef> D;
			const FVector2D VC = VillageCenter();
			const float H = VillageHalf();
			auto Add = [&](std::initializer_list<FVector2D> Pts, float Width, bool bClosed)
			{
				FPathDef P;
				P.Pts = SmoothPath(TArray<FVector2D>(Pts), bClosed);
				P.Width = Width;
				P.bClosed = bClosed;
				ShapeRoadEdges(P, D.Num());
				D.Add(P);
			};
			// Around the whole map
			Add({ {5300, 200}, {4300, 3500}, {1500, 5200}, {-2200, 5000}, {-5300, 3200}, {-5600, -300}, {-4600, -3400}, {-1500, -5300}, {2500, -4800}, {4900, -2500} }, 300.f, true);
			// Start to the village gate, and on to the plaza
			Add({ {0, 0}, {-500, -150}, {VC.X + H, VC.Y}, {VC.X, VC.Y} }, 260.f, false);
			// Village north-south street, then on to the graveyard
			Add({ {VC.X, VC.Y - 1000.f}, {VC.X, VC.Y}, {VC.X, VC.Y + H} }, 220.f, false);
			Add({ {VC.X, VC.Y + H}, {-2650, 1950}, {-2300, 2050} }, 240.f, false);
			// Village to the redwoods
			Add({ {VC.X, VC.Y + H}, {-3900, 2900}, {-5300, 3200} }, 240.f, false);
			Add({ {-5300, 3200}, {-5200, 4000} }, 240.f, false);
			// Start to the cottage door, and a branch out to the ring road
			Add({ {0, 0}, {330, -350}, {1250, -1330} }, 260.f, false);
			Add({ {330, -350}, {500, -1800}, {1500, -3200}, {2500, -4800} }, 260.f, false);
			// Start to the east
			Add({ {0, 0}, {1500, 900}, {3000, 1100}, {5300, 200} }, 260.f, false);
			// The market yard in front of the store and smithy, joined to the road from the start
			Add({ {-720, -500}, {440, -500} }, 360.f, false);
			Add({ {-420, -130}, {-420, -500} }, 240.f, false);
			// A branch of the cottage road to the pond's pier
			Add({ {480, -1700}, {150, -2000}, {-250, -2150} }, 220.f, false);
			// From the ring road to the castle bridge, and on through the gate to the keep door
			{
				const FVector2D CP = CastlePos();
				Add({ {4900, -2500}, {5100, -2900}, {CP.X - 2000.f, CP.Y} }, 260.f, false);
				Add({ {CP.X - 1260.f, CP.Y}, {CP.X - 340.f, CP.Y} }, 260.f, false);
			}
			// Village plaza (a wide patch of packed earth)
			Add({ {VC.X - 330.f, VC.Y}, {VC.X + 330.f, VC.Y} }, 660.f, false);
			// Oak Hill's streets (2026-11-11; always after every road above, so their shapes never change): already dense, 5 m wide
			for (const JeebOakHill::FRoad& R : JeebOakHill::Roads())
			{
				FPathDef P;
				P.Pts = R.Pts;
				P.Width = JeebOakHill::RoadWidth;
				P.bClosed = R.bClosed;
				P.Kind = 1;
				ShapeRoadEdges(P, D.Num());
				D.Add(P);
			}
			// The forest's dirt roads out of Oak Hill (2026-11-16, OakForest.cpp; always last): each shaped whole, then cut into pieces of
			// about 100 m so PathClearance's quick box test still skips the far ones
			for (const TArray<FVector2D>& Line : JeebOakForest::RoadLines())
			{
				FPathDef Whole;
				Whole.Pts = Line;
				Whole.Width = 300.f;
				ShapeRoadEdges(Whole, D.Num());
				for (int32 s = 0; s + 1 < Line.Num(); s += 160)
				{
					FPathDef P;
					P.Width = 300.f;
					P.Kind = 2;
					for (int32 i = s; i <= FMath::Min(s + 160, Line.Num() - 1); ++i) { P.Pts.Add(Whole.Pts[i]); P.HalfL.Add(Whole.HalfL[i]); P.HalfR.Add(Whole.HalfR[i]); }
					D.Add(P);
				}
			}
			// Steele Creek's dirt streets and its road north to the fork (2026-11-18, SteeleCreek.h; after the forest's roads, so theirs
			// keep their shapes): forest roads too (Kind 2), cut into pieces of about 100 m the same way
			for (const JeebSteele::FStreet& St : JeebSteele::Streets())
			{
				FPathDef Whole;
				Whole.Pts = St.Pts;
				Whole.Width = St.Width;
				ShapeRoadEdges(Whole, D.Num());
				for (int32 s = 0; s + 1 < St.Pts.Num(); s += 160)
				{
					FPathDef P;
					P.Width = St.Width;
					P.Kind = 2;
					for (int32 i = s; i <= FMath::Min(s + 160, St.Pts.Num() - 1); ++i) { P.Pts.Add(Whole.Pts[i]); P.HalfL.Add(Whole.HalfL[i]); P.HalfR.Add(Whole.HalfR[i]); }
					D.Add(P);
				}
			}
			// The roads to the stone bridge (2026-11-18, Bridge.cpp; always last, so no older road's shape changes): Kind 3 the approach from the old village site, Kind 4 the span
			for (const JeebBridge::FRoad& BR : JeebBridge::Roads())
			{
				FPathDef P;
				P.Pts = SmoothPath(BR.Pts, false);
				P.Width = BR.Width;
				P.Kind = BR.Kind;
				ShapeRoadEdges(P, D.Num());
				D.Add(P);
			}
			// The open world's roads between the towns (2026-11-21, OpenWorld.cpp; always last, so no older road's shape changes): forest
			// roads (Kind 2), cut into pieces of about 100 m the same way
			for (const TArray<FVector2D>& Line : JeebOpen::RoadLines())
			{
				FPathDef Whole;
				Whole.Pts = Line;
				Whole.Width = 300.f;
				ShapeRoadEdges(Whole, D.Num());
				for (int32 s = 0; s + 1 < Line.Num(); s += 160)
				{
					FPathDef P;
					P.Width = 300.f;
					P.Kind = 2;
					for (int32 i = s; i <= FMath::Min(s + 160, Line.Num() - 1); ++i) { P.Pts.Add(Whole.Pts[i]); P.HalfL.Add(Whole.HalfL[i]); P.HalfR.Add(Whole.HalfR[i]); }
					D.Add(P);
				}
			}
			for (FPathDef& P : D)
			{
				P.Bounds = FBox2D(P.Pts);
				P.MaxHalf = P.Width * 0.5f;
				for (float W : P.HalfL) P.MaxHalf = FMath::Max(P.MaxHalf, W);
				for (float W : P.HalfR) P.MaxHalf = FMath::Max(P.MaxHalf, W);
			}
			return D;
		}();
		return Defs;
	}

	// Distance from a spot to the nearest path edge (negative = on a path)
	inline float PathClearance(float X, float Y)
	{
		const FVector2D P(X, Y);
		float Best = 1.0e9f;
		for (const FPathDef& Def : PathDefs())
		{
			// (a road whose whole box is further away than the best so far cannot be nearer: the same answer, much quicker)
			if (Best < 1.0e8f && FMath::Sqrt(Def.Bounds.ComputeSquaredDistanceToPoint(P)) - Def.MaxHalf > Best) continue;
			const int32 N = Def.Pts.Num();
			const int32 Segs = Def.bClosed ? N : N - 1;
			for (int32 i = 0; i < Segs; ++i)
			{
				const FVector2D A = Def.Pts[i], B = Def.Pts[(i + 1) % N];
				const FVector2D AB = B - A;
				const float L2 = FMath::Max(AB.SizeSquared(), 0.001f);
				const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.f, 1.f);
				const bool bLeft = (AB.X * (P.Y - A.Y) - AB.Y * (P.X - A.X)) > 0.f;   // which side of the road the spot is on
				const TArray<float>& Half = bLeft ? Def.HalfL : Def.HalfR;
				const float W = Half.Num() == N ? FMath::Lerp(Half[i], Half[(i + 1) % N], T) : Def.Width * 0.5f;
				const float D = FVector2D::Distance(P, A + AB * T) - W;
				if (D < Best) Best = D;
			}
		}
		return Best;
	}

	// The closest point on any path
	inline FVector2D NearestPathPoint(float X, float Y)
	{
		const FVector2D P(X, Y);
		float Best = 1.0e9f;
		FVector2D BestPt = P;
		for (const FPathDef& Def : PathDefs())
		{
			if (Best < 1.0e8f && Def.Bounds.ComputeSquaredDistanceToPoint(P) > Best * Best) continue;
			const int32 N = Def.Pts.Num();
			const int32 Segs = Def.bClosed ? N : N - 1;
			for (int32 i = 0; i < Segs; ++i)
			{
				const FVector2D A = Def.Pts[i], B = Def.Pts[(i + 1) % N];
				const FVector2D AB = B - A;
				const float L2 = FMath::Max(AB.SizeSquared(), 0.001f);
				const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.f, 1.f);
				const FVector2D Q = A + AB * T;
				const float D = FVector2D::Distance(P, Q);
				if (D < Best) { Best = D; BestPt = Q; }
			}
		}
		return BestPt;
	}

	// Trees must stay off the paths, out of the village and away from huts
	inline bool TreeBlocked(float X, float Y)
	{
		const FVector2D P(X, Y);
		if (FVector2D::Distance(P, VillageCenter()) < 3100.f) return true;
		if (FVector2D::Distance(P, PenCenter()) < 1100.f) return true;
		if (FVector2D::Distance(P, PondCenter()) < PondBowl + 350.f) return true;
		if (FVector2D::Distance(P, CastlePos()) < CastleFade + 100.f) return true;
		for (const FVector2D& S : HutSites())
		{
			if (FVector2D::Distance(P, S) < 650.f) return true;
		}
		if (JeebWorld::NearRiver(X, Y, 600.f)) return true;   // (2026-11-18: the split north of Oak Hill passes the square's east side)
		return PathClearance(X, Y) < 280.f;
	}

	// ------------------------------------------------------------------ redwood groves and their tree forts
	inline const TArray<FVector2D>& RedwoodGroves()
	{
		static const TArray<FVector2D> G = { FVector2D(-5200.f, 4800.f), FVector2D(4700.f, 4700.f), FVector2D(-6700.f, -1700.f) };
		return G;
	}
	struct FRedwoodSpot { FVector2D Pos; float Scale; };
	// Each grove: one great redwood in the middle (the fort tree), six around it, and a loose ring of more outside
	inline const TArray<FRedwoodSpot>& RedwoodSpots()
	{
		static const TArray<FRedwoodSpot> All = []()
		{
			TArray<FRedwoodSpot> A;
			for (const FVector2D& C : RedwoodGroves())
			{
				A.Add(FRedwoodSpot{ C, 1.1f });
				for (int32 k = 0; k < 6; ++k)
				{
					const float Ang = FMath::DegreesToRadians(30.f + 60.f * k);
					A.Add(FRedwoodSpot{ C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * 1120.f, 0.95f + 0.04f * k });
				}
				for (int32 k = 0; k < 9; ++k)
				{
					const float Ang = k * 2.4f + C.X * 0.001f;
					const float Rad = 1900.f + 110.f * k;
					const FVector2D Pt = C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Rad;
					if (Pt.Size() > 8800.f || TreeBlocked(Pt.X, Pt.Y)) continue;   // and never out over the edge of the woods
					A.Add(FRedwoodSpot{ Pt, 0.9f + 0.03f * (k % 4) });
				}
			}
			return A;
		}();
		return All;
	}
		// Terraces up the middle tree of each grove: four square decks, every 7 m, wider at the bottom. A rope ladder
	// leads from the ground to the first deck and from each deck to the next; the top deck has the roof.
	struct FFort { FVector2D Tree; float Scale; float LadderYaw; float Height; };   // Height = the first deck above the ground
	constexpr int32 FortLevels = 4;
	constexpr float FortSpacing = 700.f;
	inline float FortHalf(int32 Level) { return 250.f + 110.f * float(FortLevels - 1 - Level); }
	// A fort tree is cut off just under the top deck (first deck 720 up, trunk planted 40 below ground), leaving an open platform on top
	inline float FortTreeHeight() { return 720.f + FortSpacing * (FortLevels - 1) + 40.f - 12.f; }
	inline const TArray<FFort>& Forts()
	{
		static const TArray<FFort> All = []()
		{
			TArray<FFort> A;
			int32 i = 0;
			for (const FVector2D& C : RedwoodGroves()) { A.Add(FFort{ C, 1.1f, 120.f * i, 720.f }); ++i; }
			return A;
		}();
		return All;
	}
	inline const TArray<FLadder>& FortLadders()
	{
		static const TArray<FLadder> All = []()
		{
			TArray<FLadder> A;
			for (const FFort& F : Forts())
			{
				const float Ground = HeightAt(F.Tree.X, F.Tree.Y);
				for (int32 N = 0; N < FortLevels; ++N)
				{
					const float Rad = FMath::DegreesToRadians(F.LadderYaw + 90.f * N);
					FLadder L;
					L.Out = FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
					L.Pos = F.Tree + L.Out * (FortHalf(N) + 12.f);
					L.Top = Ground + F.Height + FortSpacing * N;
					L.Bottom = (N == 0) ? -1.f : L.Top - FortSpacing;
					A.Add(L);
				}
			}
			return A;
		}();
		return All;
	}

	// Ladders up to the castle wall (the climbing code treats Top as the height of the walkway)
	inline const TArray<FLadder>& CastleLadders()
	{
		static const TArray<FLadder> All = []()
		{
			TArray<FLadder> A;
			const FVector2D C = CastlePos();
			const float Top = CastleLevel() + CastleWallH;
			const float D = CastleWall - 85.f;   // the player steps off the top 85 cm further on, onto the middle of the wall
			A.Add(FLadder{ C + FVector2D(D, 400.f),   FVector2D(-1.f, 0.f), Top });
			A.Add(FLadder{ C + FVector2D(-400.f, D),  FVector2D(0.f, -1.f), Top });
			A.Add(FLadder{ C + FVector2D(400.f, -D),  FVector2D(0.f, 1.f),  Top });
			return A;
		}();
		return All;
	}

	// How close a point is to a building, 0 (open country) to 1 (at a wall or inside the grounds), easing off over about
	// 7 m. Grass uses it to grow short beside buildings and taper up gradually, never tall-then-suddenly-short.
	inline float BuildingNearness(float X, float Y)
	{
		const FVector2D P(X, Y);
		float D = 1.0e6f;   // signed distance to the nearest building footprint (negative inside)
		for (const FVector2D& S : HutSites()) D = FMath::Min(D, float(FVector2D::Distance(P, S)) - 480.f);
		for (const FHutDef& H : VillageHuts()) D = FMath::Min(D, float(FVector2D::Distance(P, H.Pos)) - 480.f);
		D = FMath::Min(D, float(FVector2D::Distance(P, FVector2D(CottagePos().X, CottagePos().Y))) - 480.f);
		D = FMath::Min(D, float(FVector2D::Distance(P, CastlePos())) - (CastleMoatOut + 80.f));
		D = FMath::Min(D, RectSD(P, VillageCenter(), FVector2D(VillageHalf(), VillageHalf())));   // the whole walled village
		D = FMath::Min(D, RectSD(P, PenCenter(), PenHalf()));
		for (const FFort& F : Forts()) D = FMath::Min(D, float(FVector2D::Distance(P, F.Tree)) - 400.f);
		D = FMath::Min(D, JeebOakHill::BuildingDistance(X, Y));
		D = FMath::Min(D, JeebSteele::BuildingDistance(X, Y));
		return 1.f - FMath::SmoothStep(0.f, 700.f, D);
	}
}
