#pragma once

#include "CoreMinimal.h"
#include "WorldMap.h"
#include "OakHill.h"

// TownLab (2026-11-14, temporary, never shipped): a test map (/Game/TownLab/TownLab, a copy of Field made by Tools/make_townlab.py)
// in which the whole world is built as in Field, so the real Oak Hill stands there exactly as it is today, and a second Oak Hill,
// the same layout and roads rebuilt the way professional games do it (ATownLabTown, TownLabTown.cpp), stands 300 m east of it on
// open land. Everything here is off unless the map's name contains "TownLab" (SetActive, from AKnightGameState::BuildWorld before
// anything is built), so Field and the packaged game never change.
//
// The new town's plan is Oak Hill's own (JeebOakHill: plots, roads, hills in metres from the plaza), read at the new plaza:
// a point (X, Y) in the new town is the point (X, Y - OffY) in the old one.
namespace JeebTownLab
{
	inline bool& ActiveFlag() { static bool b = false; return b; }
	inline bool Active() { return ActiveFlag(); }
	inline void SetActive(bool b) { ActiveFlag() = b; }

	constexpr float OffY = 30000.f;   // 300 m east of the real Oak Hill (open rolling land; the river's east branch is 57 m south of its edge)
	constexpr float PlazaX = JeebOakHill::PosX, PlazaY = JeebOakHill::PosY + OffY;
	inline float EastM(float Y) { return (Y - PlazaY) / 100.f; }
	inline float NorthM(float X) { return (X - PlazaX) / 100.f; }
	inline FVector2D ToOld(float X, float Y) { return FVector2D(X, Y - OffY); }

	// Its own ground patch (a 1 m grid, ATownLabTown) on the world's 8 m grid lines, so the world mesh leaves an exact hole for it
	constexpr float PatchX0 = -15200.f, PatchX1 = 8800.f, PatchY0 = 41600.f, PatchY1 = 57600.f;
	inline bool InPatch(float X, float Y, float Pad = 0.f) { return X > PatchX0 - Pad && X < PatchX1 + Pad && Y > PatchY0 - Pad && Y < PatchY1 + Pad; }
	inline bool InPatchCell(float X0, float Y0, float Step) { return Active() && X0 >= PatchX0 && X0 + Step <= PatchX1 && Y0 >= PatchY0 && Y0 + Step <= PatchY1; }

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
	inline const TArray<float>& RoadGrid(int32& OutNI, int32& OutNJ)
	{
		static int32 NI = 0, NJ = 0;
		static const TArray<float> G = []()
		{
			NI = FMath::RoundToInt((PatchX1 - PatchX0) / GridStep) + 1; NJ = FMath::RoundToInt((PatchY1 - PatchY0) / GridStep) + 1;
			TArray<float> A; A.SetNumUninitialized(NI * NJ);
			// (the road points once, in metres from the plaza)
			TArray<FVector2D> Pts;
			for (const JeebOakHill::FRoad& R : JeebOakHill::Roads()) for (const FVector2D& Q : R.Pts) Pts.Add(FVector2D(JeebOakHill::EastM(Q.Y), JeebOakHill::NorthM(Q.X)));
			for (int32 i = 0; i < NI; ++i)
				for (int32 j = 0; j < NJ; ++j)
				{
					const FVector2D P(EastM(PatchY0 + j * GridStep), NorthM(PatchX0 + i * GridStep));
					float Best = 1.0e12f;
					for (const FVector2D& Q : Pts) Best = FMath::Min(Best, float(FVector2D::DistSquared(P, Q)));
					A[i * NJ + j] = Pts.Num() ? FMath::Sqrt(Best) * 100.f - JeebOakHill::RoadWidth * 0.5f : 1.0e6f;
				}
			return A;
		}();
		OutNI = NI; OutNJ = NJ;
		return G;
	}
	inline float RoadClearance(float X, float Y)
	{
		if (!InPatch(X, Y)) return 1.0e6f;
		int32 NI, NJ;
		const TArray<float>& G = RoadGrid(NI, NJ);
		const float U = FMath::Clamp((X - PatchX0) / GridStep, 0.f, NI - 1.001f), V = FMath::Clamp((Y - PatchY0) / GridStep, 0.f, NJ - 1.001f);
		const int32 I = FMath::FloorToInt(U), J = FMath::FloorToInt(V);
		const float Fu = U - I, Fv = V - J;
		return FMath::Lerp(FMath::Lerp(G[I * NJ + J], G[I * NJ + J + 1], Fv), FMath::Lerp(G[(I + 1) * NJ + J], G[(I + 1) * NJ + J + 1], Fv), Fu);
	}
	// On a building in the new town (no grass, no trees), and how worn the ground is round its walls and doors
	inline bool OnBuilding(float X, float Y, float MarginM = 0.f) { const FVector2D O = ToOld(X, Y); return JeebOakHill::OnBuilding(O.X, O.Y, MarginM); }
	inline float BuildingDistance(float X, float Y) { const FVector2D O = ToOld(X, Y); return JeebOakHill::BuildingDistance(O.X, O.Y); }
}
