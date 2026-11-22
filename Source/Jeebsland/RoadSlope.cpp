// The road slope check (2026-11-20, owner: "I don't want any roads in the game to be so steep that the player can't walk on them").
// -RoadSlope (any run): walks every road in the world data (JeebTerrain::PathDefs: the old village site's, Oak Hill's streets, the forest roads,
// Steele Creek's, the bridge's approach), every 50 cm down its middle and 60% of the way to each edge, and measures the ground's slope
// there (the steepest way, from the drawn ground's heights 50 cm either side), skipping the bridge's own deck. Logs JEEBROADS lines:
// per kind, the samples steeper than 30 degrees and steeper than the knight's walkable floor (Unreal's default, 44.77 degrees), and
// the twelve worst spots (one per 5 m). Read-only; changes nothing in the world.
#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include "Terrain.h"
#include "Bridge.h"
#include "OakHill.h"

namespace JeebRoadSlope
{
	void Report()
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("RoadSlope"))) return;
		const double T0 = FPlatformTime::Seconds();
		constexpr float Walkable = 44.77f, Steep = 30.f, Step = 50.f, D = 50.f;
		const FVector2D BC = JeebBridge::Centre();
		struct FSpot { float Deg; FVector2D P; int32 Kind; int32 Road; };
		TArray<FSpot> Worst;
		int32 Samples[5] = { 0 }, OverSteep[5] = { 0 }, OverWalk[5] = { 0 };
		float MaxDeg[5] = { 0 };
		const TArray<JeebTerrain::FPathDef>& Defs = JeebTerrain::PathDefs();
		for (int32 r = 0; r < Defs.Num(); ++r)
		{
			const JeebTerrain::FPathDef& Pd = Defs[r];
			const int32 K = FMath::Clamp<int32>(Pd.Kind, 0, 4);
			const int32 N = Pd.Pts.Num();
			for (int32 i = 0; i + 1 < N + (Pd.bClosed ? 1 : 0); ++i)
			{
				const FVector2D A = Pd.Pts[i], B = Pd.Pts[(i + 1) % N];
				const float Len = FVector2D::Distance(A, B);
				if (Len < 1.f) continue;
				const FVector2D Dir = (B - A) / Len, Side(-Dir.Y, Dir.X);
				const float HL = Pd.HalfL.IsValidIndex(i) ? Pd.HalfL[i] : Pd.Width * 0.5f, HR = Pd.HalfR.IsValidIndex(i) ? Pd.HalfR[i] : Pd.Width * 0.5f;
				for (float t = 0.f; t < Len; t += Step)
					for (int32 l = -1; l <= 1; ++l)
					{
						const FVector2D P = A + Dir * t + Side * (l < 0 ? -0.6f * HL : l > 0 ? 0.6f * HR : 0.f);
						if (FMath::Abs(P.X - BC.X) < 400.f && FMath::Abs(P.Y - BC.Y) < JeebBridge::DeckEndL()) continue;   // (the deck is the road)
						const float Gx = (JeebTerrain::HeightAt(P.X + D, P.Y) - JeebTerrain::HeightAt(P.X - D, P.Y)) / (2.f * D);
						const float Gy = (JeebTerrain::HeightAt(P.X, P.Y + D) - JeebTerrain::HeightAt(P.X, P.Y - D)) / (2.f * D);
						const float Deg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(Gx * Gx + Gy * Gy)));
						++Samples[K];
						MaxDeg[K] = FMath::Max(MaxDeg[K], Deg);
						if (Deg > Steep) ++OverSteep[K];
						if (Deg > Walkable) ++OverWalk[K];
						if (Deg > Steep)
						{
							bool bNear = false;
							for (FSpot& S : Worst) if (FVector2D::Distance(S.P, P) < 500.f) { bNear = true; if (Deg > S.Deg) { S.Deg = Deg; S.P = P; } break; }
							if (!bNear) Worst.Add({ Deg, P, K, r });
						}
					}
			}
		}
		static const TCHAR* Names[5] = { TEXT("the old village site and the square"), TEXT("Oak Hill streets"), TEXT("forest and Steele roads"), TEXT("bridge approach"), TEXT("bridge span ends") };
		int32 AllWalk = 0, AllSteep = 0, AllN = 0;
		for (int32 k = 0; k < 5; ++k)
		{
			AllWalk += OverWalk[k]; AllSteep += OverSteep[k]; AllN += Samples[k];
			UE_LOG(LogTemp, Display, TEXT("JEEBROADS %s: %d samples, steepest %.1f degrees, %d over 30, %d over the walkable %.2f"), Names[k], Samples[k], MaxDeg[k], OverSteep[k], OverWalk[k], Walkable);
		}
		Worst.Sort([](const FSpot& A, const FSpot& B) { return A.Deg > B.Deg; });
		for (int32 i = 0; i < FMath::Min(12, Worst.Num()); ++i)
		{
			const FSpot& S = Worst[i];
			UE_LOG(LogTemp, Display, TEXT("JEEBROADS worst %d: %.1f degrees at %.0f %.0f (ground %.0f; %s, road %d; Oak Hill plaza E %.1f N %.1f m)"), i + 1, S.Deg, S.P.X, S.P.Y,
				JeebTerrain::HeightAt(S.P.X, S.P.Y), Names[S.Kind], S.Road, JeebOakHill::EastM(S.P.Y), JeebOakHill::NorthM(S.P.X));
			if (i < 5 && FParse::Param(FCommandLine::Get(), TEXT("RoadSlopeGrid")))
				for (int32 a = -4; a <= 4; ++a)
				{
					FString Row;
					for (int32 b = -4; b <= 4; ++b) Row += FString::Printf(TEXT(" %5.0f"), JeebTerrain::HeightAt(S.P.X + a * 100.f, S.P.Y + b * 100.f));
					UE_LOG(LogTemp, Display, TEXT("JEEBROADS grid %d X%+d m (Y -4..+4 m):%s"), i + 1, a, *Row);
				}
		}
		UE_LOG(LogTemp, Display, TEXT("JEEBROADS total: %d samples, %d over 30 degrees, %d over the walkable floor (%s), %.0f ms"), AllN, AllSteep, AllWalk, AllWalk == 0 ? TEXT("PASS") : TEXT("FAIL"), (FPlatformTime::Seconds() - T0) * 1000.0);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Oak Hill's roads eased to a walk (2026-11-20, owner: a flagstone road falling steeply off the bridge's landing; "I don't want any
// roads in the game to be so steep that the player can't walk on them"). The town's ground meets the rivers' bank cap, the forts'
// unlevelled ground and the houses' level pads in short fades, and where a street crossed one it dropped up to 1.7 m in a metre or two
// (48 degrees by the north fort, 38 at the bridge's east end). Here, once per machine, a 1 m grid of the finished ground over the town's
// box is relaxed on and round its roads (every cell within RoadReach of a street, but never a building's own plot and the flat 1.5 m
// round it) until no two neighbours there differ by more than MaxDeg; HeightAt adds the difference back. The houses, the river and the
// ground away from the roads keep their shape. Pure arithmetic in a fixed order: the same on the server and every client.
// ---------------------------------------------------------------------------------------------------------------------------------
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include <atomic>

namespace
{
	constexpr float EaseStep = 100.f, RoadReach = 250.f, MaxDeg = 27.f;
	std::atomic<bool> bEaseReady{ false };
	bool bEaseBuilding = false;
	FCriticalSection EaseLock;
	TArray<float> EaseDelta;
	int32 EaseNX = 0, EaseNY = 0;
	float EaseX0 = 0.f, EaseY0 = 0.f;

	void BuildEase()
	{
		const double T0 = FPlatformTime::Seconds();
		EaseX0 = JeebOakHill::BoxX0; EaseY0 = JeebOakHill::BoxY0;
		EaseNX = FMath::RoundToInt((JeebOakHill::BoxX1 - JeebOakHill::BoxX0) / EaseStep) + 1;
		EaseNY = FMath::RoundToInt((JeebOakHill::BoxY1 - JeebOakHill::BoxY0) / EaseStep) + 1;
		const int32 N = EaseNX * EaseNY;
		TArray<float> H; H.SetNumUninitialized(N);
		TArray<uint8> Free; Free.SetNumZeroed(N);
		int32 FreeN = 0;
		// (near a road: within RoadReach of a street's edge, marked from the streets' own points, not PathDefs, which may itself be
		// building when the ground is first asked for)
		TArray<uint8> Near; Near.SetNumZeroed(N);
		{
			auto Mark = [&](const TArray<FVector2D>& Pts, float Half)
			{
				const float R = Half + RoadReach; const int32 Rc = FMath::CeilToInt(R / EaseStep);
				for (int32 p = 0; p < Pts.Num(); ++p)
				{
					const FVector2D A = Pts[p], B = Pts[FMath::Min(p + 1, Pts.Num() - 1)];
					const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / 50.f));
					for (int32 s = 0; s <= Steps; ++s)
					{
						const FVector2D Q = FMath::Lerp(A, B, float(s) / Steps);
						const int32 Ci = FMath::RoundToInt((Q.X - EaseX0) / EaseStep), Cj = FMath::RoundToInt((Q.Y - EaseY0) / EaseStep);
						for (int32 a = -Rc; a <= Rc; ++a) for (int32 b = -Rc; b <= Rc; ++b)
						{
							const int32 i = Ci + a, j = Cj + b;
							if (i < 0 || j < 0 || i >= EaseNX || j >= EaseNY) continue;
							if (FVector2D::Distance(FVector2D(EaseX0 + i * EaseStep, EaseY0 + j * EaseStep), Q) <= R) Near[i * EaseNY + j] = 1;
						}
					}
				}
			};
			for (const JeebOakHill::FRoad& Rd : JeebOakHill::Roads()) Mark(Rd.Pts, JeebOakHill::RoadWidth * 0.5f);
			for (const JeebBridge::FRoad& Br : JeebBridge::Roads()) Mark(Br.Pts, Br.Width * 0.5f);
		}
		for (int32 i = 0; i < EaseNX; ++i)
			for (int32 j = 0; j < EaseNY; ++j)
			{
				const float X = EaseX0 + i * EaseStep, Y = EaseY0 + j * EaseStep;
				const int32 k = i * EaseNY + j;
				H[k] = JeebTerrain::HeightAt(X, Y);   // (bEaseBuilding: without the easing)
				// (the box's own edge stays put, so the eased ground always meets the world's; buildings keep their ground)
				if (i == 0 || j == 0 || i == EaseNX - 1 || j == EaseNY - 1) continue;
				if (!Near[k]) continue;
				{
					// (a building's own plot and 60 cm round it keep their ground; the rest of its flat apron may ease into the road)
					const float E = JeebOakHill::EastM(Y), Nm = JeebOakHill::NorthM(X);
					bool bPlot = false;
					for (const JeebOakHill::FPlot& P : JeebOakHill::Plots())
						if (P.Kind == JeebOakHill::EPlot::Building && FMath::Square(E - P.Ce) + FMath::Square(Nm - P.Cn) < FMath::Square(P.Reach + 2.f) && JeebOakHill::PlotSD(P, E, Nm) < 0.6f) { bPlot = true; break; }
					if (bPlot) continue;
				}
				if (JeebWorld::InWater(X, Y)) continue;
				Free[k] = 1; ++FreeN;
			}
		const TArray<float> H0 = H;
		const float S = FMath::Tan(FMath::DegreesToRadians(MaxDeg)) * EaseStep;
		const int32 Di[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, Dj[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		int32 Pass = 0; float Worst = 0.f;
		for (; Pass < 400; ++Pass)
		{
			Worst = 0.f;
			const bool bBack = (Pass & 1) != 0;
			for (int32 ii = 0; ii < EaseNX; ++ii)
			{
				const int32 i = bBack ? EaseNX - 1 - ii : ii;
				for (int32 jj = 0; jj < EaseNY; ++jj)
				{
					const int32 j = bBack ? EaseNY - 1 - jj : jj;
					const int32 k = i * EaseNY + j;
					if (!Free[k]) continue;
					for (int32 d = 0; d < 8; ++d)
					{
						const int32 k2 = (i + Di[d]) * EaseNY + (j + Dj[d]);
						const float Lim = d < 4 ? S : S * 1.41421356f;
						const float Diff = H[k] - H[k2], Ex = FMath::Abs(Diff) - Lim;
						if (Ex <= 0.f) continue;
						Worst = FMath::Max(Worst, Ex);
						const float Sg = Diff > 0.f ? 1.f : -1.f;
						if (Free[k2]) { H[k] -= Sg * Ex * 0.5f; H[k2] += Sg * Ex * 0.5f; }
						else H[k] -= Sg * Ex;
					}
				}
			}
			if (Worst < 0.5f) break;
		}
		EaseDelta.SetNumUninitialized(N);
		float Most = 0.f;
		for (int32 k = 0; k < N; ++k) { EaseDelta[k] = H[k] - H0[k]; Most = FMath::Max(Most, FMath::Abs(EaseDelta[k])); }
		uint32 Hash = 2166136261u;
		for (int32 k = 0; k < N; k += 7) { Hash ^= uint32(FMath::RoundToInt(EaseDelta[k] * 10.f)); Hash *= 16777619u; }
		UE_LOG(LogTemp, Log, TEXT("JEEBROADS eased Oak Hill's roads to %.0f degrees: %d of %d cells free, %d passes (left over %.1f cm), moved up to %.0f cm, %.0f ms, hash=%08x"),
			MaxDeg, FreeN, N, Pass, Worst, Most, (FPlatformTime::Seconds() - T0) * 1000.0, Hash);
	}
}

float JeebTerrain::RoadEaseDelta(float X, float Y)
{
	if (!bEaseReady.load(std::memory_order_acquire))
	{
		FScopeLock Lock(&EaseLock);   // (re-entrant on the building thread: its own HeightAt calls see bEaseBuilding and get the ground as it was)
		if (bEaseBuilding) return 0.f;
		if (!bEaseReady.load())
		{
			bEaseBuilding = true;
			BuildEase();
			bEaseBuilding = false;
			bEaseReady.store(true, std::memory_order_release);
		}
	}
	const float Gx = (X - EaseX0) / EaseStep, Gy = (Y - EaseY0) / EaseStep;
	if (Gx < 0.f || Gy < 0.f || Gx >= EaseNX - 1 || Gy >= EaseNY - 1) return 0.f;
	const int32 I = FMath::FloorToInt(Gx), J = FMath::FloorToInt(Gy);
	const float Fx = Gx - I, Fy = Gy - J;
	const float A = EaseDelta[I * EaseNY + J], B = EaseDelta[(I + 1) * EaseNY + J], C = EaseDelta[I * EaseNY + J + 1], D = EaseDelta[(I + 1) * EaseNY + J + 1];
	return FMath::Lerp(FMath::Lerp(A, C, Fy), FMath::Lerp(B, D, Fy), Fx);
}
