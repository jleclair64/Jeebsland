#pragma once

#include "CoreMinimal.h"

// Shared description of the land, so the ground mesh, trees, rocks, huts, paths and walls all agree.
namespace JeebTerrain
{
	// ------------------------------------------------------------------ places
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

	// The market in front of the village gate: the store and the smithy side by side, open fronts facing north (+Y).
	// ShopPos / SmithyPos are the middle of each building; the owner stands just behind the counter.
	inline FVector2D ShopPos() { return FVector2D(-600.f, -820.f); }
	inline FVector2D SmithyPos() { return FVector2D(-80.f, -820.f); }
	inline FVector2D BankPos() { return FVector2D(440.f, -820.f); }
	inline FVector2D BankNpcPos() { return BankPos() + FVector2D(0.f, 45.f); }
	inline FVector2D ShopNpcPos() { return ShopPos() + FVector2D(0.f, 45.f); }
	inline FVector2D SmithNpcPos() { return SmithyPos() + FVector2D(40.f, -10.f); }
	inline FVector2D MarketCenter() { return FVector2D(-110.f, -800.f); }
	inline FVector2D MarketHalf() { return FVector2D(790.f, 340.f); }

	// The fishing pond, south of the market. The ground dips into a bowl; the water is a flat disc a little below the rim.
	inline FVector2D PondCenter() { return FVector2D(-300.f, -2900.f); }
	constexpr float PondBowl = 900.f;       // the ground is shaped inside this radius
	constexpr float PondFade = 1250.f;      // and blends back to the hills out to here
	constexpr float PondDepth = 110.f;      // how far the middle of the bowl sinks
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
	// True if a prop of this radius would touch the village wall, a corner tower, or the pig pen and its fence
	inline bool PropBlocked(float X, float Y, float Radius)
	{
		const FVector2D P(X, Y);
		if (FMath::Abs(RectSD(P, VillageCenter(), FVector2D(VillageHalf(), VillageHalf()))) < Radius + 170.f) return true;
		if (RectSD(P, PenCenter(), PenHalf()) < Radius + 130.f) return true;
		if (RectSD(P, MarketCenter(), MarketHalf()) < Radius + 40.f) return true;
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
		static const TArray<FHutDef> All = []()
		{
			TArray<FHutDef> A;
			const FVector2D VC = VillageCenter();
			A.Add(FHutDef{ VC + FVector2D(-900.f,  1400.f), -90.f });
			A.Add(FHutDef{ VC + FVector2D( 900.f,  1400.f), -90.f });
			A.Add(FHutDef{ VC + FVector2D(-900.f, -1400.f),  90.f });
			A.Add(FHutDef{ VC + FVector2D( 900.f, -1400.f),  90.f });
			A.Add(FHutDef{ VC + FVector2D(-1300.f,    0.f),   0.f });
			return A;
		}();
		return All;
	}

	// Ladders up to the walkway. Pos is the ladder itself; Out points away from the wall (the side you climb from).
	struct FLadder { FVector2D Pos; FVector2D Out; float Top = -1.f; float Bottom = -1.f; };   // Bottom > 0: the ladder starts from a deck at this height, not the ground   // Top < 0: the village walkway; otherwise the height of the deck
	inline const TArray<FLadder>& Ladders()
	{
		static const TArray<FLadder> All = []()
		{
			TArray<FLadder> A;
			const FVector2D VC = VillageCenter();
			const float H = VillageHalf();
			const float In = 205.f; // the ladder stands just inside the walkway's edge
			A.Add(FLadder{ VC + FVector2D(H - In, 1000.f),  FVector2D(-1.f, 0.f) }); // east wall
			A.Add(FLadder{ VC + FVector2D(1550.f, H - In),  FVector2D(0.f, -1.f) }); // north wall
			A.Add(FLadder{ VC + FVector2D(-(H - In), -1000.f), FVector2D(1.f, 0.f) }); // west wall
			A.Add(FLadder{ VC + FVector2D(1550.f, -(H - In)), FVector2D(0.f, 1.f) }); // south wall
			return A;
		}();
		return All;
	}

	// Benches around the village fountain: where you can sit (position, and the direction you face)
	struct FSeat { FVector2D Pos; float Yaw; };
	inline const TArray<FSeat>& Seats()
	{
		static const TArray<FSeat> All = []()
		{
			TArray<FSeat> A;
			const FVector2D VC = VillageCenter();
			A.Add(FSeat{ VC + FVector2D(0.f, 440.f), -90.f });
			A.Add(FSeat{ VC + FVector2D(0.f, -440.f), 90.f });
			A.Add(FSeat{ VC + FVector2D(440.f, 0.f), 180.f });
			A.Add(FSeat{ VC + FVector2D(-440.f, 0.f), 0.f });
			return A;
		}();
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
		const FVector2D Stalls[3] = { ShopPos(), SmithyPos(), BankPos() };
		for (const FVector2D& St : Stalls) if (RectSD(P, St, FVector2D(270.f, 260.f)) < 0.f) return true;
		if (FVector2D::Distance(P, CastlePos()) < CastleMoatOut + 80.f) return true;
				if (FVector2D::Distance(P, PondCenter()) < PondWaterR) return true;
		
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

	// The ground, including the pond bowl
	inline float HeightAt(float X, float Y)
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
}
