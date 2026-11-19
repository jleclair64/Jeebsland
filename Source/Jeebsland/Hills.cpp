#include "Hills.h"
#include "Terrain.h"
#include "OakForest.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// The forested hills round Oak Hill (Hills.h). Every number that shapes them is here; log JEEBHILLS.
namespace
{
	TAutoConsoleVariable<float> CVarScale(TEXT("jeeb.Hills.Scale"), 1.f,
		TEXT("Height of the forested hills round Oak Hill (1 as built, about 15-37 m out at 300-500 m; 0.5 half, 0 none). Read once when the world is first built: set it in Config/DefaultEngine.ini [ConsoleVariables] or start with -HillScale=0.5, then restart. Online, give the server and every player the same value (the ground is solid)."), ECVF_Default);

	using JeebWorld::Smooth;
	const FVector2D Plaza(JeebOakHill::PosX, JeebOakHill::PosY);

	float Perlin(const FVector2D& P, float Scale, float Ox, float Oy) { return FMath::PerlinNoise2D(FVector2D(P.X / Scale + Ox, P.Y / Scale + Oy)); }

	float BoxGap(float X, float Y)   // how far outside Oak Hill's box (cm; 0 inside)
	{
		const float Dx = FMath::Max3(JeebOakHill::BoxX0 - X, 0.f, X - JeebOakHill::BoxX1), Dy = FMath::Max3(JeebOakHill::BoxY0 - Y, 0.f, Y - JeebOakHill::BoxY1);
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}

	// ------------------------------------------------------------------ the shape
	// Heights (cm): out past RingFrom..RingTo from the plaza the hills stand RingBase plus up to RingTop more where the broad noise is high
	// (its valleys and saddles about 200-300 m apart), plus ridges and single hills of up to Ridges where a ridged noise crests (about
	// 150 m apart), with smaller bumps of up to Bumps on everything. Second pass (2026-11-16, the first read as a gentle swell): they
	// start 10 m past the town's box, are full height by 90 m, and stand tall right by the town, so from the plaza and the road ends a
	// forested skyline stands 20-40 m up every way but down the river valleys
	// Third pass (2026-11-18, owner: "all the land around oak hill and steele creek needs more hills"): they come closer to the town and
	// the water (full by 55 m past the town's box and 110 m from the water, was 90 and 170), bumps up to 10 m, and the same hills now
	// stand round Steele Creek (from 60 m past its centre, full by 150 m) and all along the land between the two towns (CorridorIn
	// either side of the line between them), with Steele Creek's road levelled through them like the forest roads
	constexpr float RingFrom = 3000.f, RingTo = 16000.f, RingBase = 1800.f, RingTop = 1500.f, Ridges = 1100.f, Bumps = 1000.f;
	// Edges (cm): the rim (full to RimIn from the plaza, none by RimOut), the town's box, the original square, the rivers' water
	constexpr float RimIn = 47000.f, RimOut = 62000.f;
	constexpr float TownFrom = 1000.f, TownTo = 5500.f;
	constexpr float SquareFrom = 1500.f, SquareTo = 12000.f;
	constexpr float RiverFrom = 2500.f, RiverTo = 11000.f;
	// Steele Creek: none within SteeleFrom of its centre, full by SteeleTo; its own rim; the corridor between the towns
	constexpr float SteeleFrom = 6000.f, SteeleTo = 15000.f, SteeleRimIn = 36000.f, SteeleRimOut = 48000.f;
	constexpr float CorridorIn = 20000.f, CorridorOut = 32000.f;
	const FVector2D SteeleC(JeebSteele::PosX, JeebSteele::PosY);
	// The grid: from 320 m south of Steele Creek to 640 m north of the plaza, 640 m either side of the plaza's line
	constexpr float GridX0 = -127200.f, GridX1 = JeebOakHill::PosX + 64000.f, GridY0 = JeebOakHill::PosY - 64000.f, GridY1 = JeebOakHill::PosY + 64000.f;
	float SegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
		return FVector2D::Distance(P, A + AB * T);
	}
	// The steepest the hills may make the ground (rise per run): 0.7 is 35 degrees, kept with a cut over the whole grid (only ever
	// lowering a hill), then one soft blur so the cut leaves no crease; the knight walks up to 44.8 and trees grow to 0.7
	constexpr float MaxSlope = 0.7f;
	// The forest roads' corridors: level across RoadFlat cm from the middle, then easing into the hills over 3.5 x the cut or fill
	// (12 to 45 m)
	constexpr float RoadFlat = 400.f, RoadEaseMin = 1200.f, RoadEaseMax = 4500.f, RoadEaseBy = 3.5f;
	// The highest the land and the hills may stand together (cm, world Z): eased in over TopSoft below it
	constexpr float TopZ = 5750.f, TopSoft = 600.f;

	// ------------------------------------------------------------------ the forest roads' profiles
	struct FRoadProfile
	{
		TArray<FVector2D> Pts;   // every ~5 m along the road
		TArray<float> Hill;      // the hills' height wanted there (cm): the land plus this climbs at most RoadGrade
		TArray<float> Ease;      // how wide the bank back into the hills is (cm)
		FBox2D Bounds = FBox2D(ForceInit);
	};

	struct FGrid
	{
		int32 N = 0, NI = 0;   // (N points east-west, NI north-south)
		TArray<float> V;
		uint32 Hash = 0;
		double Ms = 0.0;
		int32 RoadsN = 0;
		float MaxCut = 0.f, MaxFill = 0.f;
		int32 Capped = 0;
		int32 Sloped = 0;
	};

	FGrid BuildGrid()
	{
		const double T0 = FPlatformTime::Seconds();
		FGrid G;

		// The roads, as routed on the raw hills
		TArray<FRoadProfile> Roads;
		// the forest roads, then Steele Creek's long road north to the fork (its streets in town lie where there are no hills)
		TArray<TArray<FVector2D>> AllLines;
		if (JeebHills::Scale() > 0.f)   // (no hills: the land exactly as before)
		{
			AllLines = JeebOakForest::RoadLines();
			for (const JeebSteele::FStreet& St : JeebSteele::Streets()) if (St.Pts.Num() > 800) AllLines.Add(St.Pts);
		}
		for (const TArray<FVector2D>& Line : AllLines)
		{
			if (Line.Num() < 2) continue;
			FRoadProfile R;
			for (int32 i = 0; i < Line.Num(); i += 8) R.Pts.Add(Line[i]);
			if (R.Pts.Last() != Line.Last()) R.Pts.Add(Line.Last());
			const int32 M = R.Pts.Num();
			TArray<float> S, Base, Raw, T;
			S.SetNum(M); Base.SetNum(M); Raw.SetNum(M); T.SetNum(M);
			for (int32 i = 0; i < M; ++i)
			{
				S[i] = i ? S[i - 1] + FVector2D::Distance(R.Pts[i], R.Pts[i - 1]) : 0.f;
				Base[i] = JeebTerrain::HeightAt(R.Pts[i].X, R.Pts[i].Y, true, 0);
				Raw[i] = JeebHills::Raw(R.Pts[i].X, R.Pts[i].Y);
			}
			// the road's height as the hills would have it, softened over 20 m either way
			for (int32 i = 0; i < M; ++i)
			{
				float Sum = 0.f, W = 0.f;
				for (int32 k = i; k >= 0 && S[i] - S[k] < 2000.f; --k) { Sum += Base[k] + Raw[k]; W += 1.f; }
				for (int32 k = i + 1; k < M && S[k] - S[i] < 2000.f; ++k) { Sum += Base[k] + Raw[k]; W += 1.f; }
				T[i] = Sum / W;
			}
			// then no steeper than RoadGrade: halfway between the highest and the lowest such line through it (each climbs at most
			// RoadGrade, so their mean does too), so the road both cuts through rises and runs on a bank over dips
			TArray<float> Lo = T, Hi = T;
			for (int32 i = 1; i < M; ++i) { const float D = (S[i] - S[i - 1]) * JeebHills::RoadGrade; Lo[i] = FMath::Min(Lo[i], Lo[i - 1] + D); Hi[i] = FMath::Max(Hi[i], Hi[i - 1] - D); }
			for (int32 i = M - 2; i >= 0; --i) { const float D = (S[i + 1] - S[i]) * JeebHills::RoadGrade; Lo[i] = FMath::Min(Lo[i], Lo[i + 1] + D); Hi[i] = FMath::Max(Hi[i], Hi[i + 1] - D); }
			R.Hill.SetNum(M); R.Ease.SetNum(M);
			for (int32 i = 0; i < M; ++i)
			{
				R.Hill[i] = 0.5f * (Lo[i] + Hi[i]) - Base[i];
			}
			// a fork (a road that starts on an earlier one) starts at that road's own height there, eased in over its first 60 m, so the
			// two profiles meet without a step (2026-11-16: 6.3 degrees where the east road leaves the north road)
			for (const FRoadProfile& Prev : Roads)
			{
				float Best = 1.0e18f; int32 BestK = 0;
				for (int32 k = 0; k < Prev.Pts.Num(); ++k) { const float D2 = FVector2D::DistSquared(Prev.Pts[k], R.Pts[0]); if (D2 < Best) { Best = D2; BestK = k; } }
				if (Best > 1000.f * 1000.f) continue;
				const float Step = Prev.Hill[BestK] + JeebTerrain::HeightAt(Prev.Pts[BestK].X, Prev.Pts[BestK].Y, true, 0) - (R.Hill[0] + Base[0]);
				for (int32 i = 0; i < M && S[i] < 6000.f; ++i) R.Hill[i] += Step * (1.f - Smooth(0.f, 6000.f, S[i]));
				break;
			}
			for (int32 i = 0; i < M; ++i)
			{
				const float Diff = R.Hill[i] - Raw[i];
				G.MaxCut = FMath::Max(G.MaxCut, -Diff); G.MaxFill = FMath::Max(G.MaxFill, Diff);
				R.Ease[i] = FMath::Clamp(RoadEaseBy * FMath::Abs(Diff), RoadEaseMin, RoadEaseMax);
			}
			R.Bounds = FBox2D(R.Pts).ExpandBy(RoadFlat + RoadEaseMax + 100.f);
			Roads.Add(R);
		}
		G.RoadsN = Roads.Num();

		// The grid: the raw hills, each road's corridor mixed in by weights that grow without end on the road (w / (1 - w), as Oak
		// Hill's pads), so where two roads meet (the fork) the ground stays smooth
		G.NI = FMath::RoundToInt((GridX1 - GridX0) / JeebHills::GridStep) + 1;
		G.N = FMath::RoundToInt((GridY1 - GridY0) / JeebHills::GridStep) + 1;
		G.V.SetNumZeroed(G.NI * G.N);
		const float X0 = GridX0, Y0 = GridY0;
		for (int32 i = 0; i < G.NI; ++i)
			for (int32 j = 0; j < G.N; ++j)
			{
				const FVector2D P(X0 + i * JeebHills::GridStep, Y0 + j * JeebHills::GridStep);
				const float Raw = JeebHills::Raw(P.X, P.Y);
				float Sum = Raw, Weight = 1.f;
				for (const FRoadProfile& R : Roads)
				{
					if (!R.Bounds.IsInside(P)) continue;
					float Best = 1.0e18f; int32 BestI = 0; float BestT = 0.f;
					for (int32 k = 0; k + 1 < R.Pts.Num(); ++k)
					{
						const FVector2D A = R.Pts[k], AB = R.Pts[k + 1] - A;
						const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
						const float D2 = FVector2D::DistSquared(P, A + AB * T);
						if (D2 < Best) { Best = D2; BestI = k; BestT = T; }
					}
					const float D = FMath::Sqrt(Best);
					const float Ease = FMath::Lerp(R.Ease[BestI], R.Ease[BestI + 1], BestT);
					if (D >= RoadFlat + Ease) continue;
					const float W = 1.f - Smooth(RoadFlat, RoadFlat + Ease, D);
					const float Odds = FMath::Min(W / FMath::Max(1.f - W, 1e-5f), 1.0e5f);
					Sum += Odds * FMath::Lerp(R.Hill[BestI], R.Hill[BestI + 1], BestT); Weight += Odds;
				}
				G.V[i * G.N + j] = Sum / Weight;
			}

		// The land under the hills, then no slope over MaxSlope: the ground (land + hills) is cut down to the lowest cone of that slope
		// standing on any point (two sweeps each way of an 8-neighbour chamfer), the hills keeping only what is left over the land
		const int32 N = G.N, NI = G.NI;
		TArray<float> Base, T;
		Base.SetNumUninitialized(NI * N); T.SetNumUninitialized(NI * N);
		for (int32 i = 0; i < NI; ++i)
			for (int32 j = 0; j < N; ++j)
			{
				const int32 k = i * N + j;
				Base[k] = JeebTerrain::HeightAt(X0 + i * JeebHills::GridStep, Y0 + j * JeebHills::GridStep, true, 0);
				T[k] = Base[k] + G.V[k];
			}
		{
			const float S1 = MaxSlope * JeebHills::GridStep, S2 = S1 * 1.41421356f;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (int32 i = 0; i < NI; ++i)
					for (int32 j = 0; j < N; ++j)
					{
						float& Here = T[i * N + j];
						if (i > 0) { Here = FMath::Min(Here, T[(i - 1) * N + j] + S1); if (j > 0) Here = FMath::Min(Here, T[(i - 1) * N + j - 1] + S2); if (j + 1 < N) Here = FMath::Min(Here, T[(i - 1) * N + j + 1] + S2); }
						if (j > 0) Here = FMath::Min(Here, T[i * N + j - 1] + S1);
					}
				for (int32 i = NI - 1; i >= 0; --i)
					for (int32 j = N - 1; j >= 0; --j)
					{
						float& Here = T[i * N + j];
						if (i + 1 < NI) { Here = FMath::Min(Here, T[(i + 1) * N + j] + S1); if (j + 1 < N) Here = FMath::Min(Here, T[(i + 1) * N + j + 1] + S2); if (j > 0) Here = FMath::Min(Here, T[(i + 1) * N + j - 1] + S2); }
						if (j + 1 < N) Here = FMath::Min(Here, T[i * N + j + 1] + S1);
					}
			}
		}
		TArray<float> Cut; Cut.SetNumUninitialized(NI * N);
		for (int32 k = 0; k < NI * N; ++k) { Cut[k] = G.V[k] > 0.f ? FMath::Max(0.f, T[k] - Base[k]) : G.V[k]; if (Cut[k] < G.V[k] - 1.f) ++G.Sloped; }
		// one 3x3 blur of the hills (where there are hills all round), then never up into the snow: M_Ground paints snow on gentle ground
		// from 60 m (Tools/world_material.py SnowFrom, its noise from about 58.4 m), so the land and the hills together ease under TopZ
		// (2026-11-16: the north-east, where the land already climbs toward the mountains, had a snowfield)
		for (int32 i = 0; i < NI; ++i)
			for (int32 j = 0; j < N; ++j)
			{
				const int32 k = i * N + j;
				float V = Cut[k];
				if (V > 0.f && i > 0 && j > 0 && i + 1 < NI && j + 1 < N)
				{
					float Sum = 0.f;
					for (int32 a = -1; a <= 1; ++a) for (int32 b = -1; b <= 1; ++b) Sum += Cut[(i + a) * N + j + b];
					V = Sum / 9.f;
				}
				if (V > 0.f)
				{
					const float Knee = TopZ - TopSoft, H = Base[k] + V;
					if (H > Knee) { const float Soft = Knee + TopSoft * (1.f - FMath::Exp(-(H - Knee) / TopSoft)); V = FMath::Max(0.f, Soft - Base[k]); ++G.Capped; }
				}
				G.V[k] = V;
			}
		G.Hash = FCrc::MemCrc32(G.V.GetData(), G.V.Num() * sizeof(float));
		G.Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		return G;
	}

	const FGrid& Grid()
	{
		static const FGrid G = BuildGrid();
		return G;
	}
}

float JeebHills::Scale()
{
#if UE_BUILD_SHIPPING
	return 1.f;   // (the packaged game: always as built. The scale shapes the solid ground, so a player who set it alone would walk on other
	               // ground than the server's: online review 2026-11-16)
#else
	static const float S = []()
	{
		float V = CVarScale.GetValueOnAnyThread();
		FParse::Value(FCommandLine::Get(), TEXT("HillScale="), V);
		return FMath::Clamp(V, 0.f, 3.f);
	}();
	return S;
#endif
}

float JeebHills::KeepOut(float X, float Y)
{
	const FVector2D P(X, Y);
	const float D = FVector2D::Distance(P, Plaza), Ds = FVector2D::Distance(P, SteeleC);
	// the rims: round Oak Hill, round Steele Creek, and along the land between them
	const float Rim = FMath::Max3(Smooth(RimOut, RimIn, D), Smooth(SteeleRimOut, SteeleRimIn, Ds), Smooth(CorridorOut, CorridorIn, SegDist(P, Plaza, SteeleC)));
	if (Rim <= 0.f) return 0.f;
	float K = Rim * Smooth(TownFrom, TownTo, BoxGap(X, Y)) * Smooth(SteeleFrom, SteeleTo, Ds);
	if (K <= 0.f) return 0.f;
	K *= Smooth(SquareFrom, SquareTo, FMath::Max(FMath::Abs(X), FMath::Abs(Y)) - JeebTerrain::GroundHalf);
	if (K <= 0.f) return 0.f;
	// the distance past the nearest water's edge (the rivers, the split, the lake: JeebWorld::WaterEdge, exact to 250 m from any water)
	K *= Smooth(RiverFrom, RiverTo, JeebWorld::WaterEdge(X, Y));
	return K;
}

float JeebHills::Raw(float X, float Y)
{
	const float Sc = Scale();
	if (Sc <= 0.f) return 0.f;
	const FVector2D P(X, Y);
	const float D = FVector2D::Distance(P, Plaza);
	if (X < GridX0 + 6000.f || X > GridX1 - 6000.f || Y < GridY0 + 6000.f || Y > GridY1 - 6000.f) return 0.f;
	const float K = KeepOut(X, Y);
	if (K <= 0.f) return 0.f;
	// rising away from each town (the lower of the two wins, so each town sits in its own bowl of hills)
	const float Ring = FMath::Min(Smooth(RingFrom, RingTo, D), Smooth(SteeleFrom, SteeleTo + 6000.f, FVector2D::Distance(P, SteeleC)));
	const float Broad = 0.7f * Perlin(P, 26000.f, 3.1f, 7.7f) + 0.3f * Perlin(P, 15000.f, -1.9f, 4.4f);
	const float Big = Smooth(-0.5f, 0.55f, Broad);   // 0 in the valleys and saddles, 1 on the tops
	const float Small = 0.5f + 0.5f * Perlin(P, 11000.f, -5.3f, 2.2f);
	// ridges and single hills: where a ridged noise crests (its zero line), sharpened, and patchy so some stand alone
	const float Crest = FMath::Square(FMath::Max(0.f, 1.f - 1.6f * FMath::Abs(Perlin(P, 17000.f, 8.2f, -3.6f))));
	const float Ridge = Crest * Smooth(-0.25f, 0.35f, Perlin(P, 23000.f, -2.7f, 6.1f));
	return Sc * K * (Ring * (RingBase + RingTop * Big + Ridges * Ridge) + Bumps * Small * (0.5f + 0.5f * Ring));
}

float JeebHills::Offset(float X, float Y)
{
	const float Lx = X - GridX0, Ly = Y - GridY0;
	if (Lx < 0.f || Ly < 0.f || X >= GridX1 || Y >= GridY1) return 0.f;
	const FGrid& G = Grid();
	const float Gx = Lx / GridStep, Gy = Ly / GridStep;
	const int32 i = FMath::Min(FMath::FloorToInt(Gx), G.NI - 2), j = FMath::Min(FMath::FloorToInt(Gy), G.N - 2);
	const float Fx = Gx - i, Fy = Gy - j;
	const float* V = G.V.GetData();
	const float A = V[i * G.N + j], B = V[(i + 1) * G.N + j], C = V[i * G.N + j + 1], Dd = V[(i + 1) * G.N + j + 1];
	float H = FMath::Lerp(FMath::Lerp(A, B, Fx), FMath::Lerp(C, Dd, Fx), Fy);
	if (H != 0.f && JeebTownLab::Active())   // (the TownLab map: its lab town 300 m east stands on its own ground)
	{
		const JeebTownLab::FSite& S = JeebTownLab::LabSite();
		const float Dx = FMath::Max3(S.X0 - X, 0.f, X - S.X1), Dy = FMath::Max3(S.Y0 - Y, 0.f, Y - S.Y1);
		H *= Smooth(TownFrom, TownTo, FMath::Sqrt(Dx * Dx + Dy * Dy));
	}
	return H;
}

void JeebHills::Prepare()
{
	const FGrid& G = Grid();
	float Top = 0.f;
	for (float V : G.V) Top = FMath::Max(Top, V);
	UE_LOG(LogTemp, Log, TEXT("JEEBHILLS grid %dx%d (%.0f m step) from Steele Creek to %.0f m north of the plaza, scale %.2f, highest %.1f m, %d roads levelled (deepest cut %.1f m, highest bank %.1f m), %d grid points lowered to keep slopes under %.0f deg, %d eased under the snow line (%.0f m), built in %.0f ms; hash=%08x"),
		G.NI, G.N, GridStep / 100.f, (GridX1 - Plaza.X) / 100.f, Scale(), Top / 100.f, G.RoadsN, G.MaxCut / 100.f, G.MaxFill / 100.f, G.Sloped, FMath::RadiansToDegrees(FMath::Atan(MaxSlope)), G.Capped, TopZ / 100.f, G.Ms, G.Hash);
}

void JeebHills::LogStats()
{
	// The finished ground over the forest's ring on the world's 8 m mesh points (what is drawn and walked on): slopes per cell,
	// the hills' height by direction (how high the horizon stands seen from the plaza), and the mesh's sag from the true heights
	const double T0 = FPlatformTime::Seconds();
	const float S = JeebWorld::WorldStep;
	const float Reach = JeebOakForest::Reach + 2000.f;
	const int32 I0 = FMath::FloorToInt((Plaza.X - Reach - JeebWorld::MinX) / S), J0 = FMath::FloorToInt((Plaza.Y - Reach - JeebWorld::MinY) / S);
	const int32 NN = FMath::CeilToInt(2.f * Reach / S) + 2;
	TArray<float> H; H.SetNumUninitialized(NN * NN);
	for (int32 i = 0; i < NN; ++i)
		for (int32 j = 0; j < NN; ++j)
			H[i * NN + j] = JeebTerrain::HeightAt(JeebWorld::MinX + (I0 + i) * S, JeebWorld::MinY + (J0 + j) * S);
	int32 Cells = 0, Over50 = 0, Over70 = 0, Over100 = 0;
	float Steepest = 0.f; FVector2D SteepAt = Plaza;
	float SectorTop[8] = {}, SectorLow[8]; for (float& L : SectorLow) L = 1.0e9f;
	float Peak = -1.0e9f; FVector2D PeakAt = Plaza;
	float Sag = 0.f; FVector2D SagAt = Plaza; int32 SagN = 0, Sag20 = 0; float Sag0 = 0.f; int32 Sag0_20 = 0;
	const float PlazaZ = JeebTerrain::HeightAt(Plaza.X, Plaza.Y);
	for (int32 i = 0; i + 1 < NN; ++i)
		for (int32 j = 0; j + 1 < NN; ++j)
		{
			const float X = JeebWorld::MinX + (I0 + i + 0.5f) * S, Y = JeebWorld::MinY + (J0 + j + 0.5f) * S;
			const float D = FVector2D::Distance(FVector2D(X, Y), Plaza);
			if (D > JeebOakForest::Reach || Offset(X, Y) < 50.f) continue;
			const float A = H[i * NN + j], B = H[(i + 1) * NN + j], C = H[i * NN + j + 1], Dd = H[(i + 1) * NN + j + 1];
			// the two triangles' own slopes (each cell split along its B-C diagonal)
			const float S1 = FVector2D(B - A, C - A).Size() / S, S2 = FVector2D(Dd - C, Dd - B).Size() / S;
			const float Sl = FMath::Max(S1, S2);
			++Cells; Over50 += Sl > 0.5f; Over70 += Sl > 0.7f; Over100 += Sl > 1.f;
			if (FMath::Max(A, Dd) > Peak) { Peak = FMath::Max(A, Dd); PeakAt = FVector2D(X, Y); }
			if (Sl > Steepest) { Steepest = Sl; SteepAt = FVector2D(X, Y); }
			if (D > 25000.f)
			{
				const float Ang = FMath::Atan2(Y - Plaza.Y, X - Plaza.X);
				const int32 Sec = (FMath::RoundToInt(Ang / (PI / 4.f)) + 8) % 8;
				const float Up = 0.25f * (A + B + C + Dd) - PlazaZ;
				SectorTop[Sec] = FMath::Max(SectorTop[Sec], Up); SectorLow[Sec] = FMath::Min(SectorLow[Sec], Up);
			}
			if ((i + j) % 7 == 0) { const float Sg = FMath::Abs(JeebTerrain::HeightAt(X, Y) - JeebTerrain::WorldMeshHeight(X, Y)); ++SagN; Sag20 += Sg > 20.f; if (Sg > Sag) { Sag = Sg; SagAt = FVector2D(X, Y); }
				const float Hf = 0.5f * S; const float Sg0 = FMath::Abs(JeebTerrain::HeightAt(X, Y, true, 0) - 0.5f * (JeebTerrain::HeightAt(X + Hf, Y - Hf, true, 0) + JeebTerrain::HeightAt(X - Hf, Y + Hf, true, 0)));   // (the same without the hills)
				Sag0 = FMath::Max(Sag0, Sg0); Sag0_20 += Sg0 > 20.f; }
		}
	static const TCHAR* Names[8] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
	FString Dirs;
	for (int32 k = 0; k < 8; ++k) if (SectorTop[k] > 0.f) Dirs += FString::Printf(TEXT(" %s %.0f..%.0f"), Names[k], SectorLow[k] / 100.f, SectorTop[k] / 100.f);
	UE_LOG(LogTemp, Log, TEXT("JEEBHILLS stats: %d hill cells (8 m) within %.0f m; finished ground hash=%08x (%d heights, rivers and towns applied); highest ground %.1f m (world) at %.0f %.0f; slope over 0.5 (27 deg) %.1f%%, over 0.7 (35 deg) %.2f%%, over 1 (45 deg, not walkable) %d; steepest %.2f (%.0f deg) at %.0f %.0f; ground over the plaza (m) 250-500 m out by direction:%s; mesh sag up to %.0f cm at %.0f %.0f (over 20 cm at %.1f%% of %d spots; without the hills up to %.0f cm, over 20 cm at %.1f%%); %.0f ms"),
		Cells, JeebOakForest::Reach / 100.f, FCrc::MemCrc32(H.GetData(), H.Num() * sizeof(float)), H.Num(), Peak / 100.f, PeakAt.X, PeakAt.Y, Cells ? 100.f * Over50 / Cells : 0.f, Cells ? 100.f * Over70 / Cells : 0.f, Over100, Steepest, FMath::RadiansToDegrees(FMath::Atan(Steepest)),
		SteepAt.X, SteepAt.Y, *Dirs, Sag, SagAt.X, SagAt.Y, SagN ? 100.f * Sag20 / SagN : 0.f, SagN, Sag0, SagN ? 100.f * Sag0_20 / SagN : 0.f, (FPlatformTime::Seconds() - T0) * 1000.0);
}
