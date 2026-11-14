#pragma once

#include "CoreMinimal.h"
#include "WorldMap.h"
#include "OakHill.h"

// The rebuilt Oak Hill (TownLab, 2026-11-14; ATownLabTown, TownLabTown.cpp). Since the owner approved it ("I think it looks great
// and I want to implement this", 2026-11-14) it IS the real Oak Hill in Field and the packaged game (RealTown(): the rebuilt town at
// the real plaza on the real land, its ground over the whole of JeebOakHill's box). The old town (AOakHillTown's ground patch, road
// ribbons and the 300k models alone) stays reachable with -OldOakHill (test builds only) until the owner confirms.
//
// TownLab, the test map (/Game/TownLab/Map/TownLab, a copy of Field, never cooked): the whole world as in Field, the OLD Oak Hill at
// its real spot for comparison, and the rebuilt one 300 m east of it on open land. The map's own parts are off unless the map's name
// contains "TownLab" (SetActive, from AKnightGameState::BuildWorld before anything is built).
//
// A site is where a rebuilt town stands: its plan is Oak Hill's own (JeebOakHill: plots, roads, hills in metres from the plaza)
// moved OffY east, so a point (X, Y) at a site is the point (X, Y - OffY) in Oak Hill's plan.
namespace JeebTownLab
{
	inline bool& ActiveFlag() { static bool b = false; return b; }
	inline bool Active() { return ActiveFlag(); }
	inline void SetActive(bool b) { ActiveFlag() = b; }

	// The real Oak Hill is the rebuilt town: in Field and the packaged game, not in the TownLab map (it shows the old town at the real
	// spot, for comparison). -OldOakHill brings the old town back (test builds only; give every machine of a test the same switch)
	inline bool OldSwitch()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("OldOakHill"));
		return bOld;
#endif
	}
	inline bool RealTown() { return JeebOakHill::Enabled() && !Active() && !OldSwitch(); }

	constexpr float OffY = 30000.f;   // 300 m east of the real Oak Hill (open rolling land; the river's east branch is 57 m south of its edge)
	constexpr float PlazaX = JeebOakHill::PosX, PlazaY = JeebOakHill::PosY + OffY;
	inline float EastM(float Y) { return (Y - PlazaY) / 100.f; }
	inline float NorthM(float X) { return (X - PlazaX) / 100.f; }
	inline FVector2D ToOld(float X, float Y) { return FVector2D(X, Y - OffY); }

	// Its own ground patch (a 1 m grid, ATownLabTown) on the world's 8 m grid lines, so the world mesh leaves an exact hole for it
	constexpr float PatchX0 = -15200.f, PatchX1 = 8800.f, PatchY0 = 41600.f, PatchY1 = 57600.f;
	inline bool InPatch(float X, float Y, float Pad = 0.f) { return X > PatchX0 - Pad && X < PatchX1 + Pad && Y > PatchY0 - Pad && Y < PatchY1 + Pad; }
	inline bool InPatchCell(float X0, float Y0, float Step) { return Active() && X0 >= PatchX0 && X0 + Step <= PatchX1 && Y0 >= PatchY0 && Y0 + Step <= PatchY1; }

	// The two sites. Real: the real plaza, its ground over JeebOakHill's whole box (the old patch east of the original square, plus
	// the square's own ground inside the box, which GrassField leaves out while RealTown()). Box edges are on the 8 m grid.
	struct FSite { float OffY, X0, X1, Y0, Y1; int32 Id; };
	inline const FSite& LabSite() { static const FSite S{ OffY, PatchX0, PatchX1, PatchY0, PatchY1, 0 }; return S; }
	inline const FSite& RealSite() { static const FSite S{ 0.f, JeebOakHill::BoxX0, JeebOakHill::BoxX1, JeebOakHill::BoxY0, JeebOakHill::BoxY1, 1 }; return S; }
	inline bool InSite(const FSite& S, float X, float Y, float Pad = 0.f) { return X > S.X0 - Pad && X < S.X1 + Pad && Y > S.Y0 - Pad && Y < S.Y1 + Pad; }
	inline bool InSiteCell(const FSite& S, float X0, float Y0, float Step) { return X0 >= S.X0 && X0 + Step <= S.X1 && Y0 >= S.Y0 && Y0 + Step <= S.Y1; }
	inline FVector2D ToPlan(const FSite& S, float X, float Y) { return FVector2D(X, Y - S.OffY); }

	// Off for frame-rate comparisons: -NoLabNew (the whole new town: land shape, ground, grass, trees, buildings)
	inline bool NewOn()
	{
		static const bool bOn = !FParse::Param(FCommandLine::Get(), TEXT("NoLabNew"));
		return Active() && bOn;
	}

	// The town's base height lifted to the land around it (the real town sits on its river's banks, this one on rolling land)
	inline float Lift()
	{
		static const float L = []()
		{
			float Sum = 0.f; int32 N = 0;
			for (int32 i = -2; i <= 2; ++i) for (int32 j = -2; j <= 2; ++j) { Sum += JeebWorld::LandHeight(PlazaX + i * 3000.f, PlazaY + j * 3000.f); ++N; }
			return FMath::Clamp(Sum / N - (JeebWorld::RiverLevel + 450.f), 0.f, 600.f);
		}();
		return L;
	}

	// The town's own land on top of the world's (H): Oak Hill's hills and level plots, fading into the world over 15 m round it
	// (as JeebOakHill::Apply does, with no river to fade into)
	inline float Apply(float X, float Y, float H)
	{
		if (!NewOn() || !InPatch(X, Y)) return H;
		const float E = EastM(Y), N = NorthM(X);
		const float M = JeebWorld::Smooth(-62.f, -56.f, E) * JeebWorld::Smooth(87.f, 72.f, E) * JeebWorld::Smooth(-115.f, -100.f, N) * JeebWorld::Smooth(120.f, 105.f, N);
		if (M <= 0.f) return H;
		const float Base = JeebWorld::RiverLevel + Lift();
		const float Hills = JeebOakHill::HillsM(E, N);
		const float Ground = FMath::Lerp(H, Base + Hills * 100.f, M);
		return FMath::Lerp(Ground, Base + JeebOakHill::TownM(E, N, (Ground - Base) / 100.f) * 100.f, M);
	}

	// Distance (cm) to the nearest road edge in the new town (Oak Hill's roads, read at the old town's spot); 1e6 outside it
	// (worked out once on a 1 m grid over the patch and read between its points: the roads are thousands of points, and the ground,
	// the grass and the trees ask hundreds of thousands of times)
	constexpr float GridStep = 100.f;
	struct FRoadGrid { TArray<float> G; int32 NI = 0, NJ = 0; };
	inline FRoadGrid MakeRoadGrid(const FSite& S)
	{
		FRoadGrid R;
		R.NI = FMath::RoundToInt((S.X1 - S.X0) / GridStep) + 1; R.NJ = FMath::RoundToInt((S.Y1 - S.Y0) / GridStep) + 1;
		R.G.SetNumUninitialized(R.NI * R.NJ);
		// (the road points once, in metres from the plan's plaza)
		TArray<FVector2D> Pts;
		for (const JeebOakHill::FRoad& Rd : JeebOakHill::Roads()) for (const FVector2D& Q : Rd.Pts) Pts.Add(FVector2D(JeebOakHill::EastM(Q.Y), JeebOakHill::NorthM(Q.X)));
		for (int32 i = 0; i < R.NI; ++i)
			for (int32 j = 0; j < R.NJ; ++j)
			{
				const FVector2D P(JeebOakHill::EastM(S.Y0 + j * GridStep - S.OffY), JeebOakHill::NorthM(S.X0 + i * GridStep));
				float Best = 1.0e12f;
				for (const FVector2D& Q : Pts) Best = FMath::Min(Best, float(FVector2D::DistSquared(P, Q)));
				R.G[i * R.NJ + j] = Pts.Num() ? FMath::Sqrt(Best) * 100.f - JeebOakHill::RoadWidth * 0.5f : 1.0e6f;
			}
		return R;
	}
	inline const FRoadGrid& RoadGrid(const FSite& S)
	{
		static const FRoadGrid Lab = MakeRoadGrid(LabSite());
		if (S.Id == 0) return Lab;
		static const FRoadGrid Real = MakeRoadGrid(RealSite());
		return Real;
	}
	inline float RoadClearance(const FSite& S, float X, float Y)
	{
		if (!InSite(S, X, Y)) return 1.0e6f;
		const FRoadGrid& R = RoadGrid(S);
		const float U = FMath::Clamp((X - S.X0) / GridStep, 0.f, R.NI - 1.001f), V = FMath::Clamp((Y - S.Y0) / GridStep, 0.f, R.NJ - 1.001f);
		const int32 I = FMath::FloorToInt(U), J = FMath::FloorToInt(V), NJ = R.NJ;
		const float Fu = U - I, Fv = V - J;
		const TArray<float>& G = R.G;
		return FMath::Lerp(FMath::Lerp(G[I * NJ + J], G[I * NJ + J + 1], Fv), FMath::Lerp(G[(I + 1) * NJ + J], G[(I + 1) * NJ + J + 1], Fv), Fu);
	}
	inline float RoadClearance(float X, float Y) { return Active() ? RoadClearance(LabSite(), X, Y) : 1.0e6f; }
	// On a building in the new town (no grass, no trees), and how worn the ground is round its walls and doors
	inline bool OnBuilding(float X, float Y, float MarginM = 0.f) { const FVector2D O = ToOld(X, Y); return JeebOakHill::OnBuilding(O.X, O.Y, MarginM); }
	inline float BuildingDistance(float X, float Y) { const FVector2D O = ToOld(X, Y); return JeebOakHill::BuildingDistance(O.X, O.Y); }
}
