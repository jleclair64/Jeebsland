#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MapRework.h"

// The whole world (2026-10-30), traced from the owner's hand-drawn map (Docs/MapSketch.jpg, with the old village (Blackrock until 2026-11-22) and Oak Hill
// swapped: the village WEST of the river, Oak Hill east of it). Blank for now: the land's shape only (mountains, the river and
// its two branches, the east coast and ocean, the islands, the rocky south-west, level ground for each town); no trees or
// grass outside the original square round the start, which stays exactly as it was (JeebTerrain blends the two).
//
// Scale: one pixel of the sketch is 1.3 m, which puts the old village site about a 4 minute walk from Anchorage, as decided. North is +X,
// east is +Y (the sketch's up and right). The sketch's (400, 870) is the old village site (JeebTerrain::VillageCenter), kept empty for a new town.
namespace JeebWorld
{
	// A smooth 0 .. 1 ramp from A to B that works either way round (A > B ramps down). Unreal's Smooth only works with
	// A < B: called the other way it returns 1 almost everywhere, which once turned the whole world into mountains.
	inline float Smooth(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / ((B - A) == 0.f ? 1.f : (B - A)), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	constexpr float SketchScale = 130.f;   // cm per sketch pixel
	inline FVector2D FromSketch(float Px, float Py) { return FVector2D(-2900.f + (870.f - Py) * SketchScale, -300.f + (Px - 400.f) * SketchScale); }

	// The world's edges (cm). The big terrain mesh is a grid of WorldStep cells over this, with a hole where the original
	// square (JeebTerrain::GroundHalf round the start) is; the edges are chosen so the hole falls exactly on grid lines.
	constexpr float MinX = -126400.f, MaxX = 105600.f, MinY = -36000.f, MaxY = 134400.f;
	constexpr float WorldStep = 800.f;
	constexpr float SeaLevel = -60.f;            // the ocean's surface
	constexpr float RiverLevel = SeaLevel + 40.f; // the rivers' surface (a touch above the sea, so they run into it)

	// ------------------------------------------------------------------ places (sketch pixels)
	struct FTown { const TCHAR* Name; FVector2D Pos; float Flat, Fade; float LabelSide = 0.f; };   // (LabelSide: where the world map's name sits, -1 left of the spot, 0 on it, 1 right)
	inline const TArray<FTown>& Towns()
	{
		static const TArray<FTown> T = {
			{ TEXT("Oak Hill"),      FromSketch(543.f, 875.f),  0.f,    0.f, 1.f },      // moved 100 m west (owner, 2026-11-06): its west edge is the river, its forts and bridge face west over it (Docs/OakHillSketch.webp). Its own hills, not a level disc: OakHill.h (2026-11-11)
			{ TEXT("Silver Point"),  FromSketch(835.f, 455.f),  3800.f, 8000.f },   // a level shelf among the northern peaks
			{ TEXT("Anchorage"),     FromSketch(1110.f, 995.f), 3200.f, 6000.f },
			{ TEXT("Birch Grove"),   FromSketch(760.f, 1280.f), 5000.f, 8000.f },   // the big walled troll town (in PvP 2)
			{ TEXT("Steele Creek"),  FromSketch(380.f, 1580.f), 0.f,    0.f, 1.f },     // the smithing town: its own gentle land, not a level disc (SteeleCreek.h, 2026-11-18)
			{ TEXT("Palmer Square"), FromSketch(1125.f, 1550.f), 3200.f, 6000.f },
		};
		return T;
	}

	// Peaks and ranges (each "^" on the sketch), the rocky south-west, and the islands off the east coast
	inline const TArray<FVector2D>& MountainPoints()
	{
		static const TArray<FVector2D> P = []()
		{
			static const float S[][2] = {
				// north and north-west
				{265,90},{360,105},{445,105},{565,110},{735,90},{840,95},{945,100},{1035,95},{240,150},{300,135},{350,140},{500,150},
				{575,135},{695,105},{605,180},{410,160},{285,195},{520,230},{160,265},{265,250},{390,300},{180,315},{170,420},{330,380},
				// north-east, round Silver Point and through PvP 1 to the coast
				{970,150},{1060,160},{970,230},{1050,230},{700,280},{825,290},{935,290},{1025,330},{1100,385},{1125,465},{1140,540},
				{900,560},{1000,620},{1100,700},{1130,780},{1140,850},{960,780},{860,700},
				// a short range south of Anchorage
				{1050,1160},{1080,1250},{1135,1100},
			};
			TArray<FVector2D> A;
			for (const auto& Q : S) A.Add(FromSketch(Q[0], Q[1]));
			return A;
		}();
		return P;
	}
	inline const TArray<FVector2D>& RockPoints()
	{
		static const TArray<FVector2D> P = { FromSketch(200.f, 1420.f), FromSketch(330.f, 1470.f), FromSketch(520.f, 1580.f), FromSketch(250.f, 1610.f), FromSketch(450.f, 1500.f), FromSketch(560.f, 1450.f) };
		return P;
	}
	inline const TArray<FVector2D>& Islands()
	{
		static const TArray<FVector2D> P = { FromSketch(1380.f, 1345.f), FromSketch(1375.f, 1405.f), FromSketch(1365.f, 1490.f), FromSketch(1345.f, 1555.f), FromSketch(1355.f, 1625.f), FromSketch(1370.f, 1680.f) };
		return P;
	}

	// The coastline, north to south: the land is west of it (smaller Y). Returns its Y at a given X.
	inline float CoastY(float X)
	{
		static const TArray<FVector2D> C = []()
		{
			static const float S[][2] = { {1085,20},{1110,200},{1130,350},{1150,500},{1180,650},{1230,800},{1200,900},{1180,960},{1240,1000},
				{1180,1100},{1150,1200},{1130,1350},{1160,1500},{1180,1620},{1200,1760} };
			TArray<FVector2D> A;
			for (const auto& Q : S) A.Add(FromSketch(Q[0], Q[1]));   // X falls as the sketch goes down the page
			return A;
		}();
		// (2026-11-25, the map rework: headlands and bights along the north-east's sea cliffs, MapRework.h)
		float Base = C.Last().Y;
		if (X >= C[0].X) Base = C[0].Y;
		else
			for (int32 i = 0; i + 1 < C.Num(); ++i)
				if (X >= C[i + 1].X) { const float T = (C[i].X - X) / FMath::Max(C[i].X - C[i + 1].X, 1.f); Base = FMath::Lerp(C[i].Y, C[i + 1].Y, T); break; }
		// Anchorage's bay: the coast curves in between two headlands where it used to stand out in a cape
		if (const float Bay = JeebRework::BayBlend(X); Bay > 0.f) Base = FMath::Lerp(Base, JeebRework::BayLine(X), Bay);
		return Base + JeebRework::CoastWiggle(X);
	}

	// ------------------------------------------------------------------ the river
	// Starts in the northern mountains, winds south past the old village site's east side and forks just south of it: one branch
	// east through the woods to Anchorage on the coast, the other far round the south-west past Steele Creek and back east
	// to the sea by Palmer Square.
	//
	// (2026-11-18, owner: "Can you change the river so it isn't always the same width? ... it could wind more like a real river, and have
	// parts that are wider", a lake between Oak Hill and Steele Creek, and the river north of Oak Hill splitting round an island.) Each
	// river is a dense line (a point every 5 m) with its own half width at every point (Halfs) and how sharply it bends there (Bends,
	// signed: + turning left, so its inside is on the left); the lines wind sideways by a sum of two slow waves, never near the fork, the
	// bridge, Oak Hill's river front or Steele Creek (those reaches are exactly as they were). The lake is a shoreline polygon with an
	// island, measured as a signed distance (FLake). Everything is worked out once, from fixed numbers: the same on every machine.
	// HalfWidth is the river's usual half width (the tests read it); RiverDistance gives the half width where the point is nearest.
	struct FRiver { TArray<FVector2D> Pts; float HalfWidth; FBox2D Bounds; TArray<float> Halfs; TArray<float> Bends; };
	inline TArray<FVector2D> Smooth(const TArray<FVector2D>& C, float Step)
	{
		TArray<FVector2D> Out;
		const int32 N = C.Num();
		auto At = [&](int32 i) { return C[FMath::Clamp(i, 0, N - 1)]; };
		for (int32 i = 0; i + 1 < N; ++i)
		{
			const FVector2D P0 = At(i - 1), P1 = At(i), P2 = At(i + 1), P3 = At(i + 2);
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(P1, P2) / Step));
			for (int32 s = 0; s < Steps; ++s)
			{
				const float T = float(s) / Steps, T2 = T * T, T3 = T2 * T;
				Out.Add(0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3));
			}
		}
		Out.Add(C.Last());
		return Out;
	}
	// Fixed spots the winding keeps away from (the reaches there stay exactly as they were): the fork, the stone bridge (Bridge.cpp
	// CentreX / CentreY), Steele Creek's centre
	inline FVector2D ForkAt() { return FromSketch(505.f, 940.f); }
	inline FVector2D BridgeAt() { return FVector2D(-3550.f, 12110.f); }
	inline FVector2D SteeleAt() { return FromSketch(380.f, 1580.f); }
	// The lake between Oak Hill and Steele Creek (owner's drawing, 2026-11-18): its middle, its two half axes (cm, turned LakeRot degrees
	// clockwise from north), its bays (each: the direction it points, degrees clockwise from north; how far it reaches past the oval, as a
	// share of the radius; its half width in degrees) and its wooded island. The south branch runs along its east side: in at the north,
	// out at the south. Its east shore keeps at least 19 m from Steele Creek's road.
	inline FVector2D LakeAt() { return FVector2D(-50000.f, -3600.f); }
	constexpr float LakeA = 6300.f, LakeB = 7400.f, LakeRot = 20.f;
	constexpr float LakeHalf = 4000.f;     // the lake as a "river" half width: RiverDistance gives LakeHalf + the distance past its shore
	inline FVector2D IsleAt() { return FVector2D(-49600.f, -4400.f); }
	constexpr float IsleA = 2900.f, IsleB = 1900.f, IsleRot = 25.f;
	// How far the water's distances are worked out (cm past a water's edge line): beyond it a point is "far from any water" (1e9). Every
	// user asks within this (the hills' keep-out, 170 m, is the widest)
	constexpr float WaterReach = 25000.f;

	inline float Smooth01(float A, float B, float X) { const float T = FMath::Clamp((X - A) / ((B - A) == 0.f ? 1.f : (B - A)), 0.f, 1.f); return T * T * (3.f - 2.f * T); }

	inline const TArray<FRiver>& Rivers()
	{
		static const TArray<FRiver> R = []()
		{
			TArray<FRiver> A;
			auto ArcLen = [](const TArray<FVector2D>& P) { TArray<float> S; S.SetNumZeroed(P.Num()); for (int32 i = 1; i < P.Num(); ++i) S[i] = S[i - 1] + FVector2D::Distance(P[i], P[i - 1]); return S; };
			// Kind: 0 main, 1 east, 2 south. Amp: how far it winds sideways (cm); L1, L2, Ph1, Ph2: the two waves; Side: 0 both ways, 1 only
			// to its left (the south branch: Steele Creek's road runs along its right bank, so it never winds toward it)
			auto Add = [&](std::initializer_list<FVector2D> SketchPts, float HalfWidth, int32 Kind, float Amp, float L1, float L2, float Ph1, float Ph2, float Side)
			{
				TArray<FVector2D> W;
				for (const FVector2D& Q : SketchPts) W.Add(FromSketch(Q.X, Q.Y));
				TArray<FVector2D> P = Smooth(W, 500.f);
				const int32 N = P.Num();
				TArray<float> S = ArcLen(P);
				TArray<float> Keep; Keep.SetNumUninitialized(N);
				TArray<FVector2D> Out; Out.SetNumUninitialized(N);
				for (int32 i = 0; i < N; ++i)
				{
					float K = FMath::Min(Smooth01(8000.f, 16000.f, FVector2D::Distance(P[i], ForkAt())), Smooth01(20000.f, 35000.f, FVector2D::Distance(P[i], BridgeAt())));
					if (Kind == 0) K = FMath::Min(K, Smooth01(32000.f, 45000.f, P[i].X));   // (Oak Hill's river front and the split, as they were)
					if (Kind == 2) K = FMath::Min(K, Smooth01(16000.f, 25000.f, FVector2D::Distance(P[i], SteeleAt())));
					Keep[i] = K;
					const float KW = FMath::Min(K, Smooth01(12000.f, 20000.f, FVector2D::Distance(P[i], LakeAt())));
					const FVector2D T = (P[FMath::Min(i + 1, N - 1)] - P[FMath::Max(i - 1, 0)]).GetSafeNormal();
					const FVector2D Left(-T.Y, T.X);
					float Wv = 0.65f * FMath::Sin(2.f * PI * S[i] / L1 + Ph1) + 0.35f * FMath::Sin(2.f * PI * S[i] / L2 + Ph2);
					if (Side != 0.f) Wv = Side * (0.5f + 0.5f * Wv);
					Out[i] = P[i] + Left * (Amp * KW * Wv);
				}
				FRiver Rv;
				Rv.Pts = Out;
				Rv.HalfWidth = HalfWidth;
				const TArray<float> S2 = ArcLen(Out);
				Rv.Halfs.SetNumUninitialized(N);
				for (int32 i = 0; i < N; ++i)
				{
					// wider and narrower reaches (a slow wave and a quicker one: pools about 400 m apart), never at the kept reaches
					const float Nz = 0.6f * FMath::Sin(2.f * PI * S2[i] / 41000.f + 1.3f) + 0.4f * FMath::Sin(2.f * PI * S2[i] / 17000.f + 0.4f);
					Rv.Halfs[i] = HalfWidth * (1.f + 0.22f * Nz * Keep[i]);
				}
				// (2026-11-25, the map rework) the east branch ends where it runs into Anchorage's bay (it used to carry on 120 m out over what
				// is now the bay); every point before that is exactly where it was
				if (Kind == 1 && JeebRework::On())
					for (int32 i = 0; i < N; ++i)
						if (Rv.Pts[i].Y - CoastY(Rv.Pts[i].X) > 600.f) { Rv.Pts.SetNum(i + 1); Rv.Halfs.SetNum(i + 1); break; }
				A.Add(Rv);
			};
			// the main river, from its spring in the mountains to the fork south of the old village site and Oak Hill
			Add({ {625,95},{640,140},{700,180},{745,230},{745,280},{700,320},{620,355},{560,395},{525,450},{510,520},{505,600},{500,690},{495,780},{495,870},{505,940} }, 700.f,
				0, 3200.f, 26000.f, 9500.f, 0.6f, 2.2f, 0.f);
			// the east branch, through the woods to Anchorage and out to sea
			Add({ {505,940},{560,960},{650,985},{760,1010},{860,1045},{960,1035},{1050,1015},{1110,1012},{1260,1010} }, 600.f,
				1, 3000.f, 24000.f, 11000.f, 1.7f, 0.9f, 0.f);
			// the south branch, round past Steele Creek and back east along the south to the sea by Palmer Square
			Add({ {505,940},{500,1000},{490,1080},{470,1160},{420,1250},{340,1350},{290,1450},{280,1560},{320,1650},{420,1700},{560,1700},
				{700,1680},{820,1630},{920,1580},{1010,1570},{1100,1585},{1260,1595} }, 650.f,
				2, 4200.f, 30000.f, 12500.f, 2.4f, 4.1f, 1.f);
			// the split north of Oak Hill (owner's drawing, 2026-11-18): a second channel leaves the main river 345 m north of the bridge,
			// curves round the west of a long wooded island (about 340 m long and up to 60 m wide) and rejoins it 55 m north of the bridge
			{
				TArray<FVector2D> W = { {31500.f, 13310.f}, {29000.f, 12500.f}, {25500.f, 8800.f}, {20500.f, 5600.f}, {15000.f, 5300.f}, {10000.f, 7400.f}, {6500.f, 10000.f}, {3600.f, 11700.f}, {2000.f, 12020.f} };
				FRiver Rv;
				Rv.Pts = Smooth(W, 500.f);
				Rv.HalfWidth = 470.f;
				const TArray<float> S = ArcLen(Rv.Pts);
				const float L = S.Last();
				for (int32 i = 0; i < Rv.Pts.Num(); ++i) Rv.Halfs.Add(470.f * (0.75f + 0.25f * Smooth01(0.f, 4000.f, S[i]) * Smooth01(L, L - 4000.f, S[i])));
				A.Add(Rv);
			}
			// how sharply each bends (signed: + turns left), over 15 m either way, smoothed: the inside of a tight bend gets a shallow bar
			for (FRiver& Rv : A)
			{
				const int32 N = Rv.Pts.Num();
				TArray<float> K; K.SetNumZeroed(N);
				for (int32 i = 3; i + 3 < N; ++i)
				{
					const FVector2D T0 = (Rv.Pts[i] - Rv.Pts[i - 3]).GetSafeNormal(), T1 = (Rv.Pts[i + 3] - Rv.Pts[i]).GetSafeNormal();
					const float Turn = FMath::Atan2(T0.X * T1.Y - T0.Y * T1.X, FVector2D::DotProduct(T0, T1));
					const float Len = FMath::Max(1.f, float(FVector2D::Distance(Rv.Pts[i - 3], Rv.Pts[i + 3])));
					const float Kappa = Turn / Len;   // 1 / radius (cm)
					K[i] = FMath::Sign(Kappa) * Smooth01(1.f / 15000.f, 1.f / 4500.f, FMath::Abs(Kappa));
				}
				Rv.Bends.SetNumZeroed(N);
				for (int32 i = 0; i < N; ++i) { float Sum = 0.f; int32 C = 0; for (int32 k = FMath::Max(0, i - 4); k <= FMath::Min(N - 1, i + 4); ++k) { Sum += K[k]; ++C; } Rv.Bends[i] = Sum / C; }
				Rv.Bounds = FBox2D(Rv.Pts).ExpandBy(7000.f);
			}
			return A;
		}();
		return R;
	}

	// ------------------------------------------------------------------ the lake
	struct FLake
	{
		TArray<FVector2D> Shore, Isle;   // closed polygons (no repeated last point)
		FVector2D Origin = FVector2D::ZeroVector;   // the signed-distance grid: Origin + (i, j) * LakeGridStep
		int32 NI = 0, NJ = 0;
		TArray<float> SD;                // signed distance to the water's shore line (cm, negative in the water: inside the shore, outside the island)
		FBox2D Box = FBox2D(ForceInit);  // the grid's extent
	};
	constexpr float LakeGridStep = 400.f;
	inline float PolySD(const TArray<FVector2D>& Poly, const FVector2D& P)
	{
		float Best = 1.0e18f; bool bIn = false;
		const int32 N = Poly.Num();
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			const FVector2D A = Poly[j], B = Poly[i], AB = B - A;
			const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
			Best = FMath::Min(Best, float(FVector2D::DistSquared(P, A + AB * T)));
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < A.X + (P.Y - A.Y) * (B.X - A.X) / (B.Y - A.Y)) bIn = !bIn;
		}
		return bIn ? -FMath::Sqrt(Best) : FMath::Sqrt(Best);
	}
	inline const FLake& Lake()
	{
		static const FLake L = []()
		{
			FLake K;
			const FVector2D C = LakeAt();
			const float Rot = FMath::DegreesToRadians(LakeRot);
			const FVector2D U(FMath::Cos(Rot), FMath::Sin(Rot)), V(-U.Y, U.X);
			struct FBay { float Dir, Reach, Wide; };
			const FBay Bays[] = { { 302.f, 0.50f, 10.f }, { 262.f, 0.62f, 6.5f }, { 233.f, 0.42f, 7.5f }, { 203.f, 0.22f, 12.f }, { 348.f, 0.16f, 14.f }, { 110.f, -0.10f, 35.f } };
			for (int32 i = 0; i < 360; ++i)
			{
				const float F = FMath::DegreesToRadians(float(i));
				const FVector2D D(FMath::Cos(F), FMath::Sin(F));
				const float Xu = FVector2D::DotProduct(D, U), Yv = FVector2D::DotProduct(D, V);
				const float R0 = 1.f / FMath::Sqrt(FMath::Square(Xu / LakeA) + FMath::Square(Yv / LakeB));
				float G = 1.f;
				for (const FBay& B : Bays) { const float Dd = FMath::Fmod(float(i) - B.Dir + 540.f, 360.f) - 180.f; G += B.Reach * FMath::Exp(-FMath::Square(Dd / B.Wide)); }
				G += 0.06f * FMath::Sin(3.f * F + 1.1f) + 0.04f * FMath::Sin(7.f * F + 0.3f);
				K.Shore.Add(C + D * R0 * G);
			}
			const float Ri = FMath::DegreesToRadians(IsleRot);
			const FVector2D Ui(FMath::Cos(Ri), FMath::Sin(Ri)), Vi(-Ui.Y, Ui.X);
			for (int32 i = 0; i < 120; ++i)
			{
				const float F = 2.f * PI * i / 120.f;
				const float G = 1.f + 0.12f * FMath::Sin(2.f * F + 0.7f) + 0.07f * FMath::Sin(5.f * F + 2.f);
				K.Isle.Add(IsleAt() + (Ui * FMath::Cos(F) * IsleA + Vi * FMath::Sin(F) * IsleB) * G);
			}
			const FBox2D B = FBox2D(K.Shore).ExpandBy(WaterReach + 1000.f);
			K.Origin = FVector2D(FMath::FloorToFloat(B.Min.X / LakeGridStep) * LakeGridStep, FMath::FloorToFloat(B.Min.Y / LakeGridStep) * LakeGridStep);
			K.NI = FMath::CeilToInt((B.Max.X - K.Origin.X) / LakeGridStep) + 1; K.NJ = FMath::CeilToInt((B.Max.Y - K.Origin.Y) / LakeGridStep) + 1;
			K.Box = FBox2D(K.Origin, K.Origin + FVector2D(K.NI - 1, K.NJ - 1) * LakeGridStep);
			K.SD.SetNumUninitialized(K.NI * K.NJ);
			const FBox2D Near = FBox2D(K.Shore).ExpandBy(3000.f);
			const float Far = FMath::Sqrt(FMath::Square(LakeA) + FMath::Square(LakeB)) * 2.f;
			for (int32 i = 0; i < K.NI; ++i)
				for (int32 j = 0; j < K.NJ; ++j)
				{
					const FVector2D P = K.Origin + FVector2D(i, j) * LakeGridStep;
					float D;
					if (Near.IsInside(P)) D = FMath::Max(PolySD(K.Shore, P), -PolySD(K.Isle, P));
					else D = FMath::Max(PolySD(K.Shore, P), -Far);   // (far out: the island can't be nearer than the shore)
					K.SD[i * K.NJ + j] = D;
				}
			return K;
		}();
		return L;
	}
	// The lake's signed distance at a point (cm past its shore line; negative in its water), or 1e9 out of its reach
	inline float LakeSD(float X, float Y)
	{
		const FLake& L = Lake();
		if (X <= L.Box.Min.X || Y <= L.Box.Min.Y || X >= L.Box.Max.X || Y >= L.Box.Max.Y) return 1.0e9f;
		const float U = (X - L.Origin.X) / LakeGridStep, V = (Y - L.Origin.Y) / LakeGridStep;
		const int32 I = FMath::Min(FMath::FloorToInt(U), L.NI - 2), J = FMath::Min(FMath::FloorToInt(V), L.NJ - 2);
		const float Fu = U - I, Fv = V - J;
		const float* G = L.SD.GetData();
		return FMath::Lerp(FMath::Lerp(G[I * L.NJ + J], G[I * L.NJ + J + 1], Fv), FMath::Lerp(G[(I + 1) * L.NJ + J], G[(I + 1) * L.NJ + J + 1], Fv), Fu);
	}

	// ------------------------------------------------------------------ the water near a point
	// Every river's nearest stretch, from a grid of 32 m cells that lists the stretches within WaterReach of each (the old way tried every
	// stretch of every river whose box held the point)
	constexpr int32 MaxRivers = 4;
	constexpr float WaterCell = 3200.f;
	struct FWaterIndex { int32 NI = 0, NJ = 0; TArray<TArray<int32>> Cells; };   // (each entry: river * 65536 + segment)
	inline const FWaterIndex& WaterIndex()
	{
		static const FWaterIndex W = []()
		{
			FWaterIndex I;
			I.NI = FMath::CeilToInt((MaxX - MinX) / WaterCell) + 1; I.NJ = FMath::CeilToInt((MaxY - MinY) / WaterCell) + 1;
			I.Cells.SetNum(I.NI * I.NJ);
			const TArray<FRiver>& Rs = Rivers();
			for (int32 r = 0; r < Rs.Num(); ++r)
			{
				float MaxHalf = 0.f; for (float H : Rs[r].Halfs) MaxHalf = FMath::Max(MaxHalf, H);
				const float Pad = WaterReach + MaxHalf + 200.f;
				for (int32 s = 0; s + 1 < Rs[r].Pts.Num(); ++s)
				{
					const FVector2D A = Rs[r].Pts[s], B = Rs[r].Pts[s + 1];
					const int32 I0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.X, B.X) - Pad - MinX) / WaterCell)), I1 = FMath::Min(I.NI - 1, FMath::FloorToInt((FMath::Max(A.X, B.X) + Pad - MinX) / WaterCell));
					const int32 J0 = FMath::Max(0, FMath::FloorToInt((FMath::Min(A.Y, B.Y) - Pad - MinY) / WaterCell)), J1 = FMath::Min(I.NJ - 1, FMath::FloorToInt((FMath::Max(A.Y, B.Y) + Pad - MinY) / WaterCell));
					for (int32 i = I0; i <= I1; ++i) for (int32 j = J0; j <= J1; ++j) I.Cells[i * I.NJ + j].Add(r * 65536 + s);
				}
			}
			return I;
		}();
		return W;
	}
	// One body of water's nearest point: Dr the distance from its middle line (cm), Half its half width there (the water's edge line is
	// Dr = Half; the waterline itself is 1 m further out), Shoal 0..1 on the inside of a tight bend, Flow its downstream direction
	struct FWaterNear { float Dr = 1.0e9f; float Half = 600.f; float Shoal = 0.f; FVector2D Flow = FVector2D::ZeroVector; };
	constexpr int32 LakeBody = MaxRivers;   // (the lake is the last body)
	// Every body's nearest point (rivers 0..3, then the lake), cached per thread for the last point asked (HeightAt asks several times)
	inline const FWaterNear* NearWater(float X, float Y)
	{
		struct FCache { float X = 1.0e30f, Y = 1.0e30f; FWaterNear N[MaxRivers + 1]; };
		thread_local FCache C;
		if (C.X == X && C.Y == Y) return C.N;
		C.X = X; C.Y = Y;
		for (FWaterNear& N : C.N) N = FWaterNear();
		const FWaterIndex& I = WaterIndex();
		const int32 Ci = FMath::FloorToInt((X - MinX) / WaterCell), Cj = FMath::FloorToInt((Y - MinY) / WaterCell);
		if (Ci >= 0 && Cj >= 0 && Ci < I.NI && Cj < I.NJ)
		{
			const TArray<FRiver>& Rs = Rivers();
			const FVector2D P(X, Y);
			float Best[MaxRivers]; int32 BestS[MaxRivers]; float BestT[MaxRivers];
			for (int32 r = 0; r < MaxRivers; ++r) { Best[r] = 1.0e18f; BestS[r] = -1; BestT[r] = 0.f; }
			for (const int32 E : I.Cells[Ci * I.NJ + Cj])
			{
				const int32 r = E >> 16, s = E & 0xFFFF;
				const FVector2D A = Rs[r].Pts[s], AB = Rs[r].Pts[s + 1] - A;
				const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
				const float D2 = FVector2D::DistSquared(P, A + AB * T);
				if (D2 < Best[r]) { Best[r] = D2; BestS[r] = s; BestT[r] = T; }
			}
			for (int32 r = 0; r < MaxRivers && r < Rs.Num(); ++r)
			{
				if (BestS[r] < 0) continue;
				const FRiver& Rv = Rs[r];
				const int32 s = BestS[r]; const float T = BestT[r];
				FWaterNear& N = C.N[r];
				N.Dr = FMath::Sqrt(Best[r]);
				N.Half = FMath::Lerp(Rv.Halfs[s], Rv.Halfs[s + 1], T);
				const FVector2D AB = Rv.Pts[s + 1] - Rv.Pts[s];
				N.Flow = AB.GetSafeNormal();
				const float Bend = FMath::Lerp(Rv.Bends[s], Rv.Bends[s + 1], T);
				const float SideOf = AB.X * (Y - Rv.Pts[s].Y) - AB.Y * (X - Rv.Pts[s].X);   // > 0 on its left
				N.Shoal = (Bend * SideOf > 0.f) ? FMath::Abs(Bend) : 0.f;
			}
		}
		const float Sd = LakeSD(X, Y);
		if (Sd < 1.0e8f) { C.N[LakeBody].Dr = FMath::Max(0.f, LakeHalf + Sd); C.N[LakeBody].Half = LakeHalf; }
		return C.N;
	}
	// Distance from a point to the nearest water's middle, and that water's half width there: Dr - Half is how far past the water's edge
	// line the point is (the nearest water is the one whose edge is nearest). 1e9 farther than WaterReach from every water.
	inline float RiverDistance(float X, float Y, float& OutHalfWidth)
	{
		const FWaterNear* N = NearWater(X, Y);
		int32 B = 0;
		for (int32 b = 1; b <= MaxRivers; ++b) if (N[b].Dr - N[b].Half < N[B].Dr - N[B].Half) B = b;
		OutHalfWidth = N[B].Half;
		return N[B].Dr;
	}
	// How far past the nearest water's edge line (cm; negative in the water, 1e9 far from any)
	inline float WaterEdge(float X, float Y) { float H; const float D = RiverDistance(X, Y, H); return D >= 1.0e8f ? 1.0e9f : D - H; }
	// The nearest flowing water's downstream direction there and how still it is (0 flowing .. 1 still: the lake and where channels meet)
	inline FVector2D FlowAt(float X, float Y, float& OutStill)
	{
		const FWaterNear* N = NearWater(X, Y);
		int32 B = 0;
		for (int32 r = 1; r < MaxRivers; ++r) if (N[r].Dr - N[r].Half < N[B].Dr - N[B].Half) B = r;
		const float Lake = N[LakeBody].Dr - LakeHalf;
		OutStill = 1.f - Smooth01(-500.f, 2500.f, Lake);
		if (JeebRework::On()) OutStill = FMath::Max(OutStill, Smooth01(-4500.f, -500.f, Y - CoastY(X)));   // (and where a river runs out into the sea)
		// where two channels meet (the fork, the split's ends) the water is still too, so overlapping currents never fight
		for (int32 r = 0; r < MaxRivers; ++r)
			if (r != B && N[r].Dr < 1.0e8f) OutStill = FMath::Max(OutStill, 1.f - Smooth01(0.f, 3500.f, (N[r].Dr - N[r].Half) - FMath::Max(0.f, N[B].Dr - N[B].Half)));
		return N[B].Flow;
	}
	// The wooded islands (0 .. 1: none in the water or within 9 m of it, all of it 14 m from it): the lake's, and the long one between the
	// main river and the split north of Oak Hill (the land the split's loop closes round)
	inline bool InSplitLoop(float X, float Y)
	{
		static const TArray<FVector2D> Loop = []()
		{
			TArray<FVector2D> L;
			const TArray<FRiver>& Rs = Rivers();
			if (Rs.Num() < 4) return L;
			const TArray<FVector2D>& Sp = Rs[3].Pts;
			L = Sp;
			// back up the main river from the split's end to its start
			auto NearestI = [&](const FVector2D& P) { int32 Bi = 0; float Bd = 1.0e18f; for (int32 i = 0; i < Rs[0].Pts.Num(); ++i) { const float D = FVector2D::DistSquared(Rs[0].Pts[i], P); if (D < Bd) { Bd = D; Bi = i; } } return Bi; };
			const int32 A = NearestI(Sp[0]), B = NearestI(Sp.Last());
			for (int32 i = B; i >= A; --i) L.Add(Rs[0].Pts[i]);
			return L;
		}();
		if (Loop.Num() < 3) return false;
		return PolySD(Loop, FVector2D(X, Y)) < 0.f;
	}
	inline float IslandWoods(float X, float Y)
	{
		const FBox2D IsleBox = FBox2D(Lake().Isle).ExpandBy(100.f);
		const bool bLakeIsle = IsleBox.IsInside(FVector2D(X, Y));
		const bool bSplit = !bLakeIsle && X > 1500.f && X < 32000.f && Y > 4000.f && Y < 13500.f && InSplitLoop(X, Y);
		if (!bLakeIsle && !bSplit) return 0.f;
		return Smooth01(bLakeIsle ? 450.f : 900.f, bLakeIsle ? 800.f : 1400.f, WaterEdge(X, Y));   // (the lake's island is small: woods from 5 m in)
	}

	// ------------------------------------------------------------------ the land's shape
	inline float Noise(const FVector2D& P, float Scale) { return FMath::PerlinNoise2D(P / Scale); }   // about -1 .. 1
	inline float Ridged(const FVector2D& P)   // mountain ridges: rounded crests and broad valleys, soft rather than jagged (2026-10-30)
	{
		const float A = 1.f - FMath::Abs(Noise(P + FVector2D(1234.f, 567.f), 9000.f));
		const float B = 0.5f + 0.5f * Noise(P + FVector2D(-890.f, 4321.f), 4200.f);
		const float Soft = Smooth(0.f, 1.f, A);   // rounds the crest over instead of a knife edge
		return 0.55f * Soft + 0.35f * B + 0.10f * (0.5f + 0.5f * Noise(P, 1500.f));
	}
	// How high the mountains stand (cm): MountainBase where they start, up to MountainBase + MountainRise on the tallest crests
	constexpr float MountainBase = 1200.f, MountainRise = 3600.f;   // (about 50 m at most: Breath of the Wild / Skyrim scale for a world this size)
	// 0 .. 1: how much a point is "in the mountains" (soft union of every peak on the sketch, and walls along the west,
	// south and north edges of the world)
	inline float MountainMask(const FVector2D& P)
	{
		float Clear = 1.f;
		for (const FVector2D& M : MountainPoints())
		{
			const float D = FVector2D::Distance(P, M);
			if (D < 17000.f) Clear *= 1.f - Smooth(17000.f, 3000.f, D);
		}
		float Mask = 1.f - Clear;
		const float Edge = FMath::Min3(P.Y - MinY, P.X - MinX, MaxX - P.X);   // (the east edge is the ocean)
		Mask = FMath::Max(Mask, Smooth(11000.f, 0.f, Edge));
		return Mask;
	}
	inline float RockMask(const FVector2D& P)
	{
		float Clear = 1.f;
		for (const FVector2D& R : RockPoints()) Clear *= 1.f - Smooth(15000.f, 2000.f, float(FVector2D::Distance(P, R)));
		return 1.f - Clear;
	}
	// The land before the rivers and the sea: rolling lowland, mountains and rocky hills, with each town on level ground
	inline float LandHeight(float X, float Y)
	{
		const FVector2D P(X, Y);
		// open country: broad rolling hills a few metres high (like Hyrule Field), with gentle bumps on them
		float H = 120.f + 170.f * (0.5f + 0.5f * Noise(P, 9000.f)) + 60.f * Noise(P + FVector2D(300.f, 900.f), 2500.f)
			+ 420.f * (0.5f + 0.5f * Noise(P + FVector2D(-5000.f, 2200.f), 26000.f));
		const float M = MountainMask(P);
		if (M > 0.001f) H += FMath::Pow(M, 1.6f) * (MountainBase + MountainRise * Ridged(P));
		const float Rk = RockMask(P);
		if (Rk > 0.001f) H += Rk * (250.f + 700.f * Ridged(P * 1.7f));
		for (const FTown& T : Towns())
		{
			if (T.Fade <= 0.f) continue;
			const float D = FVector2D::Distance(P, T.Pos);
			if (D >= T.Fade) continue;
			static TMap<FString, float> Levels;   // each town's level: the land at its centre, worked out once
			float* L = Levels.Find(T.Name);
			if (!L)
			{
				const FVector2D C = T.Pos;
				float Lv = 120.f + 170.f * (0.5f + 0.5f * Noise(C, 9000.f)) + 420.f * (0.5f + 0.5f * Noise(C + FVector2D(-5000.f, 2200.f), 26000.f));
				const float Mc = MountainMask(C);
				if (Mc > 0.001f) Lv += FMath::Pow(Mc, 1.6f) * (MountainBase + MountainRise * 0.35f);   // a shelf partway up, not on a crest
				L = &Levels.Add(T.Name, Lv);
			}
			const float W = 1.f - Smooth(T.Flat, T.Fade, D);
			H = FMath::Lerp(H, *L, W);
		}
		return H;
	}
	// ------------------------------------------------------------------ the rivers' banks
	// (2026-11-15, owner: "I need the land going down to the river to be less steep, the knight can't climb up it.") The old channel
	// was a bowl 18-20 m across whose sides reached 34 degrees at the water, with a 70 cm step where it met the valley floor: on the fine
	// grounds (the square's 2 m, Oak Hill's 1 m) that read as a 45 degree lip the knight could not walk up. Now one smooth curve runs
	// from the bed up to the valley floor: the water's edge stays where it was (Half + 100 cm from the middle), the middle stays
	// RiverDepth deep, and the steepest part (under the water) is about 25 degrees; the walkable limit is 44.8. UnderBankCap then keeps
	// whatever the land above does (Oak Hill's hills and pads, mountains at the spring) under a slope of BankCapSlope for 15 m past
	// the bank's top, so the climb out never meets a steep lip anywhere. -OldRiverBanks (test builds) brings the old shape back,
	// for comparisons only (give every machine of a test the same switch).
	constexpr float RiverDepth = 240.f;     // the bed in the middle, under RiverLevel (swimming starts at SwimEnterDepth)
	constexpr float RiverFloorUp = 130.f;   // the valley floor over RiverLevel
	constexpr float BankWater = 0.6f;       // the share of the bank's run that lies under the water (where the curve crosses RiverLevel)
	constexpr float BankCapSlope = 0.42f;   // the steepest the land may rise past the bank's top (23 degrees), for BankCapReach
	constexpr float BankCapReach = 1500.f;
	inline bool OldBanks()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		static const bool b = FParse::Param(FCommandLine::Get(), TEXT("OldRiverBanks"));
		return b;
#endif
	}
	// (2026-11-18, the lake and the wider reaches) The bank's run is BankSpan: the old (Half + 100) / BankWater for every river up to 14 m
	// of half width (so the bridge's reach and every old bank keep their exact shape), at most 25 m for wider water (the lake), whose
	// bed then lies level out in the middle. Wide water is deeper (WideDepth: 2.4 m for the rivers, 4.2 m for the lake): only the part
	// under the water stretches, so the waterline stays at Half + 100.
	inline float BankSpan(float Half) { return FMath::Min((Half + 100.f) / BankWater, 2500.f); }
	inline float WideDepth(float Half) { return RiverDepth + 180.f * Smooth01(1400.f, 3000.f, Half); }
	// Where a river's bank meets the valley floor (cm from its middle): the water's edge (Half + 100) is BankWater of the way along the bank
	inline float BankTop(float Half) { return Half + 100.f + (1.f - BankWater) * BankSpan(Half); }
	// The ground of the channel and its bank at Dr from a river's middle (Dr < BankTop): RiverLevel - RiverDepth in the middle,
	// RiverLevel at the water's edge, RiverLevel + RiverFloorUp at the top, level at both ends (a smoothstep: steepest 1.5 x rise / run).
	// Shoal (0 .. 1, the inside of a tight bend): the bed there rises to a sandy bar 35 cm under the water, between 20% and 60% of the
	// half width from the middle and out to the waterline (never in the middle, so it meets the other side's bed smoothly)
	inline float BankHeight(float Dr, float Half, float Shoal = 0.f)
	{
		const float S = BankSpan(Half), D0 = Half + 100.f - BankWater * S;
		const float T = FMath::Clamp((Dr - D0) / S, 0.f, 1.f);
		float Rel = -RiverDepth + (RiverDepth + RiverFloorUp) * T * T * (3.f - 2.f * T);
		if (Rel < 0.f)
		{
			Rel *= WideDepth(Half) / RiverDepth;
			if (Shoal > 0.f && Rel < -35.f) Rel += (-35.f - Rel) * Shoal * Smooth01(0.2f * Half, 0.6f * Half, Dr);
		}
		return RiverLevel + Rel;
	}
	// The highest the land may stand at Dr from a river's middle: the bank itself, then rising gently past its top (the slope eases in
	// over the first 3 m), then ever faster after BankCapReach, so that further out it never touches the land
	inline float BankCapAt(float Dr, float Half)
	{
		const float Top = BankTop(Half);
		if (Dr <= Top) return BankHeight(Dr, Half);
		const float X = Dr - Top;
		const float Rise = X < 300.f ? BankCapSlope * X * X / 600.f : BankCapSlope * (X - 150.f);
		const float Past = FMath::Max(0.f, X - BankCapReach);
		return RiverLevel + RiverFloorUp + Rise + 0.002f * Past * Past;
	}
	// The cap over every water near (X, Y) (the lowest wins, so it has no seam where two waters' nearest stretches meet)
	inline float BankCap(float X, float Y)
	{
		const FWaterNear* N = NearWater(X, Y);
		float Cap = 1.0e9f;
		for (int32 b = 0; b <= MaxRivers; ++b) if (N[b].Dr < 1.0e8f) Cap = FMath::Min(Cap, BankCapAt(N[b].Dr, N[b].Half));
		return Cap;
	}
	// The land kept under the cap, with a soft corner (50 cm) where the two meet, so the cut never shows as a crease. Applied last,
	// after Oak Hill's own shape (JeebTerrain::HeightAt). (NearestRiverD is no longer read: far from every water the cap stands over
	// 57 m, above any land, and NearWater's cache makes the lookup cheap)
	inline float UnderBankCap(float X, float Y, float H, float NearestRiverD)
	{
		if (OldBanks()) return H;
		const float C = BankCap(X, Y);
		if (C > 1.0e8f) return H;
		const float K = 50.f, D = C - H;
		if (D >= K) return H;
		if (D <= -K) return C;
		const float Hh = (K - FMath::Abs(D)) / K;
		return FMath::Min(H, C) - Hh * Hh * K * 0.25f;
	}

	// ------------------------------------------------------------------ the sea cliffs (2026-11-25, MapRework.h)
	// cm the cliff wall's line wanders at a spot (positive: the wall stands further inland)
	// (second pass, owner: "fewer obvious straight lines that make it look zig zaggy": the line used to wander by under 5 m, so the wall
	// ran straight for a hundred metres at a time. Now headlands and bights about 70 m and 26 m across, buttresses about 8 m across,
	// and here and there a narrow inlet cut 20 m and more into the land.)
	inline float SeaCliffRag(float X, float Y)
	{
		const FVector2D P(X, Y);
		const bool bPale = X < JeebRework::PaleToX;   // (the Pale Cove is 180 m long: smaller bends, no inlets)
		float R = (bPale ? 420.f : 1150.f) * Noise(P + FVector2D(-5100.f, 2300.f), 7000.f) + (bPale ? 380.f : 520.f) * Noise(P + FVector2D(700.f, -300.f), 2600.f) + 150.f * Noise(P + FVector2D(-90.f, 410.f), 800.f);
		if (!bPale) R += 2500.f * JeebRework::Ramp(0.27f, 0.47f, FMath::PerlinNoise1D(X / 4300.f + 17.7f)) * (0.75f + 0.5f * FMath::PerlinNoise1D(X / 9000.f + 40.f));
		return R;
	}
	// where a sea stack stands (its Y is measured out from the coast line)
	inline float StackY(const JeebRework::FStack& S)
	{
		static const TArray<float> Ys = []() { TArray<float> A; for (const JeebRework::FStack& K : JeebRework::Stacks()) A.Add(CoastY(K.X) + K.Off); return A; }();
		return Ys[int32(&S - JeebRework::Stacks().GetData())];
	}
	// The sea cliffs at a point: Amount 0 .. 1 (the stretch of coast: 0 away from the cliffs and more than Inland cm inland of the coast
	// line); OutFace 0 .. 1 on the wall itself (between its lip and its foot), OutD cm past the wall's foot line (negative inland)
	inline float SeaCliffNear(float X, float Y, float Inland, float* OutFace = nullptr, float* OutD = nullptr)
	{
		if (OutFace) *OutFace = 0.f;
		if (!JeebRework::InCliffX(X)) return 0.f;
		const float D = Y - CoastY(X);
		if (D < -Inland) return 0.f;
		const JeebRework::FSeaCliff Cl = JeebRework::SeaCliffAt(X);
		if (Cl.A <= 0.f) return 0.f;
		const float Dw = D + Cl.A * SeaCliffRag(X, Y);
		if (OutD) *OutD = Dw;
		if (OutFace) *OutFace = Cl.A * Smooth(-Cl.Run - 450.f, -Cl.Run - 250.f, Dw) * Smooth(450.f, 250.f, Dw);
		return Cl.A;
	}

	// 0 .. 1: on a sea cliff's top within Reach cm of its lip (second pass, owner: "making the grass a bit thicker/denser near the edges
	// to mask the edge": Meadow.cpp)
	inline float SeaCliffLipNear(float X, float Y, float Reach)
	{
		float Dw = 0.f;
		if (SeaCliffNear(X, Y, 8000.f + Reach, nullptr, &Dw) <= 0.f) return 0.f;
		const JeebRework::FSeaCliff Cl = JeebRework::SeaCliffAt(X);
		const float Lip = JeebRework::CliffLip(Cl, X);
		return Dw > Lip ? 0.f : Cl.A * Smooth(Lip - Reach, Lip - Reach * 0.35f, Dw);
	}

	// The full world height (without the original square round the start): the land, then the sea and the coast, then the
	// river valleys and channels
	inline float SeaAndRivers(float X, float Y, float H, float* OutRiverD = nullptr, float* OutRiverHalf = nullptr)
	{
		const FVector2D P(X, Y);
		// The coast: the land eases down to a beach at the shoreline; past it the sea floor drops away
		H += JeebRework::HeadlandRise(X, Y);   // (2026-11-25: the headlands of Anchorage's bay)
		if (JeebRework::NearNeedles(X, Y, 0.f))   // (2026-11-25: the Needles, pillars of rock standing on the land as it is)
			for (const JeebRework::FStack& S : JeebRework::Needles()) { const float Hs = JeebRework::StackHeight(S, X - S.X, Y - S.Off); if (Hs > -1.0e8f) H += Hs; }
		const float D = Y - CoastY(X);   // > 0 out at sea
		// (2026-11-25, the map rework) the north-east's sea cliffs (MapRework.h): along that coast the land is lifted in a wedge that rises
		// toward the sea and ends in a wall, in place of the beach
		const JeebRework::FSeaCliff Cl = D > -JeebRework::CliffWedge ? JeebRework::SeaCliffAt(X) : JeebRework::FSeaCliff();
		if (D > -3500.f || Cl.A > 0.f)
		{
			const float PaleShelf = JeebRework::InPaleX(X) ? Smooth(9500.f, 6000.f, FMath::Abs(X - JeebRework::PaleCoveX)) * Smooth(14000.f, 9000.f, D) : 0.f;   // (the Pale Cove's shallows: light water over sand)
			float Hb = H;   // the old shore: a beach, then the sea floor
			if (D > -3500.f)
			{
				if (D <= 0.f) Hb = FMath::Lerp(H, SeaLevel + 30.f, Smooth(-3500.f, 0.f, D));
				else Hb = SeaLevel + 30.f - FMath::Lerp(1900.f * Smooth(0.f, 5000.f, D), 650.f * Smooth(0.f, 9000.f, D), FMath::Max(JeebRework::BayShelter(X, Y), PaleShelf));   // (Anchorage's bay and the Pale Cove: a broad shallow shelf)
			}
			if (Cl.A > 0.f)
			{
				// (the wall's line wanders by a few metres at two sizes: buttresses and gullies, never a ruled edge)
				const float Dw = D + Cl.A * SeaCliffRag(X, Y);
				const float Lifted = H + FMath::Max(0.f, SeaLevel + Cl.Top + 0.25f * H - H) * Smooth(Cl.Pale > 0.5f ? -6000.f : -JeebRework::CliffWedge, -Cl.Run - 300.f, Dw);   // (the Pale Cove lifts only its last 60 m: a 300 m wedge there reached the Birch Grove to Palmer Square road and moved it)
				const float Foot = SeaLevel + FMath::Lerp(-260.f, 90.f, Cl.Beach);   // straight into deep water, or onto a rocky beach
				const float BeachW = 1000.f * Cl.Beach;
				float Hc;
				if (Dw <= -Cl.Run) Hc = Lifted;
				else if (Dw < 0.f) Hc = FMath::Lerp(Foot, Lifted, JeebRework::CliffStep(-Dw / Cl.Run, Cl.Ledge, X));
				else Hc = Foot - 110.f * Cl.Beach * Smooth(0.f, FMath::Max(BeachW, 1.f), Dw) - FMath::Lerp(1900.f * Smooth(BeachW, BeachW + 5000.f, Dw), 520.f * Smooth(BeachW, BeachW + 11000.f, Dw), PaleShelf);
				H = FMath::Lerp(Hb, Hc, Cl.A);
			}
			else H = Hb;
			if (D > -3500.f)
			{
				for (const FVector2D& I : Islands())   // little islands off the coast
				{
					const float Di = FVector2D::Distance(P, I);
					if (Di < 2600.f) H = FMath::Max(H, SeaLevel + 700.f * FMath::Square(1.f - Di / 2600.f) - 100.f);
				}
				for (const JeebRework::FSkerry& K : JeebRework::Skerries())   // the low rocks off Anchorage's bay
				{
					const float Di = FVector2D::Distance(P, FVector2D(K.X, K.Y));
					if (Di < K.R) H = FMath::Max(H, SeaLevel - 150.f + (K.Top + 150.f) * FMath::Square(1.f - Di / K.R));
				}
				for (const JeebRework::FStack& S : JeebRework::Stacks())   // the sea stacks off the cliffs
				{
					if (FMath::Abs(X - S.X) > S.R * JeebRework::StackReach) continue;
					const float Hs = JeebRework::StackHeight(S, X - S.X, Y - StackY(S));
					if (Hs > -1.0e8f) H = FMath::Max(H, SeaLevel + Hs);
				}
			}
		}
		// The rivers and the lake: a wide valley with a level floor, and a channel down its middle
		float Half;
		const float Dr = RiverDistance(X, Y, Half);
		if (OutRiverD) *OutRiverD = Dr;
		if (OutRiverHalf) *OutRiverHalf = Half;
		if (Dr - Half > 6000.f) return H;
		const float Floor = RiverLevel + RiverFloorUp;
		if (OldBanks())
		{
			if (Dr < 6000.f)
			{
				H = FMath::Lerp(H, FMath::Min(H, Floor), Smooth(6000.f, Half + 500.f, Dr));
				if (Dr < Half + 200.f)
				{
					const float U = FMath::Clamp(Dr / (Half + 200.f), 0.f, 1.f);
					H = FMath::Min(H, RiverLevel - 240.f * (1.f - U * U) + 60.f * U * U);
				}
			}
			return H;
		}
		// each water's valley eases down to the floor, which meets the bank's top (out to 46.7 m past it: the old 60 m from a 7 m river's
		// middle); the channel and its bank are one curve (BankHeight). Every water near counts: the most valley and the lowest bank win,
		// so where two meet (the fork, the split's ends, the lake's inlet and outlet) the ground has no seam
		const FWaterNear* N = NearWater(X, Y);
		float Valley = 0.f, Bank = 1.0e9f;
		for (int32 b = 0; b <= MaxRivers; ++b)
		{
			if (N[b].Dr >= 1.0e8f) continue;
			const float Top = BankTop(N[b].Half);
			Valley = FMath::Max(Valley, Smooth(Top + 4667.f, Top, N[b].Dr));
			if (N[b].Dr < Top) Bank = FMath::Min(Bank, BankHeight(N[b].Dr, N[b].Half, N[b].Shoal));
		}
		H = FMath::Lerp(H, FMath::Min(H, Floor), Valley);
		return FMath::Min(H, Bank);
	}
	// Within Margin cm of a river's edge line (its half width from the middle). Always use this (or measure first, then compare):
	// "RiverDistance(X, Y, Half) < Half + M" in one expression may read Half before RiverDistance has set it, since C++ leaves the
	// order of the two sides open (2026-11-15: MSVC read it first, so the river counted as water only 2.5 m either side of its
	// middle, or not at all, and swimming in it barely ever started).
	inline bool NearRiver(float X, float Y, float Margin)
	{
		float Half = 600.f;
		const float Dr = RiverDistance(X, Y, Half);
		return Dr < Half + Margin;
	}
	inline bool InWater(float X, float Y)
	{
		if (Y - CoastY(X) > 300.f) return true;
		return NearRiver(X, Y, 0.f);
	}
}
