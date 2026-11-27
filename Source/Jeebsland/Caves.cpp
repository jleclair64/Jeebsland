#include "Caves.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "MapRework.h"
#include "GroundMask.h"
#include "OpenWorld.h"
#include "SkillComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Docs/CavesPlan.md. Every number that decides where a cave is and what shape it has is here, and nothing in it reads anything but the
// finished height (JeebTerrain::HeightAt) and fixed numbers: the same caves on the server and every player's machine.

namespace
{
	TAutoConsoleVariable<float> CVarCaveSky(TEXT("jeeb.Cave.Sky"), 0.08f, TEXT("Caves: the share of the sky light kept with the camera all the way inside (1 none taken; the rock has no sky light of its own inside either way)."));
	TAutoConsoleVariable<float> CVarCaveExposure(TEXT("jeeb.Cave.Exposure"), 0.4f, TEXT("Caves: stops of exposure added with the camera all the way inside (0: as dark as it really is)."));
	TAutoConsoleVariable<float> CVarCaveGlow(TEXT("jeeb.Cave.Glow"), 2.5f, TEXT("Caves: the crystals' own glow (M_CaveCrystal Glow)."));
	TAutoConsoleVariable<float> CVarCaveLight(TEXT("jeeb.Cave.Light"), 450.f, TEXT("Caves: each crystal light's strength (lumens)."));
	TAutoConsoleVariable<float> CVarCaveReach(TEXT("jeeb.Cave.Reach"), 1100.f, TEXT("Caves: each crystal light's reach (cm)."));
	TAutoConsoleVariable<int32> CVarCaveChannel(TEXT("jeeb.Cave.Channel"), 1, TEXT("Caves: 1 the crystal lights light only the cave shells, crystals, ore rocks and people (lighting channel 1), so they cannot shine through the rock onto the hillside; 0 everything."));
	TAutoConsoleVariable<int32> CVarCaveShadows(TEXT("jeeb.Cave.Shadows"), 0, TEXT("Caves: crystal lights casting shadows: 0 none, 1 one per cave, 2 all (each costs the scene drawn six times)."));
	TAutoConsoleVariable<float> CVarCaveNear(TEXT("jeeb.Cave.LightsNear"), 1500.f, TEXT("Caves: a cave's lights are on while the camera is this near its mouth (cm), or inside it."));

	bool GPrepared = false;
	TArray<JeebCaves::FCave> GCaves;
	TArray<FBox2D> GFine;
	uint32 GHash = 0;

	inline float Sm(float A, float B, float X) { return JeebWorld::Smooth01(A, B, X); }
	inline float H(float X, float Y) { return JeebTerrain::HeightAt(X, Y); }
	inline float H(const FVector2D& P) { return JeebTerrain::HeightAt(P.X, P.Y); }

	// The passage's shape along it (cm from the mouth): two bends of opposite hand, a pinch, then the chamber, which closes in a round end
	constexpr float Len = 5900.f;
	void Shape(bool bSea, float A, float Floor0, float& Floor, float& HalfW, float& Height)
	{
		HalfW = FMath::Lerp(270.f, 220.f, Sm(0.f, 600.f, A)) - 50.f * Sm(2400.f, 2700.f, A) * (1.f - Sm(2900.f, 3200.f, A));
		Height = bSea ? FMath::Lerp(560.f, 430.f, Sm(0.f, 1200.f, A)) : FMath::Lerp(470.f, 420.f, Sm(0.f, 600.f, A));
		Height -= 70.f * Sm(2400.f, 2700.f, A) * (1.f - Sm(2900.f, 3200.f, A));
		const float Ch = Sm(3900.f, 4700.f, A), Cl = Sm(5150.f, 5880.f, A);
		HalfW = FMath::Lerp(FMath::Lerp(HalfW, 700.f, Ch), 55.f, Cl);
		Height = FMath::Lerp(FMath::Lerp(Height, 760.f, Ch), 80.f, Cl);
		if (bSea) Floor = FMath::Lerp(JeebWorld::SeaLevel - 170.f, JeebWorld::SeaLevel + 30.f, Sm(0.f, 1400.f, A)) + 0.03f * FMath::Max(0.f, FMath::Min(A, 4300.f) - 1400.f);
		else Floor = Floor0 - 150.f * Sm(0.f, 800.f, A) - 0.05f * FMath::Max(0.f, FMath::Min(A, 4300.f) - 800.f);   // (down 1.5 m over the first 8 m: under the hill sooner)
		Floor -= 12.f * Ch * (1.f - Cl);   // (a shallow bowl in the chamber)
	}
	float Heading(float A, int32 Turn)
	{
		return Turn * FMath::DegreesToRadians(32.f * Sm(1500.f, 2400.f, A) - 64.f * Sm(2600.f, 3600.f, A) + 20.f * Sm(3900.f, 4900.f, A));
	}
	void BuildStations(JeebCaves::FCave& C)
	{
		C.St.Reset();
		FVector2D P = C.Mouth;
		const float Base = FMath::Atan2(C.D.Y, C.D.X);
		for (float A = 0.f; A <= Len - 40.f; A += JeebCaves::Step)
		{
			JeebCaves::FStation S;
			S.A = A; S.P = P;
			const float Hd = Base + Heading(A, C.Turn);
			S.Dir = FVector2D(FMath::Cos(Hd), FMath::Sin(Hd));
			Shape(C.bSea, A, C.Floor0, S.Floor, S.HalfW, S.Height);
			C.St.Add(S);
			const float Hn = Base + Heading(A + 0.5f * JeebCaves::Step, C.Turn);
			P += FVector2D(FMath::Cos(Hn), FMath::Sin(Hn)) * JeebCaves::Step;
		}
	}
	// The least rock (cm) over the roof anywhere past the mouth, or a large negative number as soon as it is under MinWant
	float Cover(const JeebCaves::FCave& C, float MinWant)
	{
		float Least = 1.0e9f;
		for (const JeebCaves::FStation& S : C.St)
		{
			if (S.A < 1300.f) continue;   // (nearer the mouth the frame and the cut cells take care of it)
			const float Want = S.A > 3900.f ? MinWant + 50.f : MinWant;
			for (const float X : { -1.f, 0.f, 1.f })
			{
				const FVector2D Q = S.P + S.Lat() * (X * S.HalfW);
				const float Over = H(Q) - (S.Floor + S.Height);
				if (Over < Want) return -1.0e9f;
				Least = FMath::Min(Least, Over - (Want - MinWant));
			}
		}
		return Least;
	}
	float RoadDist(const FVector2D& P)
	{
		float Best = 1.0e9f;
		for (const TArray<FVector2D>& L : JeebOpen::RoadLines())
			for (int32 i = 0; i < L.Num(); i += 10) Best = FMath::Min(Best, float(FVector2D::DistSquared(P, L[i])));
		return FMath::Sqrt(Best);
	}
	bool Clear(const FVector2D& M)
	{
		if (M.X < JeebWorld::MinX + 9000.f || M.X > JeebWorld::MaxX - 9000.f || M.Y < JeebWorld::MinY + 9000.f || M.Y > JeebWorld::MaxY - 9000.f) return false;   // (well inside the map's edge walls)
		if (JeebWorld::WaterEdge(M.X, M.Y) < 2500.f) return false;
		for (const JeebOpen::FSite& S : JeebOpen::Sites()) if (FVector2D::Distance(M, S.Pos) < S.Radius + 6000.f) return false;
		return RoadDist(M) > 4000.f;
	}

	// A mountain cave: the best mouth in a box (a spot at the foot of a rise, gentle and dry in front, enough rock over the whole passage)
	bool FindMountain(JeebCaves::FCave& Out, const FBox2D& Box, float MinZ, float MaxZ, int32 Turn, float HighBonus)
	{
		struct FCand { FVector2D M, D; float Score; };
		TArray<FCand> Cands;
		int32 Rej[6] = { 0, 0, 0, 0, 0, 0 };
		for (float X = Box.Min.X; X <= Box.Max.X; X += 500.f)
			for (float Y = Box.Min.Y; Y <= Box.Max.Y; Y += 500.f)
			{
				const FVector2D M(X, Y);
				const float F = H(M);
				++Rej[0]; if (F < MinZ || F > MaxZ) continue; ++Rej[1];
				for (int32 k = 0; k < 16; ++k)
				{
					const float Ang = 2.f * PI * k / 16.f;
					const FVector2D D(FMath::Cos(Ang), FMath::Sin(Ang)), L(-D.Y, D.X);
					const float Back = H(M + D * 500.f) - F;
					if (Back < 200.f) continue; ++Rej[2];
					const float F1 = H(M - D * 300.f), F2 = H(M - D * 700.f);
					if (F1 < F - 200.f || F1 > F + 40.f || F2 < F - 450.f || F2 > F + 60.f) continue; ++Rej[3];
					const float L1 = H(M + L * 400.f), L2 = H(M - L * 400.f);
					if (L1 < F - 200.f || L1 > F + 300.f || L2 < F - 200.f || L2 > F + 300.f) continue; ++Rej[4];
					JeebCaves::FCave C; C.Mouth = M; C.D = D; C.Floor0 = F - 5.f; C.Turn = Turn;
					BuildStations(C);
					const float Cv = Cover(C, 300.f);
					if (Cv < 0.f) continue; ++Rej[5];
					Cands.Add({ M, D, FMath::Min(Cv, 1200.f) + 1.5f * FMath::Min(Back, 800.f) - 2.f * FMath::Abs(F - F1) + HighBonus * F });
				}
			}
		Cands.Sort([](const FCand& A, const FCand& B) { return A.Score > B.Score; });
		for (const FCand& Cd : Cands)
		{
			bool bFar = true;
			for (const JeebCaves::FCave& O : GCaves) bFar &= FVector2D::Distance(O.Mouth, Cd.M) > 15000.f;   // (no two caves within 150 m)
			if (!bFar || !Clear(Cd.M)) continue;
			Out.Mouth = Cd.M; Out.D = Cd.D; Out.Floor0 = H(Cd.M) - 5.f; Out.Turn = Turn; Out.Score = Cd.Score;
			BuildStations(Out);
			Out.MinCover = Cover(Out, 300.f);
			UE_LOG(LogTemp, Log, TEXT("JEEBCAVE search %s: %d mouths with rock enough, best %.0f %.0f heading %.0f, ground %.1f m, score %.0f"), *Out.Name, Cands.Num(), Cd.M.X, Cd.M.Y, FMath::RadiansToDegrees(FMath::Atan2(Cd.D.Y, Cd.D.X)), H(Cd.M) / 100.f, Cd.Score);
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("JEEBCAVE search %s: no mouth in X %.0f..%.0f Y %.0f..%.0f (%d with rock enough, none clear of roads and water; spots %d, in height %d, dirs rising behind %d, gentle in front %d, level beside %d; ground at the box middle %.0f)"), *Out.Name, Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, Cands.Num(), Rej[0], Rej[1], Rej[2], Rej[3], Rej[4], H(Box.GetCenter()));
		return false;
	}

	// A sea cave: a sheer, beachless stretch of the north-east wall, the mouth at the wall's foot in deep water
	bool FindSea(JeebCaves::FCave& Out, float X0, float X1, int32 Turn)
	{
		if (!JeebRework::On()) return false;
		float Best = -1.0e9f; JeebCaves::FCave Pick;
		int32 Rs[6] = { 0, 0, 0, 0, 0, 0 };
		for (float X = X0; X <= X1; X += 500.f)
		{
			const JeebRework::FSeaCliff Cl = JeebRework::SeaCliffAt(X);
			++Rs[0]; if (Cl.Top < 2200.f || Cl.Beach > 0.15f) continue; ++Rs[1];
			bool bNear = false;
			for (const float Cx : JeebRework::CoveX) bNear |= FMath::Abs(X - Cx) < 5000.f;
			for (const JeebRework::FStack& K : JeebRework::Stacks()) bNear |= FMath::Abs(X - K.X) < K.R * 1.5f + 800.f;
			if (bNear) continue; ++Rs[2];
			const float Ya = JeebWorld::CoastY(X - 1000.f), Yb = JeebWorld::CoastY(X + 1000.f);
			if (FMath::Abs(Yb - Ya) > 1200.f) continue;   // (not where the line turns sharply: an inlet or a headland's nose)
			FVector2D D = FVector2D(Ya - Yb, 2000.f).GetSafeNormal();   // (perpendicular to the coast, toward the land: -Y)
			if (D.Y > 0.f) D = -D;
			const FVector2D Sea(X, JeebWorld::CoastY(X) + 2500.f);
			float Wall = -1.f;
			for (float S = 0.f; S < 9000.f; S += 50.f) if (H(Sea + D * S) > JeebWorld::SeaLevel + 300.f) { Wall = S; break; }
			if (Wall < 0.f) continue; ++Rs[3];
			const FVector2D M = Sea + D * (Wall - 100.f);
			if (H(M - D * 400.f) > JeebWorld::SeaLevel - 150.f) continue;   // &
			++Rs[4];
			if (H(M + D * 900.f) < JeebWorld::SeaLevel + 1800.f) continue;
			JeebCaves::FCave C; C.bSea = true; C.Mouth = M; C.D = D; C.Floor0 = JeebWorld::SeaLevel - 170.f; C.Turn = Turn;
			BuildStations(C);
			const float Cv = Cover(C, 300.f);
			if (Cv < 0.f) continue;
			const float Score = FMath::Min(Cv, 1500.f) + 0.2f * Cl.Top;
			if (Score > Best) { Best = Score; Pick = C; Pick.Score = Score; Pick.MinCover = Cv; }
		}
		if (Best < -1.0e8f) { UE_LOG(LogTemp, Warning, TEXT("JEEBCAVE search %s: no sea mouth in X %.0f..%.0f (spots %d, sheer %d, clear %d, wall %d, deep %d)"), *Out.Name, X0, X1, Rs[0], Rs[1], Rs[2], Rs[3], Rs[4]); return false; }
		const FString Name = Out.Name;
		Out = Pick; Out.Name = Name;
		UE_LOG(LogTemp, Log, TEXT("JEEBCAVE search %s: best %.0f %.0f heading %.0f, wall top %.1f m, score %.0f"), *Out.Name, Out.Mouth.X, Out.Mouth.Y, FMath::RadiansToDegrees(FMath::Atan2(Out.D.Y, Out.D.X)), H(Out.Mouth + Out.D * 900.f) / 100.f, Out.Score);
		return true;
	}

	// The 2 m ground cells left out at a mouth: any of whose corners or middle lies inside the passage's width + 1 m and under its roof + 1.5 m,
	// within the first 15 m (the frame covers where they were; deeper in the hill is over the roof)
	void FindHoles(JeebCaves::FCave& C)
	{
		C.Holes.Reset();
		const FVector2D M = C.Mouth;
		for (float X0 = FMath::FloorToFloat((M.X - 2200.f) / 200.f) * 200.f; X0 < M.X + 2200.f; X0 += 200.f)
			for (float Y0 = FMath::FloorToFloat((M.Y - 2200.f) / 200.f) * 200.f; Y0 < M.Y + 2200.f; Y0 += 200.f)
			{
				bool bCut = false;
				for (const FVector2D& Q : { FVector2D(X0, Y0), FVector2D(X0 + 200.f, Y0), FVector2D(X0, Y0 + 200.f), FVector2D(X0 + 200.f, Y0 + 200.f), FVector2D(X0 + 100.f, Y0 + 100.f) })
				{
					int32 Bi = 0; float Bd = 1.0e18f;
					for (int32 i = 0; i < C.St.Num() && C.St[i].A <= 1600.f; ++i) { const float Dd = FVector2D::DistSquared(Q, C.St[i].P); if (Dd < Bd) { Bd = Dd; Bi = i; } }
					const JeebCaves::FStation& S = C.St[Bi];
					const float Along = S.A + FVector2D::DotProduct(Q - S.P, S.Dir), Lat = FVector2D::DotProduct(Q - S.P, S.Lat());
					if (Along < -60.f || Along > 1500.f || FMath::Abs(Lat) > S.HalfW + 100.f) continue;
					if (H(Q) > S.Floor + S.Height + 150.f) continue;
					bCut = true; break;
				}
				if (bCut) C.Holes.Add(JeebCaves::CellKey(X0, Y0));
			}
	}

	void PlaceInside(JeebCaves::FCave& C, int32 Index)
	{
		C.Crystals.Reset(); C.Lights.Reset();
		FRandomStream R(9100 + Index * 131);
		auto StationAt = [&](float A) { int32 Bi = 0; for (int32 i = 0; i < C.St.Num(); ++i) if (FMath::Abs(C.St[i].A - A) < FMath::Abs(C.St[Bi].A - A)) Bi = i; return Bi; };
		auto Cluster = [&](int32 Si, int32 K, float Size, bool bLight)
		{
			const JeebCaves::FStation& S = C.St[Si];
			const FVector W = JeebCaves::RingPoint(C, Si, K);
			const FVector Axis(S.P.X, S.P.Y, S.Floor + 0.35f * S.Height);
			FVector In = (Axis - W); In.Z = 0.f; In = In.GetSafeNormal();
			const FVector Base = W + In * 25.f;
			const int32 N = R.RandRange(5, 8);
			for (int32 n = 0; n < N; ++n)
			{
				JeebCaves::FCrystal Cr;
				const FVector Jit(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-0.4f, 0.6f));
				Cr.At = Base + Jit * 35.f * Size;
				Cr.Up = (In * R.FRandRange(0.3f, 0.9f) + FVector(0.f, 0.f, R.FRandRange(0.5f, 1.f)) + FVector(R.FRandRange(-0.3f, 0.3f), R.FRandRange(-0.3f, 0.3f), 0.f)).GetSafeNormal();
				Cr.Size = Size * R.FRandRange(0.45f, 1.f) * (n == 0 ? 1.35f : 1.f);
				C.Crystals.Add(Cr);
			}
			if (bLight) C.Lights.Add(Base + In * 90.f + FVector(0.f, 0.f, 70.f));
		};
		const int32 S1 = StationAt(4500.f), S2 = StationAt(4950.f), S3 = StationAt(5350.f), SP = StationAt(3300.f);
		Cluster(S1, JeebCaves::FloorN + 1, 1.3f, true);
		Cluster(S2, JeebCaves::RingN - 2, 1.f, false);
		Cluster(S3, JeebCaves::FloorN + 2, 1.1f, true);
		Cluster(S3, JeebCaves::RingN - 3, 0.8f, false);
		Cluster(SP, (C.Turn > 0 ? JeebCaves::FloorN + 1 : JeebCaves::RingN - 2), 0.6f, true);
		const JeebCaves::FStation& Sr = C.St[StationAt(4800.f)];
		const FVector2D Rp = Sr.P - Sr.Lat() * (0.42f * Sr.HalfW);
		C.RockAt = FVector(Rp.X, Rp.Y, Sr.Floor - 8.f);
	}
}

bool JeebCaves::On()
{
#if !UE_BUILD_SHIPPING
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("NoCaves"));
	if (bOff) return false;
#endif
	return true;
}

void JeebCaves::Prepare(UWorld* World)
{
	if (GPrepared) return;
	GPrepared = true;
	if (!On() || !World || !World->GetMapName().Contains(TEXT("Field"))) { UE_LOG(LogTemp, Log, TEXT("JEEBCAVE none (switched off, or not the real map)")); return; }
	const double T0 = FPlatformTime::Seconds();
	struct FSpec { const TCHAR* Name; bool bSea; FBox2D Box; float MinZ, MaxZ, High; int32 Turn; FLinearColor Glow; int32 Ore; };
	const FSpec Specs[] = {
		{ TEXT("Storm Peak Cave"), false, FBox2D(FVector2D(84000.f, -27000.f), FVector2D(98000.f, 25000.f)), 5000.f, 9000.f, 0.5f, 1, FLinearColor(0.35f, 0.8f, 1.f), int32(EItemType::Emerald) },
		{ TEXT("West Range Cave"), false, FBox2D(FVector2D(62000.f, -27000.f), FVector2D(84000.f, -5000.f)), 1500.f, 9000.f, 0.f, -1, FLinearColor(0.62f, 0.38f, 1.f), int32(EItemType::GoldOre) },
		{ TEXT("Middle Range Cave"), false, FBox2D(FVector2D(62000.f, 5000.f), FVector2D(84000.f, 45000.f)), 1500.f, 9000.f, 0.f, 1, FLinearColor(1.f, 0.62f, 0.22f), int32(EItemType::SilverOre) },
		{ TEXT("East Mass Cave"), false, FBox2D(FVector2D(78000.f, 45000.f), FVector2D(100000.f, 80000.f)), 1500.f, 9000.f, 0.f, -1, FLinearColor(0.45f, 1.f, 0.55f), -1 },
	};
	for (const FSpec& Sp : Specs)
	{
		FCave C; C.Name = Sp.Name; C.Glow = Sp.Glow; C.Ore = Sp.Ore;
		if (FindMountain(C, Sp.Box, Sp.MinZ, Sp.MaxZ, Sp.Turn, Sp.High)) { C.Glow = Sp.Glow; C.Ore = Sp.Ore; GCaves.Add(C); }
	}
	struct FSeaSpec { const TCHAR* Name; float X0, X1; int32 Turn; FLinearColor Glow; int32 Ore; };
	const FSeaSpec Seas[] = {
		{ TEXT("Gull Cave"), 26000.f, 44000.f, -1, FLinearColor(0.2f, 1.f, 0.82f), int32(EItemType::GoldOre) },
		{ TEXT("Seal Cave"), 51000.f, 72000.f, 1, FLinearColor(0.5f, 0.7f, 1.f), -1 },
		{ TEXT("Kraken Cave"), 78000.f, 99000.f, -1, FLinearColor(1.f, 0.45f, 0.75f), -1 },
	};
	for (const FSeaSpec& Sp : Seas)
	{
		FCave C; C.Name = Sp.Name;
		if (FindSea(C, Sp.X0, Sp.X1, Sp.Turn)) { C.Glow = Sp.Glow; C.Ore = Sp.Ore; GCaves.Add(C); }
	}
	GHash = 0;
	for (int32 i = 0; i < GCaves.Num(); ++i)
	{
		FCave& C = GCaves[i];
		FindHoles(C);
		PlaceInside(C, i);
		C.Box = FBox2D(ForceInit);
		for (const FStation& S : C.St) { C.Box += S.P + S.Lat() * (S.HalfW + 200.f); C.Box += S.P - S.Lat() * (S.HalfW + 200.f); }
		C.Box += C.Mouth - C.D * (HoodEnd + 300.f);
		C.Box = C.Box.ExpandBy(HoodEnd + 1500.f);
		if (!C.bSea)
		{
			const FVector2D Mid = C.Mouth + C.D * 600.f;
			C.Fine = FBox2D(Mid - FVector2D(2400.f, 2400.f), Mid + FVector2D(2400.f, 2400.f));
			GFine.Add(C.Fine);
		}
		for (const FStation& S : C.St) { GHash = FCrc::MemCrc32(&S.P, sizeof(FVector2D), GHash); GHash = FCrc::MemCrc32(&S.Floor, sizeof(float), GHash); }
		const int32 NH = C.Holes.Num();
		GHash = FCrc::MemCrc32(&NH, sizeof(int32), GHash);
		const FStation& Last = C.St.Last();
		UE_LOG(LogTemp, Log, TEXT("JEEBCAVE %d %s%s: mouth %.0f %.0f floor %.0f heading %.0f; chamber %.0f %.0f floor %.0f; %d stations, %.1f m of rock over the roof at least, %d ground cells cut; ore %d at %.0f %.0f %.0f; %d crystals, %d lights"),
			i, *C.Name, C.bSea ? TEXT(" (sea)") : TEXT(""), C.Mouth.X, C.Mouth.Y, C.Floor0, FMath::RadiansToDegrees(FMath::Atan2(C.D.Y, C.D.X)),
			C.St[C.St.Num() * 4 / 5].P.X, C.St[C.St.Num() * 4 / 5].P.Y, C.St[C.St.Num() * 4 / 5].Floor, C.St.Num(), C.MinCover / 100.f, NH, C.Ore, C.RockAt.X, C.RockAt.Y, C.RockAt.Z, C.Crystals.Num(), C.Lights.Num());
		// (for test pictures: an eye outside the mouth, one in the passage and one in the chamber, as -ShotEyes X_Y_A<absolute Z>_yaw_pitch_sun)
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(C.D.Y, C.D.X));
		const FVector2D Out1 = C.Mouth - C.D * 1400.f, Out2 = C.Mouth - C.D * 4000.f;
		const FStation& Sp = C.St[FMath::Min(14, C.St.Num() - 1)];
		const FStation& Sc = C.St[FMath::Min(30, C.St.Num() - 1)];
		UE_LOG(LogTemp, Log, TEXT("JEEBCAVE eyes %d: near %.0f_%.0f_A%.0f_%.0f_-3_0 mid %.0f_%.0f_A%.0f_%.0f_-6_0 passage %.0f_%.0f_A%.0f_%.0f_0_0 chamber %.0f_%.0f_A%.0f_%.0f_-8_0 lookout %.0f_%.0f_A%.0f_%.0f_0_0"), i,
			Out1.X, Out1.Y, FMath::Max(H(Out1), C.Floor0) + 220.f, Yaw, Out2.X, Out2.Y, FMath::Max(H(Out2), C.Floor0) + 700.f, Yaw,
			Sp.P.X, Sp.P.Y, Sp.Floor + 190.f, FMath::RadiansToDegrees(FMath::Atan2(Sp.Dir.Y, Sp.Dir.X)),
			Sc.P.X - Sc.Dir.X * 500.f, Sc.P.Y - Sc.Dir.Y * 500.f, Sc.Floor + 260.f, FMath::RadiansToDegrees(FMath::Atan2(Sc.Dir.Y, Sc.Dir.X)),
			Sp.P.X, Sp.P.Y, Sp.Floor + 190.f, Yaw + 180.f);
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBCAVE all: %d caves (%d in the sea) in %.0f ms; hash=%08x"), GCaves.Num(), GCaves.FilterByPredicate([](const FCave& C) { return C.bSea; }).Num(), (FPlatformTime::Seconds() - T0) * 1000.0, GHash);
}

const TArray<JeebCaves::FCave>& JeebCaves::All() { return GCaves; }
uint32 JeebCaves::Hash() { return GHash; }
const TArray<FBox2D>& JeebCaves::FineBoxes()
{
	if (!GPrepared) UE_LOG(LogTemp, Warning, TEXT("JEEBCAVE the ground's fine patches were asked for before the caves were placed: the mountain caves will have no 2 m ground"));
	return GFine;
}

bool JeebCaves::HoleCell(float X0, float Y0)
{
	for (const FCave& C : GCaves) if (C.Box.IsInside(FVector2D(X0 + 100.f, Y0 + 100.f)) && C.Holes.Contains(CellKey(X0, Y0))) return true;
	return false;
}


// (a sea cliff's wall: only the faces in the mouth's opening go, the wall over the roof stays; whole cells would leave a notch to the top)
bool JeebCaves::CutsAt(const FVector& P)
{
	const FVector2D Q(P.X, P.Y);
	for (const FCave& C : GCaves)
	{
		if (!C.Box.IsInside(Q) || C.Holes.Num() == 0) continue;
		int32 Bi = 0; float Bd = 1.0e18f;
		for (int32 i = 0; i < C.St.Num() && C.St[i].A <= 1600.f; ++i) { const float Dd = FVector2D::DistSquared(Q, C.St[i].P); if (Dd < Bd) { Bd = Dd; Bi = i; } }
		const FStation& S = C.St[Bi];
		const float Along = S.A + FVector2D::DotProduct(Q - S.P, S.Dir), Lat = FVector2D::DotProduct(Q - S.P, S.Lat());
		if (Along < -60.f || Along > 1500.f || FMath::Abs(Lat) > S.HalfW + 60.f) continue;
		if (P.Z > S.Floor + S.Height + 60.f) continue;
		return true;
	}
	return false;
}
bool JeebCaves::Covered(float X, float Y)
{
	const FVector2D P(X, Y);
	for (const FCave& C : GCaves)
	{
		if (!C.Box.IsInside(P)) continue;
		const FVector2D Q = P - C.Mouth;
		const float Along = FVector2D::DotProduct(Q, C.D), Lat = FVector2D::DotProduct(Q, FVector2D(-C.D.Y, C.D.X));
		if (Along > -(HoodEnd + 150.f) && Along < 1600.f && FMath::Abs(Lat) < C.St[0].HalfW + HoodEnd + 150.f) return true;
	}
	return false;
}

float JeebCaves::InsideAt(const FVector& P)
{
	float Best = 0.f;
	const FVector2D P2(P.X, P.Y);
	for (const FCave& C : GCaves)
	{
		if (!C.Box.IsInside(P2)) continue;
		int32 Bi = 0; float Bd = 1.0e18f;
		for (int32 i = 0; i < C.St.Num(); ++i) { const float D = FVector2D::DistSquared(P2, C.St[i].P); if (D < Bd) { Bd = D; Bi = i; } }
		const FStation& S = C.St[Bi];
		const float Lat = FMath::Abs(FVector2D::DotProduct(P2 - S.P, S.Lat())), Along = S.A + FVector2D::DotProduct(P2 - S.P, S.Dir);
		if (Lat > S.HalfW + 150.f || P.Z < S.Floor - 250.f || P.Z > S.Floor + S.Height + 250.f) continue;
		Best = FMath::Max(Best, Sm(150.f, 1000.f, Along));
	}
	return Best;
}

FVector JeebCaves::RingPoint(const FCave& C, int32 Si, int32 K)
{
	const FStation& S = C.St[Si];
	const FVector2D L = S.Lat();
	float X, Z;
	const bool bFloor = K < FloorN;
	if (bFloor) { X = FMath::Lerp(-0.92f, 0.92f, float(K) / (FloorN - 1)) * S.HalfW; Z = 0.f; }
	else
	{
		const float Psi = float(K - FloorN + 1) / float(RingN - FloorN + 1) * PI;
		X = S.HalfW * FMath::Cos(Psi) * (1.f + 0.10f * FMath::Sin(Psi));
		// (the roof's line lopsided, and differently at every station: never a mason's arch)
		Z = S.Height * FMath::Pow(FMath::Sin(Psi), 0.8f) * (1.f + 0.10f * FMath::Sin(2.f * Psi + S.A / 900.f + C.Turn));
	}
	FVector Q(S.P.X + L.X * X, S.P.Y + L.Y * X, S.Floor + Z);
	const float Fade = FMath::Lerp(0.5f, 1.f, Sm(0.f, 250.f, S.A)) * FMath::Clamp(S.HalfW / 150.f, 0.f, 1.f);
	if (!bFloor)
	{
		const float Nx = X / FMath::Max(S.HalfW, 1.f), Nz = (Z - 0.4f * S.Height) / FMath::Max(S.Height, 1.f);
		const FVector Out = FVector(L.X * Nx, L.Y * Nx, Nz).GetSafeNormal();
		const float Rough = 32.f * FMath::PerlinNoise3D(Q / 280.f + FVector(3.1f, -7.7f, 1.9f)) + 14.f * FMath::PerlinNoise3D(Q / 95.f + FVector(-2.3f, 5.5f, 8.8f)) + 6.f * FMath::PerlinNoise3D(Q / 41.f + FVector(9.4f, 1.2f, -3.6f));
		Q += Out * Rough * Fade;
	}
	else Q.Z += 3.f * FMath::PerlinNoise3D(Q / 130.f + FVector(1.7f, 4.4f, -2.2f));
	return Q;
}

void JeebCaves::BuildShells(AActor* Owner, UMaterialInterface* Mat, bool bServer)
{
	uint32 MeshHash = 0;
	int32 AllTris = 0;
	for (const FCave& C : GCaves)
	{
		const int32 NS = C.St.Num();
		constexpr int32 NH = 8;   // frame rows out from the rim
		// rows: the frame from its sunk edge (row 0) in to the rim, then every station; then the end cap's middle
		TArray<FVector> V; TArray<FVector2D> UV, UV1, UV2; TArray<FLinearColor> Col; TArray<int32> Tri;
		const int32 Rows = NH + NS;
		V.SetNum(Rows * RingN); UV2.SetNumZeroed(Rows * RingN); Col.SetNum(Rows * RingN);
		const FStation& S0 = C.St[0];
		const FVector2D L0 = S0.Lat();
		const float Cz = S0.Floor + 0.42f * S0.Height;
		const float Lt = C.bSea ? 160.f : 45.f, Lv = C.bSea ? 50.f : 30.f;
		for (int32 k = 0; k < RingN; ++k)
		{
			const FVector R = RingPoint(C, 0, k);
			const FVector2D Rxy(R.X, R.Y);
			const float Xk = FVector2D::DotProduct(Rxy - S0.P, L0);
			const float Phi = FMath::Atan2(Xk, -(R.Z - Cz));
			const float W = Sm(FMath::DegreesToRadians(55.f), FMath::DegreesToRadians(85.f), FMath::Abs(Phi));
			const FVector2D E(FMath::Sin(Phi), -FMath::Cos(Phi));   // (out from the mouth's middle, in its plane: lateral, up)
			const float Thick = Lt + Lv * FMath::PerlinNoise1D(Phi * 1.3f + C.Mouth.X / 3000.f);
			// where the line along the heading at height Z through this plane point meets the ground (cm along; negative: in front)
			auto Cross = [&](const FVector2D& Pp, float Z) -> float
			{
				if (H(Pp - C.D * 1500.f) >= Z) return -1500.f;
				float Prev = -1500.f;
				for (float S = -1450.f; S <= 3000.f; S += 50.f)
				{
					if (H(Pp + C.D * S) >= Z)
					{
						float Lo = Prev, Hi = S;
						for (int32 b = 0; b < 6; ++b) { const float Mm = 0.5f * (Lo + Hi); if (H(Pp + C.D * Mm) >= Z) Hi = Mm; else Lo = Mm; }
						return 0.5f * (Lo + Hi);
					}
					Prev = S;
				}
				return 3000.f;
			};
			const float S0c = Cross(Rxy, R.Z);
			const float Lf0 = R.Z - H(Rxy);
			for (int32 j = 1; j <= NH; ++j)
			{
				const float T = HoodEnd * float(j) / NH;
				// the upper rim: out from the middle in the mouth's plane, then along the heading onto the slope or the wall's face
				const FVector2D Pp = Rxy + L0 * (E.X * T);
				const float Pz = R.Z + E.Y * T;
				const float Lu = T <= HoodCover ? FMath::Lerp(S0c, Thick, Sm(0.f, 150.f, T)) : FMath::Lerp(Thick, -120.f, Sm(HoodCover, HoodEnd, T));
				const float Sc = Cross(Pp, Pz);
				const FVector Su(Pp.X + C.D.X * (Sc - Lu), Pp.Y + C.D.Y * (Sc - Lu), Pz);
				// the floor's rim: forward over the ground in front, just over it, then down into it
				const FVector2D Dir2 = -C.D + L0 * (FMath::Sign(Xk) * 0.25f * FMath::Abs(Xk) / FMath::Max(S0.HalfW, 1.f));
				const FVector2D Fxy = Rxy + Dir2 * T;
				const float Lf = T <= 100.f ? FMath::Lerp(Lf0, 4.f, Sm(0.f, 100.f, T)) : (T <= HoodCover ? 4.f : FMath::Lerp(4.f, -60.f, Sm(HoodCover, HoodEnd, T)));
				const FVector Sf(Fxy.X, Fxy.Y, H(Fxy) + Lf);
				FVector Q = FMath::Lerp(Sf, Su, W);
				// (a little lumpiness on the frame's face, none at its rim)
				Q += FVector(0.f, 0.f, 18.f * FMath::PerlinNoise3D(Q / 170.f + FVector(4.2f, -1.1f, 2.6f)) * Sm(0.f, 200.f, T) * W);
				const int32 Row = NH - j;
				V[Row * RingN + k] = Q;
				// (2026-11-26: the frame's outer part takes the slope's own grass and stone, so it reads as the hill folding in, not a stone dome)
				const float Gb = C.bSea ? 0.f : Sm(150.f, HoodEnd * 0.8f, T);
				Col[Row * RingN + k] = Gb > 0.f ? FMath::Lerp(FLinearColor(0.f, 1.f, 0.f, 0.f), JeebGround::At(Q.X, Q.Y, Q.Z, JeebGround::Upness(Q.X, Q.Y)).Color(), Gb) : FLinearColor(0.f, 1.f, 0.f, 0.f);
			}
		}
		for (int32 s = 0; s < NS; ++s)
			for (int32 k = 0; k < RingN; ++k)
			{
				const int32 I = (NH + s) * RingN + k;
				V[I] = RingPoint(C, s, k);
				UV2[I] = FVector2D(Sm(0.f, 700.f, C.St[s].A), 0.f);
				Col[I] = k < FloorN ? FLinearColor(0.55f, 0.75f, 0.f, 0.f) : FLinearColor(0.f, 1.f, 0.f, 0.f);
			}
		const int32 CapI = V.Num();
		{
			const FStation& E = C.St.Last();
			V.Add(FVector(E.P.X + E.Dir.X * 40.f, E.P.Y + E.Dir.Y * 40.f, E.Floor + 0.3f * E.Height));
			UV2.Add(FVector2D(1.f, 0.f)); Col.Add(FLinearColor(0.f, 1.f, 0.f, 0.f));
		}
		for (int32 r = 0; r + 1 < Rows; ++r)
			for (int32 k = 0; k < RingN; ++k)
			{
				const int32 K1 = (k + 1) % RingN;
				const int32 A = r * RingN + k, B = r * RingN + K1, Cc = (r + 1) * RingN + K1, D = (r + 1) * RingN + k;
				Tri.Append({ A, B, Cc, A, Cc, D });
			}
		// one winding for the whole sheet (it is one surface, the passage's inside running on into the frame's outside): a triangle's cross
		// product points into the rock, as the ground's; checked on the roof of the passage's fifth ring
		{
			const int32 r = NH + 4, k = RingN / 2 + 2;
			const int32 A = r * RingN + k, B = r * RingN + k + 1, Cc = (r + 1) * RingN + k + 1;
			const FVector Cr = FVector::CrossProduct(V[B] - V[A], V[Cc] - V[A]);
			if (Cr.Z < 0.f) for (int32 t = 0; t + 2 < Tri.Num(); t += 3) Swap(Tri[t + 1], Tri[t + 2]);
		}
		{
			const int32 Last = (Rows - 1) * RingN;
			const FVector Fw(C.St.Last().Dir.X, C.St.Last().Dir.Y, 0.f);
			for (int32 k = 0; k < RingN; ++k)
			{
				int32 A = Last + k, B = Last + (k + 1) % RingN;
				if (FVector::DotProduct(FVector::CrossProduct(V[B] - V[A], V[CapI] - V[A]), Fw) < 0.f) Swap(A, B);
				Tri.Append({ A, B, CapI });
			}
		}
		TArray<FVector> N; N.SetNumZeroed(V.Num());
		for (int32 t = 0; t + 2 < Tri.Num(); t += 3)
		{
			const FVector Cr = FVector::CrossProduct(V[Tri[t + 1]] - V[Tri[t]], V[Tri[t + 2]] - V[Tri[t]]);
			for (int32 q = 0; q < 3; ++q) N[Tri[t + q]] -= Cr;
		}
		for (FVector& Nv : N) Nv = Nv.IsNearlyZero() ? FVector::UpVector : Nv.GetSafeNormal();
		UV.SetNum(V.Num()); UV1.SetNum(V.Num());
		for (int32 i = 0; i < V.Num(); ++i)
		{
			UV[i] = FVector2D(V[i].X / 1000.f, V[i].Y / 1000.f);
			// (snow on the frame's tops as on the ground round it; none inside)
			UV1[i] = (i < NH * RingN && !bServer) ? FVector2D(JeebOpen::SnowRaw(V[i].X, V[i].Y, V[i].Z, N[i].Z) + 1.f, 0.f) : FVector2D::ZeroVector;
		}
		UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(Owner);
		PM->SetupAttachment(Owner->GetRootComponent());
		PM->bUseAsyncCooking = true;
		PM->RegisterComponent();
		PM->CreateMeshSection_LinearColor(0, V, Tri, N, UV, UV1, UV2, TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), true);
		PM->SetCollisionProfileName(TEXT("BlockAll"));
		PM->ComponentTags.Add(FName(TEXT("JeebCave")));
		PM->bCastShadowAsTwoSided = true;   // (the rock keeps the sun out from either side)
		PM->SetLightingChannels(true, true, false);   // (channel 1: the cave lights reach only the shells, crystals, ore rocks and people, ACaves::Tick)
		if (!bServer) { if (Mat) PM->SetMaterial(0, Mat); }
		else PM->SetVisibility(false);
		MeshHash = FCrc::MemCrc32(V.GetData(), V.Num() * sizeof(FVector), MeshHash);
		AllTris += Tri.Num() / 3;
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBCAVE shells: %d meshes, %d triangles; hash=%08x"), GCaves.Num(), AllTris, MeshHash);
}

float JeebCaves::SkyKeep() { return FMath::Clamp(CVarCaveSky.GetValueOnGameThread(), 0.f, 1.f); }
float JeebCaves::ExposureLift() { return CVarCaveExposure.GetValueOnGameThread(); }

// ------------------------------------------------------------------------------------------------ the crystals and their lights
float ACaves::CameraInside = 0.f;

ACaves::ACaves()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ACaves::BeginPlay()
{
	Super::BeginPlay();
	CameraInside = 0.f;
	if (IsNetMode(NM_DedicatedServer)) { SetActorTickEnabled(false); return; }
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_CaveCrystal.M_CaveCrystal"));
	if (!Base) { UE_LOG(LogTemp, Warning, TEXT("JEEBCAVE M_CaveCrystal is missing: run Tools/BuildWorldMaterial.ps1; the crystals use M_Color")); Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color")); }
	const TArray<JeebCaves::FCave>& All = JeebCaves::All();
	for (int32 c = 0; c < All.Num(); ++c)
	{
		const JeebCaves::FCave& C = All[c];
		// six-sided prisms with a pointed tip, each its own flat faces (a cut gem, not a rounded one)
		TArray<FVector> V, N; TArray<int32> T; TArray<FLinearColor> Col;
		FRandomStream R(500 + c * 17);
		for (const JeebCaves::FCrystal& Cr : C.Crystals)
		{
			const FVector Up = Cr.Up, Ax = FVector::CrossProduct(Up, FMath::Abs(Up.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal(), Ay = FVector::CrossProduct(Up, Ax);
			const float Rad = 14.f * Cr.Size * R.FRandRange(0.8f, 1.2f), Ln = 70.f * Cr.Size * R.FRandRange(0.8f, 1.3f), Tip = Ln * R.FRandRange(0.28f, 0.4f);
			const FVector Foot = Cr.At - Up * 12.f * Cr.Size, Top = Foot + Up * Ln, Point = Top + Up * Tip;
			const float Bright = R.FRandRange(0.65f, 1.f);
			auto Face = [&](const FVector& A, const FVector& B, const FVector& Cc)
			{
				const FVector Nn = -FVector::CrossProduct(B - A, Cc - A).GetSafeNormal();
				const int32 I = V.Num();
				V.Append({ A, B, Cc }); N.Append({ Nn, Nn, Nn }); Col.Append({ FLinearColor(Bright, 0.f, 0.f, 1.f), FLinearColor(Bright, 0.f, 0.f, 1.f), FLinearColor(Bright, 0.f, 0.f, 1.f) });
				T.Append({ I, I + 1, I + 2 });
			};
			const float Tw = R.FRandRange(0.f, PI);
			for (int32 k = 0; k < 6; ++k)
			{
				const float A0 = Tw + 2.f * PI * k / 6.f, A1 = Tw + 2.f * PI * (k + 1) / 6.f;
				const FVector O0 = (Ax * FMath::Cos(A0) + Ay * FMath::Sin(A0)) * Rad, O1 = (Ax * FMath::Cos(A1) + Ay * FMath::Sin(A1)) * Rad;
				// (wound so the cross product points inward: Unreal's front face, as the ground's)
				Face(Foot + O0, Top + O0, Top + O1); Face(Foot + O0, Top + O1, Foot + O1);
				Face(Top + O0, Point, Top + O1);
			}
		}
		// (each face: its cross product must point into the crystal; fix any the other way, judged from the crystal's own axis)
		UProceduralMeshComponent* G = NewObject<UProceduralMeshComponent>(this);
		G->SetupAttachment(RootComponent);
		G->RegisterComponent();
		G->CreateMeshSection_LinearColor(0, V, T, N, TArray<FVector2D>(), Col, TArray<FProcMeshTangent>(), false);
		G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		G->SetCastShadow(false);
		G->SetLightingChannels(true, true, false);
		G->SetCachedMaxDrawDistance(12000.f);
		G->LDMaxDrawDistance = 12000.f;
		if (Base)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
			M->SetVectorParameterValue(TEXT("Tint"), C.Glow);
			M->SetVectorParameterValue(TEXT("Color"), C.Glow);
			M->SetScalarParameterValue(TEXT("Glow"), CVarCaveGlow.GetValueOnGameThread());
			G->SetMaterial(0, M);
			GemMats.Add(M);
		}
		Gems.Add(G);
		for (const FVector& At : C.Lights)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetWorldLocation(At);
			L->SetIntensityUnits(ELightUnits::Lumens);
			L->SetIntensity(CVarCaveLight.GetValueOnGameThread());
			L->SetAttenuationRadius(CVarCaveReach.GetValueOnGameThread());
			L->SetLightColor(FLinearColor::LerpUsingHSV(C.Glow, FLinearColor(1.f, 0.9f, 0.75f), 0.35f));
			L->SetCastShadows(false);   // (Tick: jeeb.Cave.Shadows; with jeeb.Cave.Channel the light cannot reach the hillside through the rock anyway)
		L->SetLightingChannels(false, true, false);
			L->SetSourceRadius(20.f);
			L->SetVisibility(false);
			L->RegisterComponent();
			Lamps.Add(L); LampCave.Add(c); LampBase.Add(1.f);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBCAVE crystals and lights: %d caves, %d lights"), All.Num(), Lamps.Num());
}

void ACaves::Tick(float Dt)
{
	Super::Tick(Dt);
	const APlayerController* Pc = GetWorld()->GetFirstPlayerController();   // (this machine's own view: looks only)
	if (!Pc || !Pc->PlayerCameraManager) return;
#if !UE_BUILD_SHIPPING
	// (jeeb.Test.GoCave N, single player: put me 8 m outside cave N's mouth (1 to 7), facing in; JEEBCAVE lists them)
	static IConsoleVariable* GoCave = IConsoleManager::Get().RegisterConsoleVariable(TEXT("jeeb.Test.GoCave"), 0, TEXT("Test builds, single player: N puts you 8 m outside cave N's mouth, facing in (1 Storm Peak, 2 West Range, 3 Middle Range, 4 East Mass, 5 Gull, 6 Seal, 7 Kraken; the log's JEEBCAVE lines)."));
	if (GoCave && GoCave->GetInt() > 0)
	{
		const int32 N = GoCave->GetInt() - 1;
		GoCave->Set(0, ECVF_SetByConsole);
		APawn* Me = Pc->GetPawn();
		if (Me && Me->HasAuthority() && JeebCaves::All().IsValidIndex(N))
		{
			const JeebCaves::FCave& C = JeebCaves::All()[N];
			const FVector2D P = C.Mouth - C.D * 800.f;
			Me->SetActorLocation(FVector(P.X, P.Y, FMath::Max(JeebTerrain::HeightAt(P.X, P.Y), JeebWorld::SeaLevel) + 120.f), false, nullptr, ETeleportType::TeleportPhysics);
			const FRotator Face(0.f, FMath::RadiansToDegrees(FMath::Atan2(C.D.Y, C.D.X)), 0.f);
			Me->SetActorRotation(Face);
			const_cast<APlayerController*>(Pc)->SetControlRotation(FRotator(-8.f, Face.Yaw, 0.f));
			UE_LOG(LogTemp, Log, TEXT("JEEBCAVE test: put outside %s"), *C.Name);
		}
	}
#endif
	const FVector Cam = Pc->PlayerCameraManager->GetCameraLocation();
	const float In = JeebCaves::InsideAt(Cam);
	CameraInside = FMath::FInterpConstantTo(CameraInside, In, Dt, 1.5f);
	const TArray<JeebCaves::FCave>& All = JeebCaves::All();
	const float Near = CVarCaveNear.GetValueOnGameThread();
	Pulse += Dt;
	TArray<bool> On; On.Init(false, All.Num());
	for (int32 c = 0; c < All.Num(); ++c)
	{
		const FVector2D C2(Cam.X, Cam.Y);
		// (2026-11-26: each shadowed light draws the scene six times, +2 to 3 ms near a mouth at 90 m: on only at the mouth or inside)
		On[c] = FVector2D::Distance(C2, All[c].Mouth) < Near || (In > 0.01f && All[c].Box.IsInside(C2));
	}
	const float Lm = CVarCaveLight.GetValueOnGameThread(), Reach = CVarCaveReach.GetValueOnGameThread();
	for (int32 i = 0; i < Lamps.Num(); ++i)
	{
		UPointLightComponent* L = Lamps[i];
		if (!L) continue;
		// (reach, channels and shadows change only while the light is off: changed on a lit light they left the renderer's copy stale
		// and crashed a test run, 2026-11-26. A change turns the light off for one frame.)
		const bool bCh = CVarCaveChannel.GetValueOnGameThread() != 0;
		const int32 Sh = CVarCaveShadows.GetValueOnGameThread();
		const bool bShadow = Sh >= 2 || (Sh == 1 && (i == 0 || LampCave[i - 1] != LampCave[i]));
		const bool bStale = FMath::Abs(L->AttenuationRadius - Reach) > 1.f || L->LightingChannels.bChannel1 != bCh || L->LightingChannels.bChannel0 == bCh || L->CastShadows != bShadow;
		if (bStale)
		{
			if (L->IsVisible()) { L->SetVisibility(false); continue; }
			L->SetAttenuationRadius(Reach);
			L->SetLightingChannels(!bCh, bCh, false);
			L->SetCastShadows(bShadow);
			continue;
		}
		if (L->IsVisible() != On[LampCave[i]]) L->SetVisibility(On[LampCave[i]]);
		if (!On[LampCave[i]]) continue;
		const float Want = Lm * (1.f + 0.06f * FMath::Sin(Pulse * 0.9f + i * 1.7f));   // (a slow breath, never a flicker)
		if (FMath::Abs(L->Intensity - Want) > Lm * 0.01f) L->SetIntensity(Want);
	}
	// (people take the cave lights too: every pawn's parts on channel 1 as well, looked over once a second; outside no light uses it)
	PawnCheck -= Dt;
	if (PawnCheck <= 0.f)
	{
		PawnCheck = 1.f;
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
			for (UPrimitiveComponent* Pc2 : Parts) if (Pc2 && !Pc2->LightingChannels.bChannel1 && Pc2->LightingChannels.bChannel0) Pc2->SetLightingChannels(true, true, Pc2->LightingChannels.bChannel2);
		}
	}
	const float Glow = CVarCaveGlow.GetValueOnGameThread();
	for (UMaterialInstanceDynamic* M : GemMats) if (M) { float Now = 0.f; M->GetScalarParameterValue(FName(TEXT("Glow")), Now); if (!FMath::IsNearlyEqual(Now, Glow)) M->SetScalarParameterValue(TEXT("Glow"), Glow); }
}
