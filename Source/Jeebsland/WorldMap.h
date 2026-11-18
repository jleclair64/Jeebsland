#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// The whole world (2026-10-30), traced from the owner's hand-drawn map (Docs/MapSketch.jpg, with Blackrock and Oak Hill
// swapped: Blackrock WEST of the river, Oak Hill east of it). Blank for now: the land's shape only (mountains, the river and
// its two branches, the east coast and ocean, the islands, the rocky south-west, level ground for each town); no trees or
// grass outside the original square round the start, which stays exactly as it was (JeebTerrain blends the two).
//
// Scale: one pixel of the sketch is 1.3 m, which puts Blackrock about a 4 minute walk from Anchorage, as decided. North is +X,
// east is +Y (the sketch's up and right). Blackrock's sketch spot is the existing village.
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
			{ TEXT("Blackrock"),     FromSketch(400.f, 870.f),  0.f,    0.f, -1.f },      // the existing village (its own ground)
			{ TEXT("Oak Hill"),      FromSketch(543.f, 875.f),  0.f,    0.f, 1.f },      // moved 100 m west (owner, 2026-11-06): its west edge is the river, its forts and bridge face Blackrock (Docs/OakHillSketch.webp). Its own hills, not a level disc: OakHill.h (2026-11-11)
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
		if (X >= C[0].X) return C[0].Y;
		for (int32 i = 0; i + 1 < C.Num(); ++i)
			if (X >= C[i + 1].X) { const float T = (C[i].X - X) / FMath::Max(C[i].X - C[i + 1].X, 1.f); return FMath::Lerp(C[i].Y, C[i + 1].Y, T); }
		return C.Last().Y;
	}

	// ------------------------------------------------------------------ the river
	// Starts in the northern mountains, winds south past Blackrock's east side and forks just south of the town: one branch
	// east through the woods to Anchorage on the coast, the other far round the south-west past Steele Creek and back east
	// to the sea by Palmer Square.
	struct FRiver { TArray<FVector2D> Pts; float HalfWidth; FBox2D Bounds; };
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
	inline const TArray<FRiver>& Rivers()
	{
		static const TArray<FRiver> R = []()
		{
			TArray<FRiver> A;
			auto Add = [&](std::initializer_list<FVector2D> SketchPts, float HalfWidth)
			{
				TArray<FVector2D> W;
				for (const FVector2D& Q : SketchPts) W.Add(FromSketch(Q.X, Q.Y));
				FRiver Rv;
				Rv.Pts = Smooth(W, 1500.f);
				Rv.HalfWidth = HalfWidth;
				Rv.Bounds = FBox2D(Rv.Pts);
				Rv.Bounds = Rv.Bounds.ExpandBy(7000.f);
				A.Add(Rv);
			};
			// the main river, from its spring in the mountains to the fork south of Blackrock and Oak Hill
			Add({ {625,95},{640,140},{700,180},{745,230},{745,280},{700,320},{620,355},{560,395},{525,450},{510,520},{505,600},{500,690},{495,780},{495,870},{505,940} }, 700.f);
			// the east branch, through the woods to Anchorage and out to sea
			Add({ {505,940},{560,960},{650,985},{760,1010},{860,1045},{960,1035},{1050,1015},{1110,1012},{1260,1010} }, 600.f);
			// the south branch, round past Steele Creek and back east along the south to the sea by Palmer Square
			Add({ {505,940},{500,1000},{490,1080},{470,1160},{420,1250},{340,1350},{290,1450},{280,1560},{320,1650},{420,1700},{560,1700},
				{700,1680},{820,1630},{920,1580},{1010,1570},{1100,1585},{1260,1595} }, 650.f);
			return A;
		}();
		return R;
	}
	// Distance from a point to the nearest river's middle, and that river's half width
	inline float RiverDistance(float X, float Y, float& OutHalfWidth)
	{
		const FVector2D P(X, Y);
		float Best = 1.0e9f;
		OutHalfWidth = 600.f;
		for (const FRiver& Rv : Rivers())
		{
			if (!Rv.Bounds.IsInside(P)) continue;
			for (int32 i = 0; i + 1 < Rv.Pts.Num(); ++i)
			{
				const FVector2D A = Rv.Pts[i], AB = Rv.Pts[i + 1] - A;
				const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
				const float D = FVector2D::Distance(P, A + AB * T);
				if (D < Best) { Best = D; OutHalfWidth = Rv.HalfWidth; }
			}
		}
		return Best;
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
	// Where a river's bank meets the valley floor (cm from its middle): the water's edge (Half + 100) is BankWater of the way out
	inline float BankTop(float Half) { return (Half + 100.f) / BankWater; }
	// The ground of the channel and its bank at Dr from a river's middle (Dr < BankTop): RiverLevel - RiverDepth in the middle,
	// RiverLevel at the water's edge, RiverLevel + RiverFloorUp at the top, level at both ends (a smoothstep: steepest 1.5 x rise / run)
	inline float BankHeight(float Dr, float Half)
	{
		const float T = FMath::Clamp(Dr / BankTop(Half), 0.f, 1.f);
		return RiverLevel - RiverDepth + (RiverDepth + RiverFloorUp) * T * T * (3.f - 2.f * T);
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
	// The cap over every river near (X, Y) (the lowest wins, so it has no seam where two rivers' nearest stretches meet at the fork)
	inline float BankCap(float X, float Y)
	{
		const FVector2D P(X, Y);
		float Cap = 1.0e9f;
		for (const FRiver& Rv : Rivers())
		{
			if (!Rv.Bounds.IsInside(P)) continue;
			float Best = 1.0e18f;
			for (int32 i = 0; i + 1 < Rv.Pts.Num(); ++i)
			{
				const FVector2D A = Rv.Pts[i], AB = Rv.Pts[i + 1] - A;
				const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
				Best = FMath::Min(Best, float(FVector2D::DistSquared(P, A + AB * T)));
			}
			Cap = FMath::Min(Cap, BankCapAt(FMath::Sqrt(Best), Rv.HalfWidth));
		}
		return Cap;
	}
	// The land kept under the cap, with a soft corner (50 cm) where the two meet, so the cut never shows as a crease. Applied last,
	// after Oak Hill's own shape (JeebTerrain::HeightAt).
	inline float UnderBankCap(float X, float Y, float H, float NearestRiverD)
	{
		if (OldBanks() || NearestRiverD > 4500.f) return H;   // (from 3 km past a bank's top the cap stands over 57 m: above any land)
		const float C = BankCap(X, Y);
		const float K = 50.f, D = C - H;
		if (D >= K) return H;
		if (D <= -K) return C;
		const float Hh = (K - FMath::Abs(D)) / K;
		return FMath::Min(H, C) - Hh * Hh * K * 0.25f;
	}

	// The full world height (without the original square round the start): the land, then the sea and the coast, then the
	// river valleys and channels
	inline float SeaAndRivers(float X, float Y, float H, float* OutRiverD = nullptr, float* OutRiverHalf = nullptr)
	{
		const FVector2D P(X, Y);
		// The coast: the land eases down to a beach at the shoreline; past it the sea floor drops away
		const float D = Y - CoastY(X);   // > 0 out at sea
		if (D > -3500.f)
		{
			if (D <= 0.f) H = FMath::Lerp(H, SeaLevel + 30.f, Smooth(-3500.f, 0.f, D));
			else H = SeaLevel + 30.f - 1900.f * Smooth(0.f, 5000.f, D);
			for (const FVector2D& I : Islands())   // little islands off the coast
			{
				const float Di = FVector2D::Distance(P, I);
				if (Di < 2600.f) H = FMath::Max(H, SeaLevel + 700.f * FMath::Square(1.f - Di / 2600.f) - 100.f);
			}
		}
		// The rivers: a wide valley with a level floor, and a channel down its middle
		float Half;
		const float Dr = RiverDistance(X, Y, Half);
		if (OutRiverD) *OutRiverD = Dr;
		if (OutRiverHalf) *OutRiverHalf = Half;
		if (Dr < 6000.f)
		{
			const float Floor = RiverLevel + RiverFloorUp;
			if (OldBanks())
			{
				H = FMath::Lerp(H, FMath::Min(H, Floor), Smooth(6000.f, Half + 500.f, Dr));
				if (Dr < Half + 200.f)
				{
					const float U = FMath::Clamp(Dr / (Half + 200.f), 0.f, 1.f);
					H = FMath::Min(H, RiverLevel - 240.f * (1.f - U * U) + 60.f * U * U);
				}
				return H;
			}
			// the valley eases down to the floor, which meets the bank's top; the channel and its bank are one curve (BankHeight)
			const float Top = BankTop(Half);
			H = FMath::Lerp(H, FMath::Min(H, Floor), Smooth(6000.f, Top, Dr));
			if (Dr < Top) H = FMath::Min(H, BankHeight(Dr, Half));
		}
		return H;
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
