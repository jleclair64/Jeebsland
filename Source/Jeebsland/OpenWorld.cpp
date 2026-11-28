#include "OpenWorld.h"
#include "Terrain.h"
#include "OakForest.h"
#include "Misc/Crc.h"
#include "JeebTuning.h"
#include "Algo/Reverse.h"
#include "SilverPoint.h"

// The open world's land and roads (OpenWorld.h). Every number that shapes them is here; log JEEBOPEN.
namespace
{
	using JeebWorld::Smooth;
	constexpr float Step = JeebWorld::WorldStep;
	const FVector2D Plaza(JeebOakHill::PosX, JeebOakHill::PosY), SteeleC(JeebSteele::PosX, JeebSteele::PosY);

	float Perlin(const FVector2D& P, float Scale, float Ox, float Oy) { return FMath::PerlinNoise2D(FVector2D(P.X / Scale + Ox, P.Y / Scale + Oy)); }
	float SegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
		return FVector2D::Distance(P, A + AB * T);
	}
	// every noise is read through a two-level warp, so outlines bend and fold instead of running in straight even swells (as JeebHills)
	FVector2D Warp(const FVector2D& P)
	{
		const FVector2D A(Perlin(P, 24000.f, 1.7f, -2.3f), Perlin(P, 24000.f, -5.9f, 4.1f));
		const FVector2D Q = P + 6000.f * A;
		return Q + 2400.f * FVector2D(Perlin(Q, 9000.f, 2.2f, -1.4f), Perlin(Q, 9000.f, -3.8f, 7.2f));
	}

	// ------------------------------------------------------------------ where the grid is zero
	// Oak Hill's hills, Steele Creek's and the land between them are JeebHills' (Hills.cpp: rims at 470-620 m, 360-480 m and 200-320 m).
	// This grid starts past the forest roads' ends (about 515-530 m from the plaza) and is whole 150 m further out.
	float HillZone(const FVector2D& P)
	{
		return FMath::Max3(Smooth(70000.f, 55000.f, float(FVector2D::Distance(P, Plaza))), Smooth(52000.f, 40000.f, float(FVector2D::Distance(P, SteeleC))), Smooth(36000.f, 24000.f, SegDist(P, Plaza, SteeleC)));
	}
	// the level town sites (WorldMap.h Towns: the land is already a level disc there)
	float TownKeep(const FVector2D& P)
	{
		float K = 1.f;
		for (const JeebWorld::FTown& T : JeebWorld::Towns())
			if (T.Fade > 0.f) K *= Smooth(T.Flat, T.Fade, float(FVector2D::Distance(P, T.Pos)));
		return K;
	}
	float KeepAt(float X, float Y)
	{
		const FVector2D P(X, Y);
		const float Open = 1.f - HillZone(P);
		if (Open <= 0.f) return 0.f;
		// (2026-11-25, the sea cliffs, MapRework.h: the fade to nothing over the last 15 m before the coast line left the old coastal range
		// standing as a rim along the cliffs' lip, half cancelled; where there is a cliff the grid carries on to 15 m past the coast line)
		const float Shore = JeebWorld::CoastY(X) - Y;
		const float CoastKeep = FMath::Lerp(Smooth(0.f, 1500.f, Shore), Smooth(-3000.f, -1500.f, Shore), JeebRework::SeaCliffAt(X).A);
		return Open * TownKeep(P) * Smooth(1500.f, 12000.f, FMath::Max(FMath::Abs(X), FMath::Abs(Y)) - JeebTerrain::GroundHalf)
			* CoastKeep;   // (never the sea floor or the islands)
	}
	// a town site's middle: the town's spot, moved off the water if it lies on a bank (Anchorage's is 22 m from the east branch's middle)
	FVector2D SiteMid(const FVector2D& Town)
	{
		FVector2D P = Town;
		for (int32 k = 0; k < 40 && JeebWorld::WaterEdge(P.X, P.Y) < 3500.f; ++k)
		{
			const FVector2D G(JeebWorld::WaterEdge(P.X + 200.f, P.Y) - JeebWorld::WaterEdge(P.X - 200.f, P.Y), JeebWorld::WaterEdge(P.X, P.Y + 200.f) - JeebWorld::WaterEdge(P.X, P.Y - 200.f));
			if (G.IsNearlyZero()) break;
			P += G.GetSafeNormal() * 200.f;
		}
		return P;
	}
	// the old range's share of the land there (WorldMap.h LandHeight)
	float OldRange(const FVector2D& P)
	{
		const float M = JeebWorld::MountainMask(P);
		return M > 0.001f ? FMath::Pow(M, 1.6f) * (JeebWorld::MountainBase + JeebWorld::MountainRise * JeebWorld::Ridged(P)) : 0.f;
	}

	// ------------------------------------------------------------------ the shape
	// The northern range (cm): its foot runs east-west about 610 m north of the world's origin (wandering 45 m either way), the land climbs
	// over about 300 m to the divide and stays high to the map's north edge. Along the divide a slow noise makes summits (up to
	// RangeBase + RangeSummit) and saddles (RangeBase). Spurs and gullies run down its sides, a band of foothills stands in front of it,
	// and it opens into a broad trough where the river runs (none within 15 m of the water, whole about 150 m out) and falls away to the coast.
	constexpr float NorthFrom = 50000.f, NorthTo = 60000.f;
	constexpr float RangeFoot = 61000.f, RangeFootWander = 4500.f, RangeRun = 30000.f;
	constexpr float RangeBase = 5000.f, RangeSummit = 6200.f;   // (the rounding takes some metres off every top: JEEBOPEN logs the highest ground, about 100 m)
	constexpr float SpurH = 1500.f, GullyH = 1000.f, Foothill = 2300.f;
	// Rolling country elsewhere (cm): up to about RollBig + RollMid where a 500 m noise says hill country, a few metres on the plains
	constexpr float RollBig = 1500.f, RollMid = 500.f, RollSmall = 130.f;
	// Rock bands (cliffs) on the range: the height is stepped into sloping ledges with short risers, where the slope cut is eased to CliffSlope
	constexpr float CliffStep = 2200.f, CliffRiser = 0.25f, CliffSlope = 2.0f;
	constexpr float MaxSlope = 0.7f;   // 35 degrees: the steepest the ground may be anywhere else (the knight walks up to 44.8)
	float Terrace(float H)
	{
		const float U = H / CliffStep, Fl = FMath::FloorToFloat(U), F = U - Fl;
		const float R = FMath::Clamp((F - (1.f - CliffRiser)) / CliffRiser, 0.f, 1.f);
		return CliffStep * (Fl + 0.25f * FMath::Min(F / (1.f - CliffRiser), 1.f) + 0.75f * R * R * (3.f - 2.f * R));
	}

	// cm to add to the land at a point, before the slope cut, the rounding and the roads; OutCliff 0 .. 1 where a rock band stands
	float Raw(float X, float Y, float& OutCliff)
	{
		OutCliff = 0.f;
		const float Keep = KeepAt(X, Y);
		if (Keep <= 0.f) return 0.f;
		const FVector2D P(X, Y);
		const FVector2D Q = Warp(P);
		const float We = JeebWorld::WaterEdge(X, Y);        // cm past the nearest water's edge (1e9 far from any)
		const float Shore = JeebWorld::CoastY(X) - Y;       // cm inland of the coast
		float V = 0.f, InRange = 0.f;
		const float North = Smooth(NorthFrom, NorthTo, X);
		if (North > 0.f)
		{
			const float Foot = RangeFoot + RangeFootWander * Perlin(FVector2D(Y, 0.f), 42000.f, 3.7f, 1.1f);
			const float U = (Q.X - Foot) / RangeRun;
			const float Env = Smooth(0.f, 1.f, U);
			// (summits where a slow noise says, and always a second massif east of the river, so the range stands on both sides of the valley)
			const float Summit = FMath::Max(Smooth(-0.35f, 0.45f, Perlin(Q, 34000.f, 6.2f, -1.4f)), Smooth(30000.f, 9000.f, float(FVector2D::Distance(P, FVector2D(90000.f, 64000.f)))));
			// (ridges: a rounded ridged noise gives the range distinct ridge lines with valleys between, not one dome)
			const float Ridge = Smooth(0.f, 1.f, 1.f - FMath::Abs(Perlin(Q, 24000.f, 2.4f, 6.6f)));
			float H = FMath::Pow(Env, 1.3f) * (RangeBase + RangeSummit * Summit) * (0.86f + 0.14f * Perlin(Q, 15000.f, -3.1f, 8.4f)) * (0.6f + 0.4f * Ridge);
			const float Spur = FMath::Square(FMath::Max(0.f, 1.f - 1.9f * FMath::Abs(Perlin(Q, 11000.f, -6.4f, 3.3f))));
			H += SpurH * Spur * Env * (0.4f + 0.6f * Summit);
			const float Gully = FMath::Max(0.f, 1.f - 3.5f * FMath::Abs(Perlin(P + 0.35f * (Q - P), 6500.f, 2.9f, -7.1f)));
			H -= GullyH * Gully * Gully * Smooth(0.15f, 0.6f, Env);
			H += 280.f * Perlin(Q, 3600.f, 1.9f, 4.4f) * Env;
			// the foothills: a band of 15-30 m hills in front of the range
			const float Band = Smooth(Foot - 16000.f, Foot - 5000.f, Q.X) * (1.f - Smooth(0.2f, 0.7f, U));
			H += Band * (Foothill * Smooth(-0.15f, 0.5f, Perlin(Q, 13000.f, 9.3f, -2.2f)) + 500.f * (0.5f + 0.5f * Perlin(Q, 5200.f, -1.1f, 6.6f)));
			// the wall along the map's west edge, as the old land had it
			const float EdgeW = Smooth(11000.f, 0.f, Y - JeebWorld::MinY);
			if (EdgeW > 0.f) H = FMath::Max(H, FMath::Pow(EdgeW, 1.6f) * (JeebWorld::MountainBase + JeebWorld::MountainRise * JeebWorld::Ridged(P)));
			H = FMath::Max(H, 0.f);
			H *= Smooth(1500.f, 15000.f + 4000.f * Perlin(P, 30000.f, 4.4f, 4.4f), We) * Smooth(3500.f, 16000.f, Shore);
			const float Cm = Smooth(0.12f, 0.38f, Perlin(P, 17000.f, 9.1f, 1.3f)) * Smooth(1800.f, 4000.f, H) * North;
			if (Cm > 0.f)
			{
				const float Shift = 500.f * Perlin(Q, 9000.f, -3.3f, -1.1f);
				H = FMath::Lerp(H, FMath::Max(0.f, Terrace(H + Shift) - Shift), Cm);
				OutCliff = Cm * Smooth(0.5f, 1.f, Keep);
			}
			V += North * (H - OldRange(P));
			InRange = North * FMath::Max(Smooth(0.f, 0.3f, U), Band);
		}
		const float Country = Smooth(-0.2f, 0.3f, Perlin(P, 52000.f, 2.6f, 9.4f));   // 0 plains .. 1 hill country
		float Roll = Country * (RollBig * Smooth(-0.5f, 0.6f, Perlin(Q, 23000.f, -4.2f, 0.9f)) + RollMid * (0.5f + 0.5f * Perlin(Q, 9500.f, 7.1f, 3.5f)))
			+ RollSmall * Perlin(P, 4300.f, -8.8f, 5.7f) * (0.3f + 0.7f * Country);
		Roll *= Smooth(2500.f, 11000.f, We) * Smooth(3500.f, 14000.f, Shore) * (1.f - InRange);
		V += Roll;
		return Keep * V;
	}

	// the sea, the rivers' valleys and banks over land H (what JeebTerrain::HeightAt does outside the towns)
	float Finish(float X, float Y, float H)
	{
		float Rd = 1.0e9f, Rh = 600.f;
		H = JeebWorld::SeaAndRivers(X, Y, H, &Rd, &Rh);
		return JeebWorld::UnderBankCap(X, Y, H, Rd);
	}

	// ------------------------------------------------------------------ the grid
	constexpr float RoadFlat = 400.f, RoadEaseMin = 1200.f, RoadEaseMax = 4500.f, RoadEaseBy = 3.5f;   // (the roads' corridors, as JeebHills')
	constexpr float RoadClearIn = 3500.f, RoadClearOut = 6500.f;   // no steep rock within, full steepness allowed past (cm from a road's middle)
	constexpr float OpenRoadGrade = 0.11f;   // tan 6.3 degrees: the steepest these roads climb (the forest roads round Oak Hill keep to 5: they have lower hills to cross)
	struct FRoad
	{
		TArray<FVector2D> Line;   // every 60 cm
		TArray<FVector2D> Pts;    // every ~5 m
		TArray<float> Want;       // the finished ground wanted there (cm, world)
		TArray<float> Ease;
		FBox2D Bounds = FBox2D(ForceInit);
		float Len = 0.f;
	};
	struct FGrid
	{
		int32 NI = 0, NJ = 0;
		TArray<float> V;       // the offset
		TArray<float> T;       // the finished ground on the grid points (away from the towns)
		TArray<FRoad> Roads;
		uint32 Hash = 0, RoadHash = 0;
		double Ms = 0.0;
		int32 Sloped = 0, Rounded = 0;
		float MaxCut = 0.f, MaxFill = 0.f;
		float GX(int32 i) const { return JeebWorld::MinX + i * Step; }
		float GY(int32 j) const { return JeebWorld::MinY + j * Step; }
		// a grid array laid flat across the world mesh's triangles (each cell split along its B-C diagonal, as AWorldTerrain)
		float Tri(const TArray<float>& A, float X, float Y) const
		{
			const float WX = (X - JeebWorld::MinX) / Step, WY = (Y - JeebWorld::MinY) / Step;
			const int32 i = FMath::Clamp(FMath::FloorToInt(WX), 0, NI - 2), j = FMath::Clamp(FMath::FloorToInt(WY), 0, NJ - 2);
			const float Fx = FMath::Clamp(WX - i, 0.f, 1.f), Fy = FMath::Clamp(WY - j, 0.f, 1.f);
			const float* D = A.GetData();
			const float Aa = D[i * NJ + j], B = D[(i + 1) * NJ + j], C = D[i * NJ + j + 1];
			if (Fx + Fy <= 1.f) return Aa + (B - Aa) * Fx + (C - Aa) * Fy;
			const float Dd = D[(i + 1) * NJ + j + 1];
			return Dd + (C - Dd) * (1.f - Fx) + (B - Dd) * (1.f - Fy);
		}
	};

	// ------------------------------------------------------------------ the roads' routes
	// A least-cost search over every second grid point (16 m), sixteen headings: a step costs its length times 1 + 30 x its grade squared
	// (11 degrees 2.1 times, 19 degrees 4.6 times: a road goes round a hill only when that is not much longer, since its corridor is levelled anyway), so a road follows valley floors and takes the lowest saddle;
	// never in water or within 15 m of it, on the beach, in the original square, in Oak Hill or through a Steele Creek building.
	struct FRouter
	{
		const FGrid& G;
		int32 NA, NB;
		TArray<uint8> Block;
		TArray<float> Pen;
		explicit FRouter(const FGrid& In) : G(In)
		{
			NA = (G.NI + 1) / 2; NB = (G.NJ + 1) / 2;
			Block.SetNumZeroed(NA * NB); Pen.SetNumZeroed(NA * NB);
			for (int32 a = 0; a < NA; ++a)
				for (int32 b = 0; b < NB; ++b)
				{
					const float X = G.GX(2 * a), Y = G.GY(2 * b);
					const float We = JeebWorld::WaterEdge(X, Y);
					bool bNo = a < 3 || b < 3 || a >= NA - 3 || b >= NB - 3 || We < 1500.f || JeebWorld::CoastY(X) - Y < 2000.f;
					bNo = bNo || FMath::Max(FMath::Abs(X), FMath::Abs(Y)) < JeebTerrain::GroundHalf + 800.f;
					bNo = bNo || (JeebOakHill::Enabled() && JeebOakHill::InBox(X, Y, 1000.f));
					bNo = bNo || JeebSteele::OnBuilding(X, Y, 5.f);
					Block[a * NB + b] = bNo ? 1 : 0;
					Pen[a * NB + b] = 0.6f * Smooth(5000.f, 1500.f, We);
				}
		}
		FVector2D Pos(int32 n) const { return FVector2D(G.GX(2 * (n / NB)), G.GY(2 * (n % NB))); }
		float Z(int32 n) const { return G.T[(2 * (n / NB)) * G.NJ + 2 * (n % NB)]; }
		int32 Snap(const FVector2D& P) const
		{
			const int32 a0 = FMath::RoundToInt((P.X - JeebWorld::MinX) / (2.f * Step)), b0 = FMath::RoundToInt((P.Y - JeebWorld::MinY) / (2.f * Step));
			int32 Best = -1; float BestD = 1.0e18f;
			for (int32 a = FMath::Max(0, a0 - 8); a <= FMath::Min(NA - 1, a0 + 8); ++a)
				for (int32 b = FMath::Max(0, b0 - 8); b <= FMath::Min(NB - 1, b0 + 8); ++b)
				{
					if (Block[a * NB + b]) continue;
					const float D = FVector2D::DistSquared(P, FVector2D(G.GX(2 * a), G.GY(2 * b)));
					if (D < BestD) { BestD = D; Best = a * NB + b; }
				}
			return Best;
		}
		TArray<FVector2D> Route(const FVector2D& From, const FVector2D& To) const
		{
			TArray<FVector2D> Out;
			const int32 S = Snap(From), E = Snap(To);
			if (S < 0 || E < 0) return Out;
			static const int32 Dirs[16][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1},{1,2},{1,-2},{-1,2},{-1,-2},{2,1},{2,-1},{-2,1},{-2,-1} };
			TArray<float> Cost; Cost.Init(1.0e18f, NA * NB);
			TArray<int32> From2; From2.Init(-1, NA * NB);
			struct FOpen { float F; int32 N; };
			auto Less = [](const FOpen& A, const FOpen& B) { return A.F != B.F ? A.F < B.F : A.N < B.N; };   // (ties by index: the same on every machine)
			TArray<FOpen> Heap;
			const FVector2D Goal = Pos(E);
			Cost[S] = 0.f;
			Heap.HeapPush(FOpen{ float(FVector2D::Distance(Pos(S), Goal)), S }, Less);
			while (Heap.Num())
			{
				FOpen Cur; Heap.HeapPop(Cur, Less, EAllowShrinking::No);
				if (Cur.N == E) break;
				const FVector2D Pc = Pos(Cur.N);
				if (Cur.F > Cost[Cur.N] + float(FVector2D::Distance(Pc, Goal)) + 1.f) continue;   // (an old entry)
				const int32 a = Cur.N / NB, b = Cur.N % NB;
				const float Zc = Z(Cur.N);
				for (const auto& D : Dirs)
				{
					const int32 a2 = a + D[0], b2 = b + D[1];
					if (a2 < 0 || b2 < 0 || a2 >= NA || b2 >= NB) continue;
					const int32 N2 = a2 * NB + b2;
					if (Block[N2]) continue;
					// the points a long or slanted step passes
					const int32 Sa = FMath::Sign(D[0]), Sb = FMath::Sign(D[1]);
					if (D[0] != 0 && D[1] != 0 && (Block[(a + Sa) * NB + b] || Block[a * NB + b + Sb] || Block[(a + Sa) * NB + b + Sb])) continue;
					const float L = 2.f * Step * FMath::Sqrt(float(D[0] * D[0] + D[1] * D[1]));
					const float Grade = FMath::Abs(Z(N2) - Zc) / L;
					const float C = Cost[Cur.N] + L * (1.f + 30.f * Grade * Grade + Pen[N2]);
					if (C >= Cost[N2]) continue;
					Cost[N2] = C; From2[N2] = Cur.N;
					Heap.HeapPush(FOpen{ C + float(FVector2D::Distance(Pos(N2), Goal)), N2 }, Less);
				}
			}
			if (From2[E] < 0 && E != S) return Out;
			TArray<FVector2D> Nodes;
			for (int32 N = E; N >= 0; N = From2[N]) { Nodes.Add(Pos(N)); if (N == S) break; }
			Algo::Reverse(Nodes);
			// control points: the exact ends, every third point between (48 m), eased twice toward their neighbours (never toward water)
			TArray<FVector2D> Ctrl = { From };
			for (int32 i = 2; i + 2 < Nodes.Num(); i += 3) Ctrl.Add(Nodes[i]);
			Ctrl.Add(To);
			for (int32 Pass = 0; Pass < 2; ++Pass)
				for (int32 i = 1; i + 1 < Ctrl.Num(); ++i)
				{
					const FVector2D W = Ctrl[i] + 0.3f * (0.5f * (Ctrl[i - 1] + Ctrl[i + 1]) - Ctrl[i]);
					if (JeebWorld::WaterEdge(W.X, W.Y) > 1500.f) Ctrl[i] = W;
				}
			if (Ctrl.Num() >= 3) Out = JeebTerrain::SmoothPath(Ctrl, false);
			return Out;
		}
	};

	// no slope over MaxSlope (times Allow): the ground is cut down to the lowest cone of that slope standing on any point (two sweeps each
	// way of an 8-neighbour chamfer); the points that keep their height (Fixed) only limit their neighbours
	void CutSlopes(const FGrid& G, TArray<float>& T, const TArray<float>& Allow, const TArray<uint8>& Fixed)
	{
		const int32 NI = G.NI, N = G.NJ;
		const float S1 = MaxSlope * Step, S2 = S1 * 1.41421356f;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 i = 0; i < NI; ++i)
				for (int32 j = 0; j < N; ++j)
				{
					const int32 k = i * N + j;
					if (Fixed[k]) continue;
					float& Here = T[k];
					if (i > 0) { Here = FMath::Min(Here, T[k - N] + S1 * Allow[k]); if (j > 0) Here = FMath::Min(Here, T[k - N - 1] + S2 * Allow[k]); if (j + 1 < N) Here = FMath::Min(Here, T[k - N + 1] + S2 * Allow[k]); }
					if (j > 0) Here = FMath::Min(Here, T[k - 1] + S1 * Allow[k]);
				}
			for (int32 i = NI - 1; i >= 0; --i)
				for (int32 j = N - 1; j >= 0; --j)
				{
					const int32 k = i * N + j;
					if (Fixed[k]) continue;
					float& Here = T[k];
					if (i + 1 < NI) { Here = FMath::Min(Here, T[k + N] + S1 * Allow[k]); if (j + 1 < N) Here = FMath::Min(Here, T[k + N + 1] + S2 * Allow[k]); if (j > 0) Here = FMath::Min(Here, T[k + N - 1] + S2 * Allow[k]); }
					if (j + 1 < N) Here = FMath::Min(Here, T[k + 1] + S1 * Allow[k]);
				}
		}
	}

	FGrid BuildGrid()
	{
		const double T0 = FPlatformTime::Seconds();
		FGrid G;
		G.NI = FMath::RoundToInt((JeebWorld::MaxX - JeebWorld::MinX) / Step) + 1;
		G.NJ = FMath::RoundToInt((JeebWorld::MaxY - JeebWorld::MinY) / Step) + 1;
		const int32 NI = G.NI, N = G.NJ, Num = NI * N;
		TArray<float> L0, Cliff, Allow, Keep;
		TArray<uint8> Fixed;
		L0.SetNumUninitialized(Num); Cliff.SetNumZeroed(Num); Allow.SetNumUninitialized(Num); Keep.SetNumUninitialized(Num); Fixed.SetNumZeroed(Num);
		G.V.SetNumZeroed(Num); G.T.SetNumUninitialized(Num);
		for (int32 i = 0; i < NI; ++i)
			for (int32 j = 0; j < N; ++j)
			{
				const int32 k = i * N + j;
				const float X = G.GX(i), Y = G.GY(j);
				L0[k] = JeebTerrain::LandBeforeWater(X, Y);
				Keep[k] = KeepAt(X, Y);
				G.V[k] = Raw(X, Y, Cliff[k]);
				Fixed[k] = Keep[k] < 0.01f ? 1 : 0;
				if (Fixed[k]) G.V[k] = 0.f;
				// (a town site is a level disc of the land, but Oak Hill's hills are laid on afterwards and Birch Grove lies in their rim:
				// inside each disc the hills are taken off again, so the site is level)
				const float InTown = 1.f - TownKeep(FVector2D(X, Y));
				if (InTown > 0.f) { const float Hh = JeebHills::Offset(X, Y); if (Hh != 0.f) { G.V[k] -= Hh * InTown; Fixed[k] = 0; } }
				Allow[k] = FMath::Lerp(1.f, CliffSlope / MaxSlope, Cliff[k]);
				// (2026-11-25, the sea cliffs, MapRework.h: from 65 m inland of the coast line to the sea the slope cut and the rounding leave the ground alone)
				if (JeebWorld::SeaCliffNear(X, Y, 9000.f) > 0.f || JeebRework::NearNeedles(X, Y, 2400.f)) { Cliff[k] = 1.f; Allow[k] = 80.f; }
			}
		auto Finished = [&]() { for (int32 i = 0; i < NI; ++i) for (int32 j = 0; j < N; ++j) { const int32 k = i * N + j; G.T[k] = Finish(G.GX(i), G.GY(j), L0[k] + G.V[k]); } };
		// The slope cut is made on the finished ground (rivers and coast applied) and taken off the offset; near water the land answers
		// less than one for one, so it is done twice
		auto Cut = [&](int32 Times)
		{
			TArray<float> Was;
			for (int32 t = 0; t < Times; ++t)
			{
				Finished();
				Was = G.T;
				CutSlopes(G, G.T, Allow, Fixed);
				for (int32 k = 0; k < Num; ++k) if (!Fixed[k] && G.T[k] < Was[k] - 0.5f) { G.V[k] -= Was[k] - G.T[k]; ++G.Sloped; }
			}
		};
		// Rounded tops (owner, 2026-11-22, of the hills round Oak Hill: "hills like this that end in a point ... I want them all rounded
		// off"; the same here): the ground is eased down, never up, toward its own 40 m average, so convex places (summits, ridge lines,
		// the cut's cone tips) sink and round off while valleys keep their shape. The rock bands keep their edges; RoadW keeps the roads.
		auto Round = [&](int32 Passes, const TArray<float>* RoadW)
		{
			constexpr int32 R = 2;
			TArray<float> Tmp, Avg; Tmp.SetNumUninitialized(Num); Avg.SetNumUninitialized(Num);
			for (int32 Pass = 0; Pass < Passes; ++Pass)
			{
				Finished();
				for (int32 i = 0; i < NI; ++i)
					for (int32 j = 0; j < N; ++j) { float S = 0.f; for (int32 b = -R; b <= R; ++b) S += G.T[i * N + FMath::Clamp(j + b, 0, N - 1)]; Tmp[i * N + j] = S / (2 * R + 1); }
				for (int32 i = 0; i < NI; ++i)
					for (int32 j = 0; j < N; ++j) { float S = 0.f; for (int32 a = -R; a <= R; ++a) S += Tmp[FMath::Clamp(i + a, 0, NI - 1) * N + j]; Avg[i * N + j] = S / (2 * R + 1); }
				for (int32 k = 0; k < Num; ++k)
				{
					if (Fixed[k] || Avg[k] >= G.T[k]) continue;
					const float Share = Keep[k] * (1.f - Cliff[k]) * (RoadW ? 1.f - (*RoadW)[k] : 1.f);
					if (Share <= 0.f) continue;
					G.V[k] -= (G.T[k] - Avg[k]) * Share;
					if (Pass == 0 && G.T[k] - Avg[k] > 80.f) ++G.Rounded;
				}
			}
		};
		Cut(2);
		Round(2, nullptr);
		Finished();

		// ---- the roads, routed on that ground
		{
			const FRouter Router(G);
			auto TownAt = [](const TCHAR* Name) { for (const JeebWorld::FTown& T : JeebWorld::Towns()) if (FCString::Strcmp(T.Name, Name) == 0) return SiteMid(T.Pos); return FVector2D::ZeroVector; };
			auto Add = [&](const TCHAR* What, const FVector2D& From, const FVector2D& To)
			{
				FRoad R;
				R.Line = Router.Route(From, To);
				for (int32 i = 1; i < R.Line.Num(); ++i) R.Len += FVector2D::Distance(R.Line[i], R.Line[i - 1]);
				UE_LOG(LogTemp, Log, TEXT("JEEBOPEN road %s: from %.0f %.0f to %.0f %.0f, %.0f m%s"), What, From.X, From.Y, To.X, To.Y, R.Len / 100.f, R.Line.Num() < 2 ? TEXT(" (NO ROUTE: left out)") : TEXT(""));
				if (R.Line.Num() >= 2) G.Roads.Add(MoveTemp(R));
			};
			const TArray<TArray<FVector2D>>& F = JeebOakForest::RoadLines();
			const FVector2D Birch = TownAt(TEXT("Birch Grove"));
			if (F.Num() > 0) Add(TEXT("Oak Hill north road to Silver Point"), F[0].Last(), TownAt(TEXT("Silver Point")));
			if (F.Num() > 1) Add(TEXT("Oak Hill east road to Anchorage"), F[1].Last(), TownAt(TEXT("Anchorage")));
			// Steele Creek's long road north: a fork where it passes nearest Birch Grove
			{
				const JeebSteele::FStreet* Long = nullptr;
				for (const JeebSteele::FStreet& St : JeebSteele::Streets()) if (!Long || St.Pts.Num() > Long->Pts.Num()) Long = &St;
				if (Long && Long->Pts.Num() > 800)
				{
					FVector2D Fork = Long->Pts[0]; float Best = 1.0e18f;
					for (const FVector2D& Q : Long->Pts) { const float D = FVector2D::DistSquared(Q, Birch); if (D < Best && JeebWorld::WaterEdge(Q.X, Q.Y) > 4500.f) { Best = D; Fork = Q; } }   // (not on the river bank at its north end: the fork needs room to be levelled)
					Add(TEXT("Steele Creek's road to Birch Grove"), Fork, Birch);
				}
			}
			Add(TEXT("Birch Grove to Palmer Square"), Birch, TownAt(TEXT("Palmer Square")));
			// north from Birch Grove to the river's east branch, opposite the end of Oak Hill's south-east road (no bridge or ford yet)
			if (F.Num() > 2 && F[2].Num() > 60)
			{
				const FVector2D E = F[2].Last(), Dir = (E - F[2][F[2].Num() - 50]).GetSafeNormal();
				bool bWet = false; FVector2D Across = FVector2D::ZeroVector;
				for (float D = 500.f; D < 20000.f; D += 500.f)
				{
					const FVector2D Q = E + Dir * D;
					if (JeebWorld::InWater(Q.X, Q.Y)) bWet = true;
					else if (bWet && JeebWorld::WaterEdge(Q.X, Q.Y) > 1700.f) { Across = Q; break; }
				}
				if (!Across.IsZero()) Add(TEXT("Birch Grove north to the east branch (opposite Oak Hill's south-east road)"), Birch, Across);
				else UE_LOG(LogTemp, Warning, TEXT("JEEBOPEN no far bank found past Oak Hill's south-east road: the Birch Grove north road is left out"));
			}
		}

		// ---- each road's height: the ground softened over 20 m either way, then no steeper than JeebHills::RoadGrade (halfway between the
		// highest and the lowest such line through it, so it cuts through rises and runs on a bank over dips); its ends meet the ground
		TArray<float> RoadW; RoadW.SetNumZeroed(Num);
		if (G.Roads.Num())
		{
			TArray<float> T1 = G.T;   // (the ground without the roads)
			for (FRoad& R : G.Roads)
			{
				for (int32 i = 0; i < R.Line.Num(); i += 8) R.Pts.Add(R.Line[i]);
				if (R.Pts.Last() != R.Line.Last()) R.Pts.Add(R.Line.Last());
				const int32 M = R.Pts.Num();
				TArray<float> S, Base, Tt;
				S.SetNum(M); Base.SetNum(M); Tt.SetNum(M);
				for (int32 i = 0; i < M; ++i)
				{
					S[i] = i ? S[i - 1] + float(FVector2D::Distance(R.Pts[i], R.Pts[i - 1])) : 0.f;
					Base[i] = G.Tri(T1, R.Pts[i].X, R.Pts[i].Y);
				}
				for (int32 i = 0; i < M; ++i)
				{
					float Sum = 0.f, W = 0.f;
					for (int32 k = i; k >= 0 && S[i] - S[k] < 2000.f; --k) { Sum += Base[k]; W += 1.f; }
					for (int32 k = i + 1; k < M && S[k] - S[i] < 2000.f; ++k) { Sum += Base[k]; W += 1.f; }
					Tt[i] = Sum / W;
				}
				Tt[0] = Base[0]; Tt[M - 1] = Base[M - 1];   // (the ends lie on the ground as it is: the road it carries on from, the town's level ground; the climb limit then eases the rest to them)
				TArray<float> Lo = Tt, Hi = Tt;
				for (int32 i = 1; i < M; ++i) { const float D = (S[i] - S[i - 1]) * OpenRoadGrade; Lo[i] = FMath::Min(Lo[i], Lo[i - 1] + D); Hi[i] = FMath::Max(Hi[i], Hi[i - 1] - D); }
				for (int32 i = M - 2; i >= 0; --i) { const float D = (S[i + 1] - S[i]) * OpenRoadGrade; Lo[i] = FMath::Min(Lo[i], Lo[i + 1] + D); Hi[i] = FMath::Max(Hi[i], Hi[i + 1] - D); }
				R.Want.SetNum(M); R.Ease.SetNum(M);
				for (int32 i = 0; i < M; ++i)
				{
					const float W = 0.5f * (Lo[i] + Hi[i]);
					// (and inside the cones of that climb standing on both ends, so the road arrives on the ground as it is: the road it
					// carries on from, the town's level ground. The mean alone left it up to 11 m over Birch Grove's middle)
					const float FromA = S[i] * OpenRoadGrade, FromB = (S[M - 1] - S[i]) * OpenRoadGrade;
					R.Want[i] = FMath::Clamp(FMath::Clamp(W, Base[0] - FromA, Base[0] + FromA), Base[M - 1] - FromB, Base[M - 1] + FromB);
					const float Diff = R.Want[i] - Base[i];
					G.MaxCut = FMath::Max(G.MaxCut, -Diff); G.MaxFill = FMath::Max(G.MaxFill, Diff);
					R.Ease[i] = FMath::Clamp(RoadEaseBy * FMath::Abs(Diff), RoadEaseMin, RoadEaseMax);
				}
				R.Bounds = FBox2D(R.Pts).ExpandBy(RoadClearOut + 100.f);
			}
			// each road's corridor mixed into the offset by weights that grow without end on the road (as JeebHills), so where roads meet the
			// ground stays smooth; never inside Oak Hill's or Steele Creek's own ground, the towns' level discs or by the water
			for (int32 i = 0; i < NI; ++i)
				for (int32 j = 0; j < N; ++j)
				{
					const int32 k = i * N + j;
					const FVector2D P(G.GX(i), G.GY(j));
					float Sum = G.V[k], Weight = 1.f, Clear = 1.f, Wmax = 0.f;
					for (const FRoad& R : G.Roads)
					{
						if (!R.Bounds.IsInside(P)) continue;
						float Best = 1.0e18f; int32 BestI = 0; float BestT = 0.f;
						for (int32 s = 0; s + 1 < R.Pts.Num(); ++s)
						{
							const FVector2D A = R.Pts[s], AB = R.Pts[s + 1] - A;
							const float Tq = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
							const float D2 = FVector2D::DistSquared(P, A + AB * Tq);
							if (D2 < Best) { Best = D2; BestI = s; BestT = Tq; }
						}
						const float D = FMath::Sqrt(Best);
						Clear *= Smooth(RoadClearIn, RoadClearOut, D);
						const float Ease = FMath::Lerp(R.Ease[BestI], R.Ease[BestI + 1], BestT);
						if (D >= RoadFlat + Ease) continue;
						float W = 1.f - Smooth(RoadFlat, RoadFlat + Ease, D);
						W *= Smooth(1200.f, 3000.f, JeebWorld::WaterEdge(P.X, P.Y));
						if (JeebOakHill::InBox(P.X, P.Y, 1500.f) || JeebSteele::InBox(P.X, P.Y, 500.f) || FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)) < JeebTerrain::GroundHalf + 1500.f) W = 0.f;
						if (W <= 0.f) continue;
						const float Odds = FMath::Min(W / FMath::Max(1.f - W, 1e-5f), 1.0e5f);
						Sum += Odds * (FMath::Lerp(R.Want[BestI], R.Want[BestI + 1], BestT) - L0[k]); Weight += Odds;
						Wmax = FMath::Max(Wmax, W);
					}
					G.V[k] = Sum / Weight;
					RoadW[k] = Wmax;
					if (Wmax > 0.f) Fixed[k] = 0;   // (a road's corridor is shaped even through the rim of Oak Hill's hills)
					Allow[k] = FMath::Lerp(1.f, Allow[k], Clear);   // (no steep rock by a road)
					Cliff[k] *= Clear;

				}
			Cut(1);
			Round(1, &RoadW);
		}
		// (2026-11-22, owner: "a lot of straight lines to make the hills ... it doesn't look natural") the slope cut leaves eight-sided cones
		// whose faces meet in long straight creases, and the 8 m mesh drew them as hard ridge lines. Now three passes of a 5x5 blur (about
		// 25 m across together) where the offset is free all round round every crease into a curve; the rock bands take half of it (they
		// stay steep and climbable, their edges softened), roads and the town sites none. A blur never makes ground steeper than it was.
		{
			constexpr int32 R = 2;
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				TArray<float> Was = G.V, Tmp; Tmp.SetNumUninitialized(Num);
				for (int32 i = 0; i < NI; ++i)
					for (int32 j = 0; j < N; ++j) { float S = 0.f; for (int32 b = -R; b <= R; ++b) S += Was[i * N + FMath::Clamp(j + b, 0, N - 1)]; Tmp[i * N + j] = S / (2 * R + 1); }
				for (int32 i = R; i + R < NI; ++i)
					for (int32 j = R; j + R < N; ++j)
					{
						const int32 k = i * N + j;
						if (Fixed[k]) continue;
						bool bFree = true; float S = 0.f;
						for (int32 a = -R; a <= R && bFree; ++a) { bFree &= !Fixed[k + a * N - R] && !Fixed[k + a * N + R] && !Fixed[k + a * N]; S += Tmp[k + a * N]; }
						if (!bFree) continue;
						const float Hold = FMath::Max3(Cliff[k] * 0.5f, RoadW[k], 1.f - FMath::Clamp(Keep[k] * 3.f, 0.f, 1.f));
						G.V[k] = FMath::Lerp(S / (2 * R + 1), Was[k], Hold);
					}
			}
		}
		for (int32 k = 0; k < Num; ++k) if (Fixed[k]) G.V[k] = 0.f;
		Finished();
		G.Hash = FCrc::MemCrc32(G.V.GetData(), G.V.Num() * sizeof(float));
		for (const FRoad& R : G.Roads) G.RoadHash = FCrc::MemCrc32(R.Line.GetData(), R.Line.Num() * sizeof(FVector2D), G.RoadHash);
		G.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		return G;
	}

	const FGrid& Grid()
	{
		static const FGrid G = BuildGrid();
		return G;
	}
}

float JeebOpen::Offset(float X, float Y)
{
	if (X < JeebWorld::MinX || Y < JeebWorld::MinY || X > JeebWorld::MaxX || Y > JeebWorld::MaxY) return 0.f;   // (the edge points themselves take the grid: the map's rim keeps its height)
	const FGrid& G = Grid();
	return G.Tri(G.V, X, Y);
}

// ------------------------------------------------------------------ Silver Point's mountain (2026-11-27, agent mountain; OpenWorld.h)
namespace
{
	namespace SM
	{
		using JeebOpen::SilverTop;
		constexpr float Rs = 7400.f;        // the level top's radius (the cobbles reach 64 m, the walls 60 m)
		constexpr float Re = 7700.f;        // where the road meets the village (on the Oak Hill road's old line)
		constexpr float Hb = 200.f;         // the slopes' foot (the land round it wins where it is higher)
		constexpr float RoadRun = 20000.f, SideRun = 24000.f, RoadExp = 1.0f, SideExp = 1.6f;   // (round 2: the road side can reach no further than the old road's start, the far side no further than the caves)   // the slopes: run from the top's edge to the foot, and the profile's curve
		constexpr float LegDeg = 65.f, Hairpin = 800.f, Bench = 300.f, Bank = 1.3f;   // the road: its legs' ends either side of the way in, hairpin radius, level half width, steepest bank (52 deg: round 2, the legs stacked closer on the steeper peak; the cuts between them are rock)
		constexpr float CliffFull = 42.f, CliffNone = 78.f;   // degrees from the cliff's middle (opposite the way in)
		constexpr float Lip = 8400.f;        // cm from the middle to the cliff's lip, on average
		thread_local bool tBuilding = false;

		float Ang(float A) { return FMath::UnwindRadians(A); }
		const FVector2D& Mid()
		{
			static const FVector2D C = []() { for (const JeebWorld::FTown& T : JeebWorld::Towns()) if (FCString::Strcmp(T.Name, TEXT("Silver Point")) == 0) return SiteMid(T.Pos); return FVector2D(1.0e9f, 1.0e9f); }();
			return C;
		}
		// the Oak Hill road's old line in: where it crosses the foot and the village's edge, and the bearing it comes in on
		struct FEntry { int32 Road = -1, iF = -1, iE = -1, iIn = -1; float PhiE = -0.5f * PI; };
		float RunAt(float Phi, float PhiE, float* OutExp = nullptr, float* OutRoadW = nullptr)
		{
			const float Dr = FMath::RadiansToDegrees(FMath::Abs(Ang(Phi - PhiE)));
			const float W = 1.f - Smooth(80.f, 100.f, Dr);
			if (OutExp) *OutExp = FMath::Lerp(SideExp, RoadExp, W);
			if (OutRoadW) *OutRoadW = W;
			const FVector2D D(FMath::Cos(Phi), FMath::Sin(Phi));
			return FMath::Lerp(SideRun, RoadRun, W) * (1.f + 0.1f * FMath::PerlinNoise2D(D * 1.6f + FVector2D(3.1f, -2.7f)) + 0.05f * FMath::PerlinNoise2D(D * 4.3f + FVector2D(-7.4f, 1.9f)));
		}
		const FEntry& Entry()
		{
			static const FEntry E = []()
			{
				FEntry R;
				const FGrid& G = Grid();
				const FVector2D C = Mid();
				for (int32 r = 0; r < G.Roads.Num(); ++r) if (FVector2D::Distance(G.Roads[r].Line.Last(), C) < 300.f) R.Road = r;
				if (R.Road < 0) return R;
				const TArray<FVector2D>& L = G.Roads[R.Road].Line;
				for (int32 i = 0; i < L.Num(); ++i)
				{
					const float D = FVector2D::Distance(L[i], C);
					if (R.iE < 0 && D < Re) R.iE = i;
					if (R.iIn < 0 && D < Re - 900.f) R.iIn = i;
				}
				if (R.iE < 0 || R.iIn < 0) { R.Road = -1; return R; }
				const FVector2D De = L[R.iE] - C;
				R.PhiE = FMath::Atan2(De.Y, De.X);
				for (int32 i = 0; i < R.iE; ++i)
				{
					const FVector2D D = L[i] - C;
					if (D.Size() < Rs + RunAt(FMath::Atan2(D.Y, D.X), R.PhiE) + 600.f) { R.iF = i; break; }
				}
				if (R.iF < 1) R.Road = -1;
				return R;
			}();
			return E;
		}
		// the mountain's own shape at P (world cm), before the land round it and the road; OutW how much of the ground it is, OutHollow
		// -1 .. 1 (a gully, or the shady north side: more snow), OutLedge on a cliff ledge well in from both edges
		float Shape(const FVector2D& P, float& OutW, float* OutHollow = nullptr, bool* OutLedge = nullptr)
		{
			const FEntry& En = Entry();
			const FVector2D D = P - Mid();
			const float R = D.Size(), Phi = FMath::Atan2(D.Y, D.X);
			const FVector2D Dn = D / FMath::Max(R, 1.f);
			float Exp, RoadW;
			const float Run = RunAt(Phi, En.PhiE, &Exp, &RoadW);
			OutW = 1.f - Smooth(Rs + Run - 2500.f, Rs + Run + 1500.f, R);
			if (OutLedge) *OutLedge = false;
			float H = SilverTop, Hollow = 0.f;
			if (R > Rs)
			{
				const float T = (R - Rs) / Run;
				const float Tp = FMath::Sqrt(T * T + 0.0025f) - 0.05f;   // (the top's edge rounded over, about 8 m)
				H = Hb + (SilverTop - Hb) * FMath::Pow(FMath::Max(0.f, 1.f - Tp), Exp);
				// (round 2, 2026-11-28: "a craggy, natural BotW-mountain silhouette") ridges and buttresses running down from the top at three
				// unrelated sizes (ridged noise round the peak, twisting a little with height), gullies between them, and small crags; strongest
				// half way down and gentle on the road's side
				const float Tc = FMath::Clamp(T, 0.f, 1.f), Bell = 4.f * Tc * (1.f - Tc);
				const FVector2D Q(P.X * 0.799f - P.Y * 0.602f, P.X * 0.602f + P.Y * 0.799f);
				const float N = 0.6f * Perlin(P, 4800.f, 1.3f, 7.7f) + 0.4f * Perlin(Q, 1900.f, -4.2f, 2.6f);
				const float Rg1 = 1.f - FMath::Abs(FMath::PerlinNoise2D(Dn * 2.4f + FVector2D(1.7f + 0.6f * Tc, -3.3f)));
				const float Rg2 = 1.f - FMath::Abs(FMath::PerlinNoise2D(Dn * 6.1f + FVector2D(-4.6f, 2.2f + 1.1f * Tc)));
				const float Rg3 = 1.f - FMath::Abs(Perlin(Q, 1400.f, 7.3f, 0.6f));
				const float Crag = 1.f - 0.7f * RoadW;
				const float Ridge = 2600.f * (Rg1 * Rg1 - 0.45f) + 1100.f * (Rg2 * Rg2 - 0.45f) + 350.f * (Rg3 * Rg3 - 0.45f);
				H += Bell * (FMath::Lerp(1600.f, 450.f, RoadW) * N + Crag * Ridge) + 90.f * Perlin(P, 650.f, 5.5f, -3.1f) * FMath::Min(T * 8.f, 1.f);
				Hollow = -N * Bell - Bell * Crag * (Rg1 * Rg1 - 0.45f);
			}
			// the peak's own crown: two broad snowy knolls on the rim either side, 22 m and 15 m over the village (a peak, not a table top),
			// rounded (an S-curve, never steeper than about 35 degrees, so no stone walls)
			for (int32 k = 0; k < 2; ++k)
			{
				const float Pk = En.PhiE + (k ? 1.f : -1.f) * FMath::DegreesToRadians(88.f + 4.f * k);
				const FVector2D Kc = Mid() + FVector2D(FMath::Cos(Pk), FMath::Sin(Pk)) * (12500.f + 300.f * k);
				const float Rk = 5000.f - 700.f * k, Dk = float(FVector2D::Distance(P, Kc)) * (1.f + 0.15f * Perlin(P, 1500.f, 2.4f + k, -1.1f));
				if (Dk < Rk) { const float U = 1.f - Dk / Rk; H = FMath::Max(H, SilverTop - 1500.f + (3700.f - 700.f * k) * U * U * (3.f - 2.f * U) + 120.f * Perlin(P, 600.f, 4.f, 9.f + k) * U); }
			}
			// the cliff, opposite the way in: the top runs level to a ragged lip, then four drops with ledges between (3 to 14 m deep), a lower
			// wall, and a scree apron at its foot; its sides end raggedly (the fade's bearing broken by noise) with rubble, not in a straight cut
			const float Dc = FMath::RadiansToDegrees(FMath::Abs(Ang(Phi - En.PhiE - PI))) + 14.f * Perlin(P, 2600.f, 1.1f, 6.2f) + 6.f * Perlin(P, 800.f, -3.4f, 2.2f);
			const float A = 1.f - Smooth(CliffFull, CliffNone, Dc);
			if (A > 0.f)
			{
				// (round 2: toward its sides the lip steps out down the slope and every drop shrinks with it, so the cliff tapers into the slopes
				// instead of standing as a square block)
				const float Lp = Lip + 420.f * Perlin(P, 2600.f, 8.1f, -0.7f) + 230.f * Perlin(P, 900.f, -2.6f, 4.9f) + 7000.f * (1.f - A);
				const float Tl = FMath::Max(Lp - Rs, 0.f) / Run, Tpl = FMath::Sqrt(Tl * Tl + 0.0025f) - 0.05f;
				const float Ztop = FMath::Lerp(Hb + (SilverTop - Hb) * FMath::Pow(FMath::Max(0.f, 1.f - Tpl), Exp), SilverTop, Smooth(0.45f, 0.95f, A));
				const float Sc = FMath::Clamp((Ztop - Hb) / (SilverTop - Hb), 0.15f, 1.f);
				const float E = R - Lp;
				float Wl[4], Zl[4];
				const float Seeds[4] = { 2.2f, -6.6f, 4.1f, -1.3f };
				float Z = Ztop;
				for (int32 k = 0; k < 4; ++k)
				{
					Wl[k] = 300.f + 1100.f * Smooth(-0.35f, 0.25f, Perlin(P, 3400.f + 500.f * k, Seeds[k], 9.9f - 3.f * k));
					Z -= Sc * ((k == 0 ? 3200.f : 3100.f) + 550.f * Perlin(P, 5200.f - 300.f * k, 4.4f - k, 1.1f + k));
					Zl[k] = Z;
				}
				float Hc = 0.f, E0 = 0.f;
				int32 k = 0;
				if (E < 0.f) Hc = FMath::Lerp(H, SilverTop, Smooth(0.45f, 0.95f, A));   // (the level top runs to the lip only in the middle: toward its sides the slopes round off first, so it never stands as a square block)
				else
				{
					for (; k < 4; ++k) { if (E < E0 + Wl[k]) break; E0 += Wl[k]; }
					if (k < 4)
					{
						Hc = Zl[k] + 60.f * Perlin(P, 500.f, 1.f + k, 2.f);
						if (OutLedge) *OutLedge = E > E0 + 150.f && E < E0 + Wl[k] - 150.f && A > 0.85f;
					}
					else
					{
						const float S = FMath::Clamp((E - E0) / 9000.f, 0.f, 1.f);
						const float Tal = Sc * (3200.f + 1000.f * Perlin(P, 3300.f, 0.4f, -8.8f));
						Hc = Hb + Tal * FMath::Square(1.f - S) + 180.f * Perlin(P, 1200.f, 6.6f, 6.6f) * (1.f - S);
					}
				}
				const float Flank = 4.f * A * (1.f - A);   // (rubble and crags where the cliff gives way to the slopes)
				H = FMath::Lerp(H, Hc, A) + Flank * (260.f * Perlin(P, 700.f, -5.1f, 3.9f) + 140.f * Perlin(P, 260.f, 2.8f, -7.7f));
			}
			if (OutHollow) *OutHollow = FMath::Clamp(Hollow + 0.35f * FMath::Cos(Phi), -1.f, 1.f);   // (+X is north: the shady side keeps more)
			return H;
		}

		// the road: its line, its height along it, and the pieces its banks are worked out by
		struct FChunk { int32 A, B; FBox2D Box; float Lo, Hi; };
		struct FMountRoad
		{
			TArray<FVector2D> Line;   // 60 cm, from the foot (the old line's point iF) to the middle
			TArray<FVector2D> Q;      // every ~2 m over the mountain part
			TArray<float> Z;
			TArray<FChunk> Chunks;
			float Grade = 0.f, Len = 0.f, Z0 = 0.f;
			uint32 Hash = 0;
		};
		const FMountRoad& Road()
		{
			static const FMountRoad Out = []()
			{
				FMountRoad M;
				const FEntry& En = Entry();
				if (En.Road < 0) { UE_LOG(LogTemp, Warning, TEXT("JEEBMOUNT no road into Silver Point found: no mountain road")); return M; }
				TGuardValue<bool> Guard(tBuilding, true);
				const TArray<FVector2D>& L = Grid().Roads[En.Road].Line;
				const FVector2D C = Mid(), F = L[En.iF];
				float W0;
				const float Hf = JeebTerrain::HeightAt(F.X, F.Y);
				M.Z0 = FMath::Lerp(Hf, FMath::Max(Hf, Shape(F, W0)), 0.f);
				M.Z0 = FMath::Lerp(Hf, FMath::Max(Hf, Shape(F, W0)), W0);
				auto Rinv = [&](float Z, float Phi)
				{
					float Exp; const float Run = RunAt(Phi, En.PhiE, &Exp);
					const float Fr = FMath::Clamp((Z - Hb) / (SilverTop - Hb), 0.f, 1.f);
					const float Tp = 1.f - FMath::Pow(Fr, 1.f / Exp);
					return Rs + Run * FMath::Sqrt(FMath::Max(0.f, FMath::Square(Tp + 0.05f) - 0.0025f));
				};
				const FVector2D H0 = (F - L[FMath::Max(En.iF - 30, 0)]).GetSafeNormal();   // (the old road's heading at the foot)
				const FVector2D Df = F - C;
				const float PhiF = FMath::Atan2(Df.Y, Df.X);
				const float S1 = Ang(PhiF - En.PhiE) <= 0.f ? 1.f : -1.f;
				const float Leg = FMath::DegreesToRadians(LegDeg), EndOff = 1200.f / Re;
				constexpr int32 NL = 6;   // (round 2: six legs and five hairpins, the peak being 175 m)
				float Targets[NL];
				for (int32 k = 0; k < NL; ++k) Targets[k] = k == NL - 1 ? En.PhiE - S1 * EndOff : En.PhiE + ((k & 1) ? -S1 : S1) * Leg;
				// one trace of the road at grade G: returns its height at the end and fills the points (every 4 m)
				auto Trace = [&](float G, TArray<FVector2D>& Pts) -> float
				{
					Pts.Reset();
					constexpr float Ds = 100.f;
					FVector2D P = F; float Z = M.Z0, S = 0.f;
					float Phi = PhiF, Off = Df.Size() - Rinv(Z, Phi);
					int32 StepN = 0;
					for (int32 Lg = 0; Lg < NL; ++Lg)
					{
						const float Sign = Ang(Targets[Lg] - Phi) >= 0.f ? 1.f : -1.f;
						for (int32 k = 0; k < 40000; ++k)
						{
							const FVector2D D = P - C;
							const float R = D.Size();
							Phi = FMath::Atan2(D.Y, D.X);
							float Rstar = Rinv(Z, Phi) + Off;
							if (Lg == NL - 1) Rstar = FMath::Lerp(Rstar, Re + 150.f, 1.f - Smooth(0.f, 0.35f, FMath::Abs(Ang(Targets[NL - 1] - Phi))));
							const float Dr = FMath::Clamp(Rstar - R, -0.6f * Ds, 0.6f * Ds);
							const FVector2D Rad = D / FMath::Max(R, 1.f), Tan(-Rad.Y * Sign, Rad.X * Sign);
							FVector2D V = Rad * Dr + Tan * FMath::Sqrt(FMath::Max(Ds * Ds - Dr * Dr, 0.f));
							if (Lg == 0 && S < 4000.f) V = (FMath::Lerp(H0 * Ds, V, Smooth(0.f, 4000.f, S))).GetSafeNormal() * Ds;   // (on from the old road's heading)
							P += V; Z += G * Ds; S += Ds;
							Off *= 0.975f;
							if (++StepN % 4 == 0) Pts.Add(P);
							const FVector2D D2 = P - C;
							if (Sign * Ang(Targets[Lg] - FMath::Atan2(D2.Y, D2.X)) <= 0.f) break;
						}
						if (Lg == NL - 1) break;
						// a hairpin: a half circle turning in toward the top
						const FVector2D D = P - C;
						const FVector2D Rad = D.GetSafeNormal(), Tan(-Rad.Y * Sign, Rad.X * Sign), In = -Rad;
						const FVector2D Qc = P + In * Hairpin;
						const int32 Ns = FMath::CeilToInt(PI * Hairpin / Ds);
						for (int32 k = 1; k <= Ns; ++k)
						{
							const float Al = PI * k / Ns;
							P = Qc - In * Hairpin * FMath::Cos(Al) + Tan * Hairpin * FMath::Sin(Al);
							Z += G * PI * Hairpin / Ns; S += PI * Hairpin / Ns;
							if (++StepN % 4 == 0) Pts.Add(P);
						}
						const FVector2D D3 = P - C;
						Phi = FMath::Atan2(D3.Y, D3.X);
						Off = D3.Size() - Rinv(Z, Phi);
					}
					Pts.Add(P);
					M.Len = S;
					return Z;
				};
				TArray<FVector2D> Pts;
				float Lo = 0.03f, Hi = 0.25f;
				for (int32 It = 0; It < 40; ++It) { const float G = 0.5f * (Lo + Hi); if (Trace(G, Pts) < SilverTop) Lo = G; else Hi = G; }
				M.Grade = 0.5f * (Lo + Hi);
				Trace(M.Grade, Pts);
				// control points: the foot, the trace, a curve into the old line at the village's edge, the old line on to the middle
				TArray<FVector2D> Ctrl = { F };
				Ctrl.Append(Pts);
				const FVector2D Pm = Pts.Last(), Ec = L[En.iE], Pin = L[En.iIn];
				for (float T : { 0.25f, 0.5f, 0.75f }) Ctrl.Add(FMath::Square(1.f - T) * Pm + 2.f * T * (1.f - T) * Ec + T * T * Pin);
				for (int32 i = En.iIn; i < L.Num(); i += 13) Ctrl.Add(L[i]);
				if (Ctrl.Last() != L.Last()) Ctrl.Add(L.Last());
				M.Line = JeebTerrain::SmoothPath(Ctrl, false);
				// heights: a steady climb to the end of the trace, level after
				int32 Im = 0; float Best = 1.0e18f;
				TArray<float> S; S.SetNum(M.Line.Num());
				for (int32 i = 0; i < M.Line.Num(); ++i)
				{
					S[i] = i ? S[i - 1] + float(FVector2D::Distance(M.Line[i], M.Line[i - 1])) : 0.f;
					const float D = FVector2D::DistSquared(M.Line[i], Pm);
					if (D < Best) { Best = D; Im = i; }
				}
				const float Sm = FMath::Max(S[Im], 1.f);
				for (int32 i = 0; i < M.Line.Num(); i += 3)
				{
					if (i > Im && FVector2D::Distance(M.Line[i], C) < Rs - 300.f) break;
					M.Q.Add(M.Line[i]);
					M.Z.Add(FMath::Lerp(M.Z0, SilverTop, FMath::Min(S[i] / Sm, 1.f)));
				}
				for (int32 a = 0; a + 1 < M.Q.Num(); a += 4)
				{
					FChunk Ch; Ch.A = a; Ch.B = FMath::Min(a + 5, M.Q.Num() - 1);
					Ch.Box = FBox2D(ForceInit); Ch.Lo = 1.0e9f; Ch.Hi = -1.0e9f;
					for (int32 i = Ch.A; i <= Ch.B; ++i) { Ch.Box += M.Q[i]; Ch.Lo = FMath::Min(Ch.Lo, M.Z[i]); Ch.Hi = FMath::Max(Ch.Hi, M.Z[i]); }
					M.Chunks.Add(Ch);
				}
				M.Hash = FCrc::MemCrc32(M.Line.GetData(), M.Line.Num() * sizeof(FVector2D));
				M.Hash = FCrc::MemCrc32(M.Z.GetData(), M.Z.Num() * sizeof(float), M.Hash);
				UE_LOG(LogTemp, Log, TEXT("JEEBMOUNT road: foot %.0f %.0f (ground %.1f m), way in on bearing %.0f deg, %.0f m of climb at %.1f%% (%.1f deg), %d bank pieces, %d points; hash=%08x"),
					F.X, F.Y, M.Z0 / 100.f, FMath::RadiansToDegrees(En.PhiE), M.Len / 100.f, M.Grade * 100.f, FMath::RadiansToDegrees(FMath::Atan(M.Grade)), M.Chunks.Num(), M.Line.Num(), M.Hash);
				return M;
			}();
			return Out;
		}
		// the road's bench and banks over ground H at P: level across the road, banks no steeper than Bank, soft at their feet and tops
		float RoadBanks(const FVector2D& P, float H)
		{
			const FMountRoad& M = Road();
			for (const FChunk& Ch : M.Chunks)
			{
				const float Db = FMath::Sqrt(Ch.Box.ComputeSquaredDistanceToPoint(P));
				const float Reach = Bank * FMath::Max(Db - Bench - 150.f, 0.f);
				if (H >= Ch.Lo - Reach && H <= Ch.Hi + Reach) continue;   // (no bank of this piece reaches here)
				float Best = 1.0e18f, Zr = 0.f;
				for (int32 i = Ch.A; i < Ch.B; ++i)
				{
					const FVector2D A = M.Q[i], AB = M.Q[i + 1] - A;
					const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
					const float D2 = FVector2D::DistSquared(P, A + AB * T);
					if (D2 < Best) { Best = D2; Zr = FMath::Lerp(M.Z[i], M.Z[i + 1], T); }
				}
				const float E = FMath::Sqrt(Best) - Bench;
				const float Ms = E < -50.f ? 0.f : (E > 150.f ? E - 50.f : FMath::Square(E + 50.f) / 400.f);
				H = FMath::Clamp(H, Zr - Bank * Ms, Zr + Bank * Ms);
			}
			return H;
		}
		// the summit's gentle rolls (OpenWorld.h): a 2 m grid, level under the buildings, round the plaza and at every door
		struct FRolls { bool bReady = false; FVector2D O = FVector2D::ZeroVector; int32 N = 0; TArray<float> V; };
		FRolls GRolls;
		// (round 2, 2026-11-28) the mountain gives way to the ground near a river, so its foot never stands as a wall over the water
		float RiverKeep(float X, float Y) { float Half; const float Dr = JeebWorld::RiverDistance(X, Y, Half); return Smooth(Half + 200.f, Half + 1400.f, Dr); }
		float RollAt(const FVector2D& P)
		{
			if (!GRolls.bReady) return 0.f;
			const float Gx = (P.X - GRolls.O.X) / 200.f, Gy = (P.Y - GRolls.O.Y) / 200.f;
			const int32 I = FMath::FloorToInt(Gx), J = FMath::FloorToInt(Gy);
			if (I < 0 || J < 0 || I >= GRolls.N - 1 || J >= GRolls.N - 1) return 0.f;
			const float Fx = Gx - I, Fy = Gy - J;
			const float* D = GRolls.V.GetData(); const int32 N = GRolls.N;
			return FMath::Lerp(FMath::Lerp(D[I * N + J], D[(I + 1) * N + J], Fx), FMath::Lerp(D[I * N + J + 1], D[(I + 1) * N + J + 1], Fx), Fy);
		}
	}
}

FVector2D JeebOpen::SilverMid() { return SM::Mid(); }

float JeebOpen::SilverMountW(float X, float Y)
{
	const FVector2D P(X, Y);
	if (FVector2D::DistSquared(P, SM::Mid()) > FMath::Square(SilverReach) || SM::tBuilding || SM::Entry().Road < 0) return 0.f;
	float W; SM::Shape(P, W);
	return W * SM::RiverKeep(X, Y);
}

float JeebOpen::SilverMount(float X, float Y, float H)
{
	const FVector2D P(X, Y);
	const float D2 = FVector2D::DistSquared(P, SM::Mid());
	if (D2 > FMath::Square(SilverReach) || SM::tBuilding || SM::Entry().Road < 0) return H;
	float W;
	float Hm = SM::Shape(P, W);
	W *= SM::RiverKeep(X, Y);   // (round 2: the foot never fills a river: it eases down to the bank)
	if (D2 < FMath::Square(SM::Rs + 200.f)) Hm += SM::RollAt(P);
	float Out = FMath::Lerp(H, FMath::Max(H, Hm), W);
	if (SM::Road().Chunks.Num()) Out = FMath::Lerp(Out, SM::RoadBanks(P, Out), 1.f - Smooth(SilverReach - 3000.f, SilverReach, FMath::Sqrt(D2)));
	return Out;
}

bool JeebOpen::SilverOnLedge(float X, float Y)
{
	const FVector2D P(X, Y);
	if (FVector2D::DistSquared(P, SM::Mid()) > FMath::Square(SilverReach) || SM::Entry().Road < 0) return false;
	float W; bool bLedge = false;
	SM::Shape(P, W, nullptr, &bLedge);
	return bLedge;
}


bool JeebOpen::SilverWallsAt(float X, float Y)
{
	// (round 2, 2026-11-28) the fine ground draws stone walls only on the cliff's side: elsewhere on the mountain (the road's cut banks,
	// the crags) its steep faces stay ordinary ground, so no rows of little stepped stone faces show along the road
	const FVector2D P(X, Y);
	if (FVector2D::DistSquared(P, SM::Mid()) > FMath::Square(SilverReach) || SM::Entry().Road < 0) return true;
	const FVector2D D = P - SM::Mid();
	const float Dc = FMath::RadiansToDegrees(FMath::Abs(SM::Ang(FMath::Atan2(D.Y, D.X) - SM::Entry().PhiE - PI))) + 14.f * Perlin(P, 2600.f, 1.1f, 6.2f) + 6.f * Perlin(P, 800.f, -3.4f, 2.2f);
	return Dc < SM::CliffNone + 25.f;   // (well past the cliff's fade: its ends need their walls, or their faces stretch)
}
const TArray<FVector2D>& JeebOpen::SilverRoadLine() { return SM::Road().Line; }

void JeebOpen::SilverPrepare()
{
	const double T0 = FPlatformTime::Seconds();
	if (SM::Entry().Road < 0) return;
	SM::Road();
	// the rolls: the village's plots, plaza and doors first (they do not read the ground), then a 2 m grid of gentle noise over the top,
	// held level within 3-9 m of any wall, 2-8 m of a door and 2-12 m past the plaza's edge, fading out before the top's edge
	const TArray<JeebSilver::FPlot>& Plots = JeebSilver::Plots();
	const FVector2D C = SM::Mid();
	SM::FRolls R;
	R.N = 81; R.O = C - FVector2D(8000.f, 8000.f);
	R.V.SetNumZeroed(R.N * R.N);
	TArray<FVector2D> Doors;
	for (const JeebSilver::FPlot& Pl : Plots) Doors.Add(JeebSilver::DoorSpot(Pl, 60.f));
	float Hi = 0.f, Lo = 0.f;
	for (int32 i = 0; i < R.N; ++i)
		for (int32 j = 0; j < R.N; ++j)
		{
			const FVector2D P = R.O + FVector2D(i * 200.f, j * 200.f);
			const float D = FVector2D::Distance(P, C);
			float K = Smooth(SM::Rs, SM::Rs - 1500.f, D) * Smooth(JeebSilver::PlazaR + 200.f, JeebSilver::PlazaR + 1200.f, D);
			if (K <= 0.f) continue;
			for (const JeebSilver::FPlot& Pl : Plots)
				if (FVector2D::DistSquared(Pl.Centre, P) < FMath::Square(Pl.Reach + 1000.f)) K *= Smooth(300.f, 900.f, JeebSilver::WallSD(Pl, P.X, P.Y));
			for (const FVector2D& Dd : Doors) K *= Smooth(200.f, 800.f, float(FVector2D::Distance(Dd, P)));
			const FVector2D Q(P.X * 0.682f - P.Y * 0.731f, P.X * 0.731f + P.Y * 0.682f);
			const float V = K * (120.f * Perlin(P, 2300.f, 3.3f, -6.1f) + 55.f * Perlin(Q, 950.f, -1.7f, 2.4f));
			R.V[i * R.N + j] = V; Hi = FMath::Max(Hi, V); Lo = FMath::Min(Lo, V);
		}
	R.bReady = true;
	SM::GRolls = MoveTemp(R);
	UE_LOG(LogTemp, Log, TEXT("JEEBMOUNT summit rolls %.0f to %.0f cm (level under %d buildings, the plaza and every door); hash=%08x; %.0f ms"),
		Lo, Hi, Plots.Num(), FCrc::MemCrc32(SM::GRolls.V.GetData(), SM::GRolls.V.Num() * sizeof(float)), (FPlatformTime::Seconds() - T0) * 1000.0);
}

void JeebOpen::SilverLogStats()
{
	const SM::FMountRoad& M = SM::Road();
	if (M.Line.Num() < 2) return;
	const FVector2D C = SM::Mid();
	// the road as built: steepest over 15 m, worst sideways tilt, how far the ground is from the road's own height
	float Worst = 0.f, Tilt = 0.f, Off = 0.f; FVector2D WorstAt = C;
	for (int32 i = 25; i < M.Line.Num(); i += 25)
	{
		const FVector2D A = M.Line[i - 25], B = M.Line[i];
		const float Za = JeebTerrain::HeightAt(A.X, A.Y), Zb = JeebTerrain::HeightAt(B.X, B.Y);
		const float G = FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(Zb - Za) / FMath::Max(float(FVector2D::Distance(A, B)), 1.f)));
		if (G > Worst) { Worst = G; WorstAt = B; }
		const FVector2D Side = FVector2D(-(B - A).Y, (B - A).X).GetSafeNormal() * 120.f;
		Tilt = FMath::Max(Tilt, FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(JeebTerrain::HeightAt(B.X + Side.X, B.Y + Side.Y) - JeebTerrain::HeightAt(B.X - Side.X, B.Y - Side.Y)) / 240.f)));
	}
	for (int32 i = 0; i < M.Q.Num(); ++i) Off = FMath::Max(Off, FMath::Abs(JeebTerrain::HeightAt(M.Q[i].X, M.Q[i].Y) - M.Z[i]));
	// the cliff: the biggest drop along its middle bearing, and the peak against the land round it
	const float PhiC = SM::Entry().PhiE + PI;
	const FVector2D Dir(FMath::Cos(PhiC), FMath::Sin(PhiC));
	float Drop = 0.f, Prev = JeebTerrain::HeightAt(C.X + Dir.X * 6000.f, C.Y + Dir.Y * 6000.f);
	float Foot = Prev;
	for (float R = 6100.f; R < 30000.f; R += 100.f) { const float Z = JeebTerrain::HeightAt(C.X + Dir.X * R, C.Y + Dir.Y * R); Drop = FMath::Max(Drop, Prev - Z); Prev = Z; Foot = FMath::Min(Foot, Z); }
	float Around = 0.f;
	for (int32 a = 0; a < 16; ++a) { const FVector2D Q = C + FVector2D(FMath::Cos(a * PI / 8.f), FMath::Sin(a * PI / 8.f)) * 45000.f; Around = FMath::Max(Around, JeebTerrain::HeightAt(Q.X, Q.Y)); }
	// the slopes away from the cliff: steepest over 4 m
	float Steep = 0.f; FVector2D SteepAt = C;
	for (float X = C.X - 36000.f; X < C.X + 36000.f; X += 400.f)
		for (float Y = C.Y - 36000.f; Y < C.Y + 36000.f; Y += 400.f)
		{
			const FVector2D D = FVector2D(X, Y) - C;
			if (FMath::RadiansToDegrees(FMath::Abs(SM::Ang(FMath::Atan2(D.Y, D.X) - PhiC))) < 90.f) continue;
			const float G = FMath::Abs(JeebTerrain::HeightAt(X + 200.f, Y) - JeebTerrain::HeightAt(X - 200.f, Y)) / 400.f;
			if (G > Steep) { Steep = G; SteepAt = FVector2D(X, Y); }
		}
	UE_LOG(LogTemp, Log, TEXT("JEEBMOUNT stats: summit %.1f m (middle %.1f m), highest ground 450 m round it %.1f m; road %.0f m from %.1f m to %.1f m, steepest %.1f deg (15 m) at %.0f %.0f, tilt across at most %.1f deg, ground off the road's height at most %.0f cm; cliff: biggest drop in 1 m %.1f m, foot %.1f m; steepest slope away from the cliff %.0f deg at %.0f %.0f"),
		SilverTop / 100.f, JeebTerrain::HeightAt(C.X, C.Y) / 100.f, Around / 100.f, M.Len / 100.f, M.Z0 / 100.f, SilverTop / 100.f, Worst, WorstAt.X, WorstAt.Y, Tilt, Off, Drop / 100.f, Foot / 100.f,
		FMath::RadiansToDegrees(FMath::Atan(Steep)), SteepAt.X, SteepAt.Y);
	// eyes for test pictures (-ShotEyes X_Y_A<z>_yaw_pitch): far (the peak over the land), the cliff, the road, the top
	const float PhiE = SM::Entry().PhiE;
	auto Eye = [&](const TCHAR* Name, float Bear, float R, float Up, float LookAtZ)
	{
		const FVector2D Q = C + FVector2D(FMath::Cos(Bear), FMath::Sin(Bear)) * R;
		const float Zg = JeebTerrain::HeightAt(Q.X, Q.Y) + Up;
		const FVector2D To = C - Q;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(LookAtZ - Zg, To.Size()));
		UE_LOG(LogTemp, Log, TEXT("JEEBMOUNT eye %s: %.0f_%.0f_A%.0f_%.0f_%.0f"), Name, Q.X, Q.Y, Zg, Yaw, Pitch);
	};
	Eye(TEXT("far"), PhiE - 0.5f, 120000.f, 2500.f, 10000.f);
	Eye(TEXT("cliff"), PhiC + 0.3f, 46000.f, 1500.f, 11000.f);
	Eye(TEXT("cliffnear"), PhiC - 0.15f, 30000.f, 6000.f, 13000.f);
	Eye(TEXT("road"), PhiE + 0.25f, 58000.f, 12000.f, 7000.f);
	Eye(TEXT("fog"), PhiE + 0.6f, 26000.f, 2500.f, SilverTop + 1500.f);
	Eye(TEXT("top"), PhiE, 6500.f, 1400.f, SilverTop + 300.f);
}

void JeebOpen::SilverWeather(double ServerSeconds, float& OutFog, float& OutSnow)
{
	// slots of 150 s: every other one clear; between them fog or snowfall (now and then nothing), picked from the slot's number, each
	// easing in and out over 30 s inside its own slot, so the two never overlap and a clear spell always lies between
	constexpr double Slot = 150.0;
	const double T = FMath::Max(ServerSeconds, 0.0);
	const int64 K = int64(T / Slot);
	const float In = float((T - double(K) * Slot) / Slot);
	OutFog = OutSnow = 0.f;
	if ((K & 1) == 0) return;
	uint32 H = uint32(int32(K)) * 2246822519u + 0x85ebca6bu; H ^= H >> 15; H *= 2654435761u; H ^= H >> 13;
	const float Amt = Smooth(0.f, 0.2f, In) * Smooth(1.f, 0.8f, In);
	const uint32 Pick = H % 10u;
	if (Pick < 5u) OutFog = Amt; else if (Pick < 9u) OutSnow = Amt;
}

const TArray<TArray<FVector2D>>& JeebOpen::RoadLines()
{
	static const TArray<TArray<FVector2D>> Lines = []()
	{
		TArray<TArray<FVector2D>> A;
		const SM::FEntry& En = SM::Entry();
		for (int32 r = 0; r < Grid().Roads.Num(); ++r)
		{
			const TArray<FVector2D>& L = Grid().Roads[r].Line;
			if (r == En.Road && SM::Road().Line.Num() > 1)   // (Silver Point's: the old line to the mountain's foot, then the mountain road)
			{
				TArray<FVector2D> S(L.GetData(), En.iF);
				S.Append(SM::Road().Line);
				A.Add(MoveTemp(S));
			}
			else A.Add(L);
		}
		return A;
	}();
	return Lines;
}

void JeebOpen::Prepare()
{
	const FGrid& G = Grid();
	float Top = -1.0e9f, Low = 1.0e9f;
	for (float V : G.V) { Top = FMath::Max(Top, V); Low = FMath::Min(Low, V); }
	UE_LOG(LogTemp, Log, TEXT("JEEBOPEN grid %dx%d (%.0f m step) over the whole world: offsets %.1f to %.1f m, %d points lowered to keep slopes under %.0f deg, %d rounded by over 80 cm, %d roads levelled (deepest cut %.1f m, highest bank %.1f m), built in %.0f ms; hash=%08x roads=%08x"),
		G.NI, G.NJ, Step / 100.f, Low / 100.f, Top / 100.f, G.Sloped, FMath::RadiansToDegrees(FMath::Atan(MaxSlope)), G.Rounded, G.Roads.Num(), G.MaxCut / 100.f, G.MaxFill / 100.f, G.Ms, G.Hash, G.RoadHash);
}

void JeebOpen::LogStats()
{
	SilverLogStats();   // (2026-11-27: Silver Point's mountain, JEEBMOUNT)
	// The finished ground on the world's 8 m mesh points where this grid shapes it: the highest ground, slopes per triangle, sharp tops
	const double T0 = FPlatformTime::Seconds();
	const FGrid& G = Grid();
	const int32 NI = G.NI, N = G.NJ;
	TArray<float> H; H.SetNumUninitialized(NI * N);
	for (int32 i = 0; i < NI; ++i) for (int32 j = 0; j < N; ++j) H[i * N + j] = JeebTerrain::HeightAt(G.GX(i), G.GY(j));
	int32 Cells = 0, Over50 = 0, Over70 = 0, Over100 = 0, Peaks = 0, Snowy = 0;
	float Steepest = 0.f, Peak = -1.0e9f; FVector2D SteepAt = FVector2D::ZeroVector, PeakAt = FVector2D::ZeroVector;
	for (int32 i = 1; i + 2 < NI; ++i)
		for (int32 j = 1; j + 2 < N; ++j)
		{
			const int32 k = i * N + j;
			if (G.V[k] == 0.f && G.V[k + 1] == 0.f && G.V[k + N] == 0.f && G.V[k + N + 1] == 0.f) continue;
			const float A = H[k], B = H[k + N], C = H[k + 1], D = H[k + N + 1];
			const float Sl = FMath::Max(FVector2D(B - A, C - A).Size(), FVector2D(D - C, D - B).Size()) / Step;
			++Cells; Over50 += Sl > 0.5f; Over70 += Sl > 0.72f; Over100 += Sl > 1.f; Snowy += A > JeebOpen::SnowFrom;
			if (Sl > Steepest) { Steepest = Sl; SteepAt = FVector2D(G.GX(i), G.GY(j)); }
			if (A > Peak) { Peak = A; PeakAt = FVector2D(G.GX(i), G.GY(j)); }
			float Sum = 0.f; bool bTop = true;
			for (int32 a = -1; a <= 1; ++a) for (int32 b = -1; b <= 1; ++b) { if (!a && !b) continue; const float Hq = H[k + a * N + b]; bTop &= Hq < A; Sum += Hq; }
			Peaks += bTop && A - Sum / 8.f > 80.f;
		}
	UE_LOG(LogTemp, Log, TEXT("JEEBOPEN stats: %d shaped cells (8 m); finished ground hash=%08x; highest ground %.1f m (world) at %.0f %.0f; %d cells above the snow line (%.0f m); slope over 0.5 (27 deg) %.1f%%, over 0.72 (36 deg) %.2f%%, over 1 (45 deg, climbing) %d; steepest %.2f (%.0f deg) at %.0f %.0f; sharp tops (over 80 cm above their 8 neighbours' mean) %d; %.0f ms"),
		Cells, FCrc::MemCrc32(H.GetData(), H.Num() * sizeof(float)), Peak / 100.f, PeakAt.X, PeakAt.Y, Snowy, JeebOpen::SnowFrom / 100.f, Cells ? 100.f * Over50 / Cells : 0.f, Cells ? 100.f * Over70 / Cells : 0.f, Over100,
		Steepest, FMath::RadiansToDegrees(FMath::Atan(Steepest)), SteepAt.X, SteepAt.Y, Peaks, (FPlatformTime::Seconds() - T0) * 1000.0);
	for (int32 r = 0; r < G.Roads.Num(); ++r)
	{
		const TArray<FVector2D>& L = G.Roads[r].Line;
		float Worst = 0.f, High = -1.0e9f; FVector2D WorstAt = L[0];
		for (int32 i = 25; i < L.Num(); i += 25)
		{
			const float D = FVector2D::Distance(L[i], L[i - 25]);
			const float Z = JeebTerrain::HeightAt(L[i].X, L[i].Y);
			const float Gd = FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(Z - JeebTerrain::HeightAt(L[i - 25].X, L[i - 25].Y)) / FMath::Max(D, 1.f)));
			if (Gd > Worst) { Worst = Gd; WorstAt = L[i]; }
			High = FMath::Max(High, Z);
		}
		const FVector2D Mid = L[L.Num() / 2];
		UE_LOG(LogTemp, Log, TEXT("JEEBOPEN road %d: %.0f m from %.0f %.0f to %.0f %.0f, steepest %.1f degrees (over 15 m) at %.0f %.0f, highest point %.1f m; middle %.0f %.0f"),
			r, G.Roads[r].Len / 100.f, L[0].X, L[0].Y, L.Last().X, L.Last().Y, Worst, WorstAt.X, WorstAt.Y, High / 100.f, Mid.X, Mid.Y);
	}
	for (const FSite& S : Sites())
	{
		// (how level the site is: the finished ground within 30 m of its middle, and what each layer gives 10 m north-east of it)
		float Lo = 1.0e9f, Hi = -1.0e9f;
		for (float Dx = -3000.f; Dx <= 3000.f; Dx += 500.f) for (float Dy = -3000.f; Dy <= 3000.f; Dy += 500.f) { const float Z = JeebTerrain::HeightAt(S.Pos.X + Dx, S.Pos.Y + Dy); Lo = FMath::Min(Lo, Z); Hi = FMath::Max(Hi, Z); }
		const float Qx = S.Pos.X + 700.f, Qy = S.Pos.Y + 700.f;
		UE_LOG(LogTemp, Log, TEXT("JEEBOPEN site %s level check: ground within 30 m %.0f to %.0f cm; 10 m north-east: land %.0f, hills %.0f, this grid %.0f, finished %.0f"), S.Name, Lo, Hi, JeebWorld::LandHeight(Qx, Qy), JeebHills::Offset(Qx, Qy), Offset(Qx, Qy), JeebTerrain::HeightAt(Qx, Qy));
	}
	for (const FSite& S : Sites())
		UE_LOG(LogTemp, Log, TEXT("JEEBOPEN site %s at %.0f %.0f (ground %.0f): house at %.0f %.0f yaw %.0f, %.0f cm from the nearest road"), S.Name, S.Pos.X, S.Pos.Y, JeebTerrain::HeightAt(S.Pos.X, S.Pos.Y), S.House.X, S.House.Y, S.HouseYaw, JeebTerrain::PathClearance(S.House.X, S.House.Y));
}

// ------------------------------------------------------------------ woods, snow, weather, town sites
float JeebOpen::Woods(float X, float Y)
{
	const FVector2D P(X, Y);
	float Bias = 0.1f;
	// the north-east's high grasslands (east of Silver Point, north of the Anchorage road, to the coast): open, with the odd copse
	Bias -= 0.5f * Smooth(52000.f, 66000.f, Y) * Smooth(-22000.f, -8000.f, X) * Smooth(62000.f, 48000.f, X);
	// toward the tropics in the south-east: groves, not forest
	Bias -= 0.22f * Smooth(-60000.f, -78000.f, X) * Smooth(60000.f, 76000.f, Y);
	const float Val = 0.5f + 0.6f * Perlin(P, 36000.f, 1.7f, -5.2f) + 0.2f * Perlin(P, 9500.f, -3.9f, 2.8f) + Bias;
	const float Dense = Smooth(0.44f, 0.62f, Val);
	// intermittent woods: copses where a 50 m noise says, in the band between plain and forest
	const float Copse = 0.55f * Smooth(0.2f, 0.42f, Val) * Smooth(0.5f, 0.72f, 0.5f + 0.5f * Perlin(P, 5200.f, 8.3f, 0.6f));
	return FMath::Max3(Dense, Copse, 0.03f);
}

float JeebOpen::SnowRaw(float X, float Y, float Z, float Up)
{
	if (Z < SnowFrom - 2500.f) return -1.f;
	const FVector2D P(X, Y);
	const float Nz = 0.7f * Perlin(P, 9000.f, 4.9f, -6.3f) + 0.4f * Perlin(P, 2300.f, -2.2f, 7.5f);
	// (steep ground holds less: a 40 degree face stays bare rock about 24 m higher than a gentle one, so rock bands show through)
	// (only the northern range: further south the snow line climbs out of reach, so the lower ranges by the map's west edge and in the
	// north-east stay green)
	const float Base = (Z - SnowFrom) / (SnowTo - SnowFrom) + 0.5f * Nz - 0.8f * Smooth(0.9f, 0.72f, Up) - 1.5f * Smooth(55000.f, 35000.f, X);
	// (2026-11-27, Silver Point's mountain; owner: "I want the snow to start and grass to end roughly 2/3rds of the way up the mountain ...
	// don't just add a line where the snow starts") its own snow line about 117 m up (two thirds of 175; round 2), in a wide band broken by noise at
	// three unrelated sizes (one turned): drifts first in the gullies and on the shady north side, steep and sunny ground bare longest,
	// solid from about 145 m and over the whole top. The grass thins out through the same band (GroundMask GrassAmount).
	if (FVector2D::DistSquared(P, SM::Mid()) < FMath::Square(SilverReach) && !SM::tBuilding && SM::Entry().Road >= 0)
	{
		float W, Hollow;
		SM::Shape(P, W, &Hollow);
		if (W > 0.f)
		{
			const FVector2D Q(X * 0.766f - Y * 0.643f, X * 0.643f + Y * 0.766f);
			const float Nm = 0.35f * Perlin(P, 5200.f, -3.7f, 8.2f) + 0.25f * Perlin(Q, 1700.f, 6.1f, -1.4f) + 0.15f * Perlin(P, 450.f, 2.9f, 5.3f);
			const float Mine = (Z - 11700.f) / 3000.f + Nm + 0.35f * Hollow - 0.8f * Smooth(0.9f, 0.72f, Up);
			return FMath::Lerp(Base, Mine, W);
		}
	}
	return Base;
}

float JeebOpen::Snow(float X, float Y, float Z, float Up) { return FMath::Clamp(SnowRaw(X, Y, Z, Up) * 1.3f, 0.f, 1.f); }

float JeebOpen::SnowOnGround(float X, float Y)
{
	if (X < 30000.f) return 0.f;   // (no snow anywhere south of this: SnowRaw's own cut-off)
	const float Z = JeebTerrain::HeightAt(X, Y);
	if (Z < SnowFrom - 2500.f) return 0.f;
	constexpr float S = JeebWorld::WorldStep;
	const float Gx = (JeebTerrain::HeightAt(X + S, Y) - JeebTerrain::HeightAt(X - S, Y)) / (2.f * S), Gy = (JeebTerrain::HeightAt(X, Y + S) - JeebTerrain::HeightAt(X, Y - S)) / (2.f * S);
	return Snow(X, Y, Z, 1.f / FMath::Sqrt(1.f + Gx * Gx + Gy * Gy));
}

float JeebOpen::SnowSink(const FVector& Feet)
{
	if (Feet.X < 30000.f) return 0.f;
	const float Ground = JeebTerrain::DrawnHeight(Feet.X, Feet.Y);   // (the 8 m triangles as drawn and walked on)
	// only on the open ground itself (not on a rock, a roof, a bridge, or in the air)
	const float Above = Feet.Z - Ground;
	if (Above > 25.f || Above < -25.f) return 0.f;
	return SinkDepth * SnowOnGround(Feet.X, Feet.Y) * FMath::Clamp(1.f - Above / 25.f, 0.f, 1.f);
}

// (2026-11-26, owner's snowstorm request) one weather per game day, midnight to midnight on the server's clock (the sun's loop is
// 2 x 600 s, noon at clock 0, so midnight falls at 600 s + 1200 s x k). The day number seeds it, so every machine agrees. The changeover
// is eased over BlendSeconds centred on midnight (the night hides most of it).
float JeebOpen::Snowing(double ServerSeconds)
{
	auto On = [](int64 D) -> float
	{
		uint32 H = uint32(int32(D)) * 2654435761u + 0x9e3779b9u; H ^= H >> 15; H *= 2246822519u; H ^= H >> 13; H *= 3266489917u; H ^= H >> 16;
		return (double(H % 100000u) / 100000.0) < double(FMath::Clamp(JeebTune::V().StormChance, 0.f, 1.f)) ? 1.f : 0.f;
	};
	constexpr double Day = 1200.0, Midnight = 600.0;
	const double T = FMath::Max(ServerSeconds, 0.0) + Midnight;   // (day 0 runs from -600 s to 600 s: noon is its middle)
	const int64 D = int64(T / Day);
	const double In = T - double(D) * Day;
	const double Half = 0.5 * FMath::Max(1.f, JeebTune::V().BlendSeconds);
	if (In < Half && D > 0) return FMath::Lerp(On(D - 1), On(D), Smooth(0.f, 1.f, float((In + Half) / (2.0 * Half))));
	if (In > Day - Half) return FMath::Lerp(On(D), On(D + 1), Smooth(0.f, 1.f, float((In - (Day - Half)) / (2.0 * Half))));
	return On(D);
}

// 0 below the storm's height, 1 above it (world height of the camera, cm)
float JeebOpen::StormAlt(float Z)
{
	const float A = JeebTune::V().AltFromM * 100.f, B = FMath::Max(A + 100.f, JeebTune::V().AltToM * 100.f);
	return Smooth(A, B, Z);
}

const TArray<JeebOpen::FSite>& JeebOpen::Sites()
{
	static const TArray<FSite> All = []()
	{
		TArray<FSite> A;
		for (const JeebWorld::FTown& T : JeebWorld::Towns())
		{
			if (T.Fade <= 0.f) continue;   // (the old village site, Oak Hill and Steele Creek are built)
			FSite S; S.Name = T.Name; S.Pos = SiteMid(T.Pos); S.Radius = T.Flat + 1500.f;
			// the house: 16 m from the middle, on the side furthest from every road, its door toward the middle
			float Best = -1.0e9f; S.House = S.Pos + FVector2D(1600.f, 0.f);
			for (int32 a = 0; a < 12; ++a)
			{
				const FVector2D Q = S.Pos + FVector2D(1600.f, 0.f).GetRotated(a * 30.f);
				if (JeebWorld::WaterEdge(Q.X, Q.Y) < 2500.f) continue;   // (never on a river bank)
				const float C = JeebTerrain::PathClearance(Q.X, Q.Y);
				if (C > Best + 1.f) { Best = C; S.House = Q; }
			}
			const FVector2D ToMid = S.Pos - S.House;
			S.HouseYaw = FMath::RadiansToDegrees(FMath::Atan2(ToMid.Y, ToMid.X));
			A.Add(S);
		}
		return A;
	}();
	return All;
}
