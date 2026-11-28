#pragma once

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WorldMap.h"
#include "OakHill.h"

// Steele Creek (2026-11-18): the smithing town in the southwest forest, inside the river's southwest loop. Header only, so the
// ground, the streets, the grass, the fill and the town actor (ASteeleCreekTown, SteeleCreekTown.cpp) all read the same numbers on
// every machine. It reuses Oak Hill's rebuilt TownLab models (OakHill.h Models(), TownLabSwaps.h), turned and sized its own way.
//
// Coordinates: metres from the town's centre, E east and N north (game cm: X = PosX + N * 100, Y = PosY + E * 100). Door: the
// Unreal yaw the building's door faces (0 north, 90 east, 180 south, 270 west); the model's own door side is its +Y, so the model
// turns by Door - 90. Scale multiplies the model's own Scale. The plan was laid out with Saved/Steele/layout.py (an offline copy
// of the world's land) so no two buildings are closer than about 3 m and no street runs through a wall.
//
// The town has no ground mesh of its own: its land is shaped here (a gentle tilt rising north, level pads under the buildings and
// the forge yard) and drawn by the world's 8 m ground; the streets are the forest's dirt roads (Terrain.h PathDefs Kind 2), drawn
// as ribbons by APaths and respected by the grass, the fill and the trees.
namespace JeebSteele
{
	inline bool Enabled()
	{
#if UE_BUILD_SHIPPING
		return true;
#else
		static const bool bOn = !(FParse::Param(FCommandLine::Get(), TEXT("NoSteele")) && FParse::Param(FCommandLine::Get(), TEXT("JeebShot")));
		return bOn;
#endif
	}
	constexpr float PosX = -95200.f, PosY = -2900.f;   // the town's centre (JeebWorld::Towns() "Steele Creek", FromSketch(380, 1580))
	inline FVector2D ToWorld(float E, float N) { return FVector2D(PosX + N * 100.f, PosY + E * 100.f); }
	inline float EastM(float Y) { return (Y - PosY) / 100.f; }
	inline float NorthM(float X) { return (X - PosX) / 100.f; }
	// Everything the town changes on the land lies in this box (cm); outside it nothing here costs more than this test
	constexpr float BoxHalf = 11000.f;
	inline bool InBox(float X, float Y, float Pad = 0.f) { return FMath::Abs(X - PosX) < BoxHalf + Pad && FMath::Abs(Y - PosY) < BoxHalf + Pad; }
	// Where a player is put by -StartAtSteele / [Jeebsland.Testing] StartAtSteeleCreek / jeeb.Test.GoSteele (the forge yard, facing the smithy)
	constexpr float SpawnE = -6.f, SpawnN = -2.f, SpawnYaw = 90.f;

	// ------------------------------------------------------------------ plots
	struct FPlotDef { const TCHAR* Name; const TCHAR* Model; float E, N, Door, Scale, Lift; bool bNoPad = false; };   // (bNoPad: the palisade, 2026-11-28: sits on the land as it is, no level pad)
	inline const FPlotDef PlotDefs[] = {
		// PLOTS_BEGIN
		{ TEXT("Smithy"),      TEXT("TrollWarForge"),      22.5f, 2.0f, 270.0f, 1.10f, 0.0f },   // the War-forge (2026-11-28; was Oak Hill's smithy model): Grakka's forge, its yard in front
		{ TEXT("Bank"),        TEXT("TrollVault"),         -4.0f, 22.0f, 180.0f, 1.00f, 0.0f },   // the Vault (2026-11-28; was Oak Hill's bank model): Master Orrin's bank
		{ TEXT("Store"),       TEXT("TrollTradingPost"),   -28.0f, 2.0f, 90.0f, 0.85f, 0.0f },   // the Trading post (general store)
		{ TEXT("Stall"),       TEXT("GeneralStore"),       2.0f, -19.0f, 0.0f, 1.00f, 0.0f },
		{ TEXT("Well"),        TEXT("OakHillWell"),        -15.0f, -4.0f, 0.0f, 1.00f, 0.0f },
		{ TEXT("House1"),      TEXT("TrollHut"),           -30.0f, 31.0f, 212.0f, 0.85f, 0.0f },   // hide-and-stone huts (2026-11-28; the old houses' spots), homes for the folk
		{ TEXT("House2"),      TEXT("TrollHut"),           -43.0f, 40.0f, 212.0f, 0.85f, 0.0f },
		{ TEXT("House3"),      TEXT("TrollHut"),           -54.0f, 46.0f, 212.0f, 0.85f, 0.0f },
		{ TEXT("House4"),      TEXT("TrollHut"),           -42.0f, 13.0f, 32.0f, 0.85f, 0.0f },
		{ TEXT("House5"),      TEXT("TrollHut"),           -57.0f, 23.0f, 32.0f, 0.85f, 0.0f },
		{ TEXT("House6"),      TEXT("TrollHut"),           31.0f, -19.0f, 225.0f, 0.85f, 0.0f },
		{ TEXT("House7"),      TEXT("TrollHut"),           15.0f, -35.0f, 45.0f, 0.85f, 0.0f },
		{ TEXT("House8"),      TEXT("TrollHut"),           39.0f, -34.0f, 315.0f, 0.85f, 0.0f },
		{ TEXT("House9"),      TEXT("TrollHut"),           -28.0f, -15.0f, 132.0f, 0.85f, 0.0f },
		{ TEXT("House10"),     TEXT("TrollHut"),           -22.0f, -38.0f, 312.0f, 0.85f, 0.0f },
		{ TEXT("House11"),     TEXT("TrollHut"),           12.0f, 34.0f, 160.0f, 0.85f, 0.0f },
		{ TEXT("GreatHall"),   TEXT("TrollHall"),          -8.0f, 50.0f, 200.0f, 1.35f, 0.0f },   // the troll chieftain's Great Hall (2026-11-27, Docs/TrollTownPlan.md; 13.5 x 21 m, troll-sized door)
		{ TEXT("BoneCarver"),  TEXT("TrollBoneCarver"),    36.0f, 18.0f, 290.0f, 0.85f, 0.0f },   // troll weapons and the troll rock shop
		{ TEXT("GrogHall"),    TEXT("TrollGrogHall"),      -31.0f, 56.0f, 150.0f, 0.85f, 0.0f },   // the tavern
		{ TEXT("Tanner"),      TEXT("TrollTanner"),        50.0f, 0.0f, 270.0f, 0.80f, 0.0f },   // troll armour and leather goods
		{ TEXT("BrewHut"),     TEXT("TrollBrewHut"),       -40.0f, -28.0f, 60.0f, 1.20f, 0.0f },   // potions in skull jars
		{ TEXT("FightPit"),    TEXT("TrollFightPit"),      -5.0f, -46.0f, 0.0f, 0.80f, 0.0f },   // the totem ring and fighting pit
		{ TEXT("Watchtower"),  TEXT("TrollWatchtower"),    -66.0f, 31.0f, 120.0f, 1.00f, 0.0f },   // beside the bone gate
		{ TEXT("Cairn"),       TEXT("TrollCairn"),         10.0f, 63.0f, 190.0f, 1.00f, 0.0f },   // the ancestor cairn
		{ TEXT("Caves"),       TEXT("TrollCaves"),         33.0f, 55.0f, 215.0f, 1.30f, 0.0f },   // cave dwellings cut into a rock face, below the quarry
		// PLOTS_END
	};

	// ------------------------------------------------------------------ streets (metres from the centre; dirt roads)
	struct FStreetDef { float Width; const TCHAR* Pts; };
	inline const FStreetDef StreetDefs[] = {
		// STREETS_BEGIN
		{ 9.0f, TEXT("-5 0; 6 0") },   // the forge yard (a wide patch of packed earth between the well and the smithy)
		{ 3.4f, TEXT("-6 1; -15 11; -33 22; -51 33; -71 44; -90 56; -102 73; -101 92; -99 112; -95 132; -91 152; -85 171; -78 190; -70 208; -61 226; -51 243; -41 260; -30 277; -19 294; -7 310; 6 325; 19 340; 32 356; 45 371; 57 386; 70 402; 81 418; 92 435; 103 452; 113 469; 122 487; 131 505; 140 523; 147 541; 154 560; 160 580; 164 599; 168 619; 171 638; 175 658; 178 678; 180 698; 182 718; 184 737; 187 757; 188 777") },   // the high street, then the road north along the river to the fork, facing Oak Hill's southwest road
		{ 3.0f, TEXT("4 -4; 12 -12; 22 -27; 36 -44") },   // the east lane
		{ 3.0f, TEXT("-8 -4; -14 -13; -26 -29; -42 -50; -56 -70; -66 -88") },   // the river lane, down to the landing
		{ 3.0f, TEXT("12 10; 22 22; 34 36; 48 50; 60 62") },   // the mine track, up to the quarry
		{ 3.0f, TEXT("-8 4; -13 14; -14 26; -12 36") },   // (2026-11-28) to the Great Hall, past the bank
		{ 3.0f, TEXT("13 -9; 24 -11; 34 -6; 39 -1") },   // (2026-11-28) to the Tanner, round the forge
		{ 3.0f, TEXT("-14 18; -20 30; -22 40; -26 47") },   // (2026-11-28) to the Grog Hall
		{ 2.6f, TEXT("-25 -28; -30 -28; -34 -26") },   // (2026-11-28) to the Brew hut
		{ 3.0f, TEXT("-8 -5; -9 -18; -6 -33") },   // (2026-11-28) down to the fighting pit
		{ 2.6f, TEXT("5 13; 4 28; 4 44; 8 55") },   // (2026-11-28) up to the ancestor cairn
		// STREETS_END
	};

	// ------------------------------------------------------------------ the quarry's rocks (high-tier ores) and the town's people
	struct FRockDef { float E, N; int32 Ore; };   // Ore: 0 iron, 1 silver, 2 gold, 3 emerald, 4 diamond
	// (ore 5 = coal, 2026-11-27: steel is iron + coal and silver bars silver + coal, so the mine has coal beside its iron and silver)
	inline const FRockDef RockDefs[] = {
		{ 58.f, 68.f, 0 }, { 66.f, 62.f, 1 }, { 52.f, 74.f, 1 }, { 72.f, 70.f, 5 }, { 62.f, 78.f, 5 }, { 70.f, 54.f, 1 },
		{ 78.f, 62.f, 2 }, { 56.f, 84.f, 3 }, { 76.f, 78.f, 0 }, { 84.f, 70.f, 3 }, { 68.f, 86.f, 2 }, { 82.f, 84.f, 4 },
	};

	// ------------------------------------------------------------------ the palisade (2026-11-28, the troll town; Docs/TrollTownPlan.md)
	// A ring of sharpened-log wall pieces (TrollPalisade, 10 m) round the camp: an ellipse about the middle, the bone gate where the high
	// street crosses it (its passage along the street), and no piece where a street, a building or the river is (those are the ways in).
	constexpr float RingE = -5.f, RingN = 8.f, RingRE = 75.f, RingRN = 68.f;
	inline FVector2D RingAt(float A) { return FVector2D(RingE + RingRE * FMath::Cos(A), RingN + RingRN * FMath::Sin(A)); }   // (E, N) metres, A radians
	constexpr float GateHalfGapM = 9.f;   // the gate's own half width along the ring

	// ------------------------------------------------------------------ worked out once
	struct FPlot
	{
		FPlotDef Def;
		const JeebOakHill::FModel* Model = nullptr;
		float Yaw = 0.f, Cs = 1.f, Sn = 0.f, Scale = 1.f, ScaleZ = 1.f;
		FVector2D Centre = FVector2D::ZeroVector;   // game cm
		FVector2D Mid = FVector2D::ZeroVector, Half = FVector2D::ZeroVector;   // its walls' box (game cm, in the model's turned frame)
		float Reach = 0.f;   // cm from Centre to the box's far corner
		float Level = 0.f;   // its pad's height (cm)
		bool bStall = false, bWell = false;
		FVector2D EndA = FVector2D::ZeroVector, EndB = FVector2D::ZeroVector;   // (a palisade piece: its two ends, game cm; it stands on the lower ground of the two)
	};
	// The town's land before the pads: a gentle tilt, rising about 1.6 cm a metre to the north (the hills behind) and a little to the west
	inline float PlaneZ(float E, float N) { return 455.f + 1.6f * N - 0.3f * E; }
	inline void AddRing(TArray<FPlot>& A);
	inline const TArray<FPlot>& Plots()
	{
		static const TArray<FPlot> P = []()
		{
			TArray<FPlot> A;
			for (const FPlotDef& D : PlotDefs)
			{
				FPlot Pt; Pt.Def = D;
				Pt.Model = JeebOakHill::FindModel(D.Model);
				if (!Pt.Model) continue;
				Pt.Scale = Pt.Model->Scale * D.Scale;
				Pt.ScaleZ = (Pt.Model->ScaleZ > 0.f ? Pt.Model->ScaleZ : Pt.Model->Scale) * D.Scale;
				Pt.Yaw = FRotator::NormalizeAxis(D.Door - Pt.Model->DoorYaw);
				Pt.Cs = FMath::Cos(FMath::DegreesToRadians(Pt.Yaw)); Pt.Sn = FMath::Sin(FMath::DegreesToRadians(Pt.Yaw));
				Pt.Centre = ToWorld(D.E, D.N);
				// (Unreal mirrors Blender's Y: the model's walls run from (WX0, -WY1) to (WX1, -WY0))
				const FVector2D Lo(Pt.Model->WX0 * Pt.Scale, -Pt.Model->WY1 * Pt.Scale), Hi(Pt.Model->WX1 * Pt.Scale, -Pt.Model->WY0 * Pt.Scale);
				const FVector2D M = (Lo + Hi) * 0.5f;
				Pt.Mid = Pt.Centre + FVector2D(M.X * Pt.Cs - M.Y * Pt.Sn, M.X * Pt.Sn + M.Y * Pt.Cs);
				Pt.Half = (Hi - Lo) * 0.5f;
				Pt.Reach = float(FVector2D::Distance(Pt.Centre, Pt.Mid)) + Pt.Half.Size();
				Pt.Level = PlaneZ(D.E, D.N);
				Pt.bStall = JeebOakHill::IsStall(D.Model);
				Pt.bWell = FCString::Strcmp(D.Model, TEXT("OakHillWell")) == 0;
				A.Add(Pt);
			}
			AddRing(A);
			return A;
		}();
		return P;
	}
	// Signed distance (cm) from a point to a plot's walls (negative inside)
	inline float WallSD(const FPlot& P, float X, float Y)
	{
		const float DX = X - P.Mid.X, DY = Y - P.Mid.Y;
		const FVector2D Q(FMath::Abs(DX * P.Cs + DY * P.Sn) - P.Half.X, FMath::Abs(-DX * P.Sn + DY * P.Cs) - P.Half.Y);
		return FVector2D(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f)).Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.f);
	}
	// The palisade and its gate (called once from Plots(), after the buildings)
	inline TArray<FVector2D> StreetPlan(int32 s)
	{
		TArray<FVector2D> P2; TArray<FString> Parts; FString(StreetDefs[s].Pts).ParseIntoArray(Parts, TEXT(";"), true);
		for (const FString& Pa : Parts) { TArray<FString> V; Pa.TrimStartAndEnd().ParseIntoArray(V, TEXT(" "), true); if (V.Num() == 2) P2.Add(FVector2D(FCString::Atof(*V[0]), FCString::Atof(*V[1]))); }
		return P2;
	}
	inline void AddRing(TArray<FPlot>& A)
	{
		const JeebOakHill::FModel* Wall = JeebOakHill::FindModel(TEXT("TrollPalisade"));
		const JeebOakHill::FModel* Gate = JeebOakHill::FindModel(TEXT("TrollGate"));
		if (!Wall || !Gate) return;
		static TArray<FString> Names; Names.Reset(); Names.Reserve(256);
		auto Place = [&](const TCHAR* Model, const JeebOakHill::FModel* M, const FVector2D& EN, float Door, bool bNoPad, const FVector2D& EA, const FVector2D& EB)
		{
			const FString& Nm = Names.Add_GetRef(bNoPad ? FString::Printf(TEXT("Wall%d"), Names.Num()) : FString(TEXT("Gate")));
			FPlot Pt; Pt.Def = { *Nm, Model, float(EN.X), float(EN.Y), Door, 1.f, 0.f, bNoPad };
			Pt.Model = M; Pt.Scale = M->Scale; Pt.ScaleZ = M->ScaleZ > 0.f ? M->ScaleZ : M->Scale;
			Pt.Yaw = FRotator::NormalizeAxis(Door - M->DoorYaw);
			Pt.Cs = FMath::Cos(FMath::DegreesToRadians(Pt.Yaw)); Pt.Sn = FMath::Sin(FMath::DegreesToRadians(Pt.Yaw));
			Pt.Centre = ToWorld(EN.X, EN.Y);
			const FVector2D Lo(M->WX0 * Pt.Scale, -M->WY1 * Pt.Scale), Hi(M->WX1 * Pt.Scale, -M->WY0 * Pt.Scale), Md = (Lo + Hi) * 0.5f;
			Pt.Mid = Pt.Centre + FVector2D(Md.X * Pt.Cs - Md.Y * Pt.Sn, Md.X * Pt.Sn + Md.Y * Pt.Cs);
			Pt.Half = (Hi - Lo) * 0.5f;
			Pt.Reach = float(FVector2D::Distance(Pt.Centre, Pt.Mid)) + Pt.Half.Size();
			Pt.Level = PlaneZ(EN.X, EN.Y);
			Pt.EndA = ToWorld(EA.X, EA.Y); Pt.EndB = ToWorld(EB.X, EB.Y);
			A.Add(Pt);
		};
		const int32 NR = 1440;
		TArray<FVector2D> R; for (int32 i = 0; i < NR; ++i) R.Add(RingAt(2.f * PI * i / NR));
		auto Inside = [](const FVector2D& EN) { return FMath::Square((EN.X - RingE) / RingRE) + FMath::Square((EN.Y - RingN) / RingRN) < 1.f; };
		// where the high street (StreetDefs[1]) first crosses the ring going out: the gate, its passage along the street
		const TArray<FVector2D> Hs = StreetPlan(1);
		FVector2D GateEN(0.f, 0.f); bool bGate = false;
		for (int32 i = 0; i + 1 < Hs.Num() && !bGate; ++i)
			if (Inside(Hs[i]) && !Inside(Hs[i + 1]))
			{
				float Lo = 0.f, Hi = 1.f;
				for (int32 k = 0; k < 30; ++k) { const float Mi = 0.5f * (Lo + Hi); if (Inside(FMath::Lerp(Hs[i], Hs[i + 1], Mi))) Lo = Mi; else Hi = Mi; }
				GateEN = FMath::Lerp(Hs[i], Hs[i + 1], Lo);
				const FVector2D D = Hs[i + 1] - Hs[i];
				Place(TEXT("TrollGate"), Gate, GateEN, FMath::RadiansToDegrees(FMath::Atan2(D.X, D.Y)), false, GateEN, GateEN);   // (yaw 0 north, 90 east; D is (E, N))
				bGate = true;
			}
		const int32 NumBuildings = A.Num();
		TArray<TArray<FVector2D>> Others; for (int32 s = 0; s < UE_ARRAY_COUNT(StreetDefs); ++s) Others.Add(s == 1 ? TArray<FVector2D>() : StreetPlan(s));
		// pieces end to end round the ring, each as long as the model, skipping the gate, the other streets, the buildings and the river
		const float PieceM = (Wall->WX1 - Wall->WX0) * Wall->Scale / 100.f;
		float Walked = 0.f; FVector2D From = R[0];
		for (int32 i = 1; i <= NR; ++i)
		{
			const FVector2D To = R[i % NR];
			Walked += float(FVector2D::Distance(R[i - 1], To));
			if (Walked < PieceM) continue;
			const FVector2D EA = From, C = (From + To) * 0.5f, D = To - From;
			From = To; Walked = 0.f;
			bool bSkip = bGate && FVector2D::Distance(C, GateEN) < GateHalfGapM + PieceM * 0.5f;
			for (const FVector2D& Q : { EA, C, To })
			{
				const FVector2D W = ToWorld(Q.X, Q.Y);
				if (JeebWorld::NearRiver(W.X, W.Y, 600.f)) bSkip = true;
				for (int32 b = 0; b < NumBuildings && !bSkip; ++b) if (WallSD(A[b], W.X, W.Y) < 250.f) bSkip = true;
			}
			for (int32 s = 0; s < Others.Num() && !bSkip; ++s)
				for (int32 k = 0; k + 1 < Others[s].Num() && !bSkip; ++k)
				{
					const FVector2D P0 = Others[s][k], AB = Others[s][k + 1] - P0;
					const float T = FMath::Clamp(FVector2D::DotProduct(C - P0, AB) / FMath::Max(AB.SizeSquared(), 0.01f), 0.f, 1.f);
					if (FVector2D::Distance(C, P0 + AB * T) < PieceM * 0.5f + StreetDefs[s].Width * 0.5f + 1.5f) bSkip = true;
				}
			if (bSkip) continue;
			// (its face, the model's +Y, looks out of the camp, square to the ring)
			const FVector2D Out = C - FVector2D(RingE, RingN);
			const float OutYaw = FMath::RadiansToDegrees(FMath::Atan2(Out.X, Out.Y));
			float Door = FMath::RadiansToDegrees(FMath::Atan2(D.X, D.Y)) + 90.f;
			if (FMath::Abs(FRotator::NormalizeAxis(Door - OutYaw)) > 90.f) Door += 180.f;
			// (no row of identical pieces: every other one turned round, the banner side in, and each a degree or two off square)
			const uint32 Hp = (uint32(i) * 2654435761u) ^ 0x9E3779B9u;
			if ((A.Num() - NumBuildings) % 2 == 1) Door += 180.f;
			Door += (float((Hp >> 8) % 1000u) / 1000.f - 0.5f) * 4.f;
			Place(TEXT("TrollPalisade"), Wall, C, FRotator::NormalizeAxis(Door), true, EA, To);
		}
	}
	// A spot just outside a building's door (game cm), Out cm in front of its front wall, and the yaw facing out of the door
	inline FVector2D DoorSpot(const FPlot& P, float Out, float* OutYaw = nullptr)
	{
		const float LX = (P.Model->WX0 + P.Model->WX1) * 0.5f * P.Scale, LY = -P.Model->WY0 * P.Scale + Out;
		if (OutYaw) *OutYaw = P.Def.Door;
		return P.Centre + FVector2D(LX * P.Cs - LY * P.Sn, LX * P.Sn + LY * P.Cs);
	}
	inline const FPlot* FindPlot(const TCHAR* Name)
	{
		for (const FPlot& P : Plots()) if (FCString::Strcmp(P.Def.Name, Name) == 0) return &P;
		return nullptr;
	}

	// The streets as dense points (game cm, about every 60 cm), their widths; a street drawn to the river stops on its bank
	struct FStreet { TArray<FVector2D> Pts; float Width = 300.f; };
	inline const TArray<FStreet>& Streets()
	{
		static const TArray<FStreet> S = []()
		{
			TArray<FStreet> A;
			if (!Enabled()) return A;
			for (const FStreetDef& D : StreetDefs)
			{
				TArray<FVector2D> C;
				TArray<FString> Parts; FString(D.Pts).ParseIntoArray(Parts, TEXT(";"), true);
				for (const FString& Pa : Parts)
				{
					TArray<FString> V; Pa.TrimStartAndEnd().ParseIntoArray(V, TEXT(" "), true);
					if (V.Num() == 2) C.Add(ToWorld(FCString::Atof(*V[0]), FCString::Atof(*V[1])));
				}
				if (C.Num() < 2) continue;
				FStreet St; St.Width = D.Width * 100.f;
				// (a Catmull-Rom curve through the points, as JeebTerrain::SmoothPath draws the other roads)
				const int32 N = C.Num();
				auto At = [&](int32 i) { return C[FMath::Clamp(i, 0, N - 1)]; };
				for (int32 i = 0; i < N - 1; ++i)
				{
					const FVector2D P0 = At(i - 1), P1 = At(i), P2 = At(i + 1), P3 = At(i + 2);
					const int32 Steps = FMath::Max(2, FMath::CeilToInt(FVector2D::Distance(P1, P2) / 60.f));
					for (int32 s = 0; s < Steps; ++s)
					{
						const float T = float(s) / Steps, T2 = T * T, T3 = T2 * T;
						St.Pts.Add(0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3));
					}
				}
				St.Pts.Add(C.Last());
				auto Wet = [](const FVector2D& Q) { return JeebWorld::NearRiver(Q.X, Q.Y, 300.f); };
				while (St.Pts.Num() > 2 && Wet(St.Pts[0])) St.Pts.RemoveAt(0);
				while (St.Pts.Num() > 2 && Wet(St.Pts.Last())) St.Pts.Pop();
				if (St.Pts.Num() >= 2) A.Add(St);
			}
			return A;
		}();
		return S;
	}

	// ------------------------------------------------------------------ the land
	// How much of the town's own shape a point takes (0 .. 1): all of it within about 52 m of the forge yard, fading out by 80 m,
	// and none near the river (its banks stay as they are)
	inline float Mask(float E, float N, float RiverD, float RiverHalf)
	{
		const float R = FMath::Sqrt(FMath::Square(E + 5.f) + FMath::Square(N));
		return JeebWorld::Smooth(80.f, 52.f, R) * JeebWorld::Smooth(RiverHalf + 300.f, RiverHalf + 2000.f, RiverD);
	}
	constexpr float PadFlatCm = 250.f, PadFadeCm = 700.f;   // each pad is level 2.5 m round its walls, easing back over 7 m
	inline bool Covers(float X, float Y) { return InBox(X, Y) && Enabled(); }
	// The ground (cm), given the world's height there and the distance to the nearest river (from JeebWorld::SeaAndRivers)
	inline float Apply(float X, float Y, float H, float RiverD, float RiverHalf, bool bCap = true)
	{
		if (!Covers(X, Y)) return H;
		if (bCap) H = JeebWorld::UnderBankCap(X, Y, H, RiverD);
		const float E = EastM(Y), N = NorthM(X);
		const float M = Mask(E, N, RiverD, RiverHalf);
		if (M <= 0.f) return H;
		float G = FMath::Lerp(H, PlaneZ(E, N), M);
		const float PM = FMath::Min(1.f, M * 3.f);   // pads hold firm out where the town's land already fades into the hills
		// the forge yard: level round its middle
		{
			const float D = FMath::Sqrt(FMath::Square(E - 1.f) + FMath::Square(N)) * 100.f;
			const float K = 1.f - JeebWorld::Smooth(1000.f, 1000.f + PadFadeCm, D);
			if (K > 0.f) G = FMath::Lerp(G, PlaneZ(1.f, 0.f), K * PM);
		}
		for (const FPlot& P : Plots())
		{
			if (P.Def.bNoPad) continue;   // (the palisade sits on the land as it is)
			if (FVector2D::DistSquared(P.Centre, FVector2D(X, Y)) > FMath::Square(P.Reach + PadFlatCm + PadFadeCm)) continue;
			const float K = 1.f - JeebWorld::Smooth(PadFlatCm, PadFlatCm + PadFadeCm, WallSD(P, X, Y));
			if (K > 0.f) G = FMath::Lerp(G, P.Level, K * PM);
		}
		return bCap ? JeebWorld::UnderBankCap(X, Y, G, RiverD) : G;
	}
	// How much a point is the town's own trodden ground (0 .. 1): no rock patches there (the rocky southwest's mask)
	inline float Settled(float X, float Y)
	{
		if (!Covers(X, Y)) return 0.f;
		const float E = EastM(Y), N = NorthM(X);
		return JeebWorld::Smooth(75.f, 55.f, FMath::Sqrt(FMath::Square(E + 5.f) + FMath::Square(N)));
	}

	// ------------------------------------------------------------------ what grass and other code ask
	// On a building's walls (no grass, no leaves), with a margin in metres
	inline bool OnBuilding(float X, float Y, float MarginM = 0.f)
	{
		if (!InBox(X, Y) || !Enabled()) return false;
		for (const FPlot& P : Plots())
		{
			if (FVector2D::DistSquared(P.Centre, FVector2D(X, Y)) > FMath::Square(P.Reach + MarginM * 100.f + 100.f)) continue;
			if (WallSD(P, X, Y) < MarginM * 100.f) return true;
		}
		return false;
	}
	// Distance (cm) to the nearest building's walls (1e6 far from town)
	inline float BuildingDistance(float X, float Y)
	{
		if (!InBox(X, Y, 1000.f) || !Enabled()) return 1.0e6f;
		float D = 1.0e6f;
		for (const FPlot& P : Plots()) D = FMath::Min(D, WallSD(P, X, Y));
		return D;
	}
}
