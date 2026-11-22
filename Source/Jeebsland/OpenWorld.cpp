#include "OpenWorld.h"
#include "Terrain.h"
#include "OakForest.h"
#include "Misc/Crc.h"
#include "Algo/Reverse.h"

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
		return Open * TownKeep(P) * Smooth(1500.f, 12000.f, FMath::Max(FMath::Abs(X), FMath::Abs(Y)) - JeebTerrain::GroundHalf)
			* Smooth(0.f, 1500.f, JeebWorld::CoastY(X) - Y);   // (never the sea floor or the islands)
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
		// one soft 3x3 blur where the offset is free all round (not on the rock bands, whose risers it would round off, nor on a road)
		{
			TArray<float> Was = G.V;
			for (int32 i = 1; i + 1 < NI; ++i)
				for (int32 j = 1; j + 1 < N; ++j)
				{
					const int32 k = i * N + j;
					if (Fixed[k]) continue;
					bool bFree = true; float Sum = 0.f;
					for (int32 a = -1; a <= 1; ++a) for (int32 b = -1; b <= 1; ++b) { const int32 q = k + a * N + b; bFree &= !Fixed[q]; Sum += Was[q]; }
					if (!bFree) continue;
					G.V[k] = FMath::Lerp(Sum / 9.f, Was[k], FMath::Max(Cliff[k], RoadW[k]));
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

const TArray<TArray<FVector2D>>& JeebOpen::RoadLines()
{
	static const TArray<TArray<FVector2D>> Lines = []()
	{
		TArray<TArray<FVector2D>> A;
		for (const FRoad& R : Grid().Roads) A.Add(R.Line);
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
	return (Z - SnowFrom) / (SnowTo - SnowFrom) + 0.4f * Nz - 0.8f * Smooth(0.9f, 0.72f, Up);
}

float JeebOpen::Snow(float X, float Y, float Z, float Up) { return FMath::Clamp(SnowRaw(X, Y, Z, Up) * 1.3f, 0.f, 1.f); }

float JeebOpen::Snowing(double ServerSeconds)
{
	auto On = [](int64 S) -> float
	{
		if (S <= 0) return 0.f;   // (clear to begin with)
		if (S == 1) return 1.f;   // (then the first snow)
		uint32 H = uint32(S) * 2654435761u; H ^= H >> 15; H *= 2246822519u; H ^= H >> 13;
		return (H % 1000u) < 450u ? 1.f : 0.f;
	};
	constexpr double Ease = 25.0;
	const double T = FMath::Max(ServerSeconds, 0.0);
	const int64 S = int64(T / SnowSlot);
	const double In = T - double(S) * SnowSlot;
	const float Now = On(S);
	if (In < SnowSlot - Ease) return Now;
	return FMath::Lerp(Now, On(S + 1), Smooth(0.f, 1.f, float((In - (SnowSlot - Ease)) / Ease)));
}

const TArray<JeebOpen::FSite>& JeebOpen::Sites()
{
	static const TArray<FSite> All = []()
	{
		TArray<FSite> A;
		for (const JeebWorld::FTown& T : JeebWorld::Towns())
		{
			if (T.Fade <= 0.f) continue;   // (Blackrock, Oak Hill and Steele Creek are built)
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
