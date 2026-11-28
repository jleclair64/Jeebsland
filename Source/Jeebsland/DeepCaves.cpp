#include "DeepCaves.h"
#include "Terrain.h"
#include "WorldMap.h"
#include "OpenWorld.h"
#include "SilverPoint.h"
#include "HouseLoot.h"
#include "WaterLook.h"
#include "WorldTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Async/ParallelFor.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Docs/CavePlan.md. Nothing here reads anything but the finished height and fixed numbers: the same Deeps on every machine.

namespace
{
	using namespace JeebDeep;

	TAutoConsoleVariable<float> CVarDeepTorch(TEXT("jeeb.Deep.Torch"), 420.f, TEXT("The Deeps: each wall torch's light (lumens)."));
	TAutoConsoleVariable<float> CVarDeepTorchReach(TEXT("jeeb.Deep.TorchReach"), 950.f, TEXT("The Deeps: each wall torch's reach (cm)."));
	TAutoConsoleVariable<int32> CVarDeepLights(TEXT("jeeb.Deep.Lights"), 24, TEXT("The Deeps: how many of the nearest torches and glows light the rock at once (a pool of shadowless lights, lighting channel 1 only)."));
	TAutoConsoleVariable<float> CVarDeepNear(TEXT("jeeb.Deep.LightsNear"), 3600.f, TEXT("The Deeps: torches and glows further than this from the camera (cm) give no light (their flames still burn)."));

	constexpr float VS = 75.f;           // the rock's grid (cm)
	constexpr int32 CN = 16;             // cells along a chunk
	constexpr float Soil = 150.f;        // the field's ground lies this far under the real ground, out of sight
	constexpr float Rh = 1100.f;         // a spiral ramp's radius (to the floor's middle)
	constexpr float Grade = 0.19f;       // a spiral's climb
	constexpr float Spacing = 3000.f;    // the levels' grids
	constexpr float NeedCover = 600.f;   // rock (cm) over a roof, away from the mouths

	inline float Sm(float A, float B, float X) { return JeebWorld::Smooth01(A, B, X); }
	inline float H(float X, float Y) { return JeebTerrain::HeightAt(X, Y); }
	inline float H(const FVector2D& P) { return JeebTerrain::HeightAt(P.X, P.Y); }
	inline FVector2D XY(const FVector& V) { return FVector2D(V.X, V.Y); }
	inline FVector V3(const FVector2D& P, float Z) { return FVector(P.X, P.Y, Z); }
	inline FVector2D Dir(float A) { return FVector2D(FMath::Cos(A), FMath::Sin(A)); }
	inline float Hash01(uint32 A, uint32 B = 0) { uint32 X = A * 747796405u + B * 2891336453u + 12345u; X ^= X >> 16; X *= 0x7feb352du; X ^= X >> 15; X *= 0x846ca68bu; X ^= X >> 16; return float(X & 0xFFFFFF) / float(0x1000000); }

	struct FSeg { FVector A, B; float W, Ht; };
	struct FSolid { uint8 T = 0; FVector C = FVector::ZeroVector, R = FVector(100.f); FBox Box = FBox(ForceInit); };   // 0 a lump (C its middle, R its radii), 1 a cone up from C (R.X wide, R.Z tall), 2 a cone down from C, 3 a pillar (R.X)
	struct FHelix { FVector2D C; float Z0, Z1, A0, Sweep; int32 Lo = -1, Hi = -1; };

	bool GPrepared = false, GOk = false;
	TArray<FMouth> GMouths;
	TArray<FRoom> GRooms;
	TArray<FTunnel> GTun;
	TArray<FNode> GNodes;
	TArray<FSpot> GSpots;
	TArray<FSeg> GSegs;
	TArray<FSolid> GSolids;
	TArray<FHelix> GHelix;
	TMap<FIntVector, TArray<int32>> GGrid;   // 8 m cells: the passages' pieces near them
	constexpr float GridC = 800.f;
	FBox GAll(ForceInit);
	uint32 GHash = 0;
	float GLevels[5] = { 0.f, 0.f, 0.f, 0.f, 0.f };
	// (things, worked out with the rest so every machine agrees: the chests' spots above all)
	TArray<FTransform> GTorches;
	TArray<FLightSrc> GLights;
	TArray<FProp> GProps;
	struct FCrystal { FVector At, Up; float Size; FLinearColor Col; };
	TArray<FCrystal> GCrystals;
	struct FChest { FTransform Xf; FVector RoomMid; int32 Room; };
	TArray<FChest> GChests;

	FIntVector CellOf(const FVector& P) { return FIntVector(FMath::FloorToInt(P.X / GridC), FMath::FloorToInt(P.Y / GridC), FMath::FloorToInt(P.Z / GridC)); }

	float SegD(const FSeg& S, const FVector& P, float& Dz)
	{
		const FVector2D A = XY(S.A), AB = XY(S.B) - A;
		const float L2 = AB.SizeSquared();
		const float Tu = L2 > 1.f ? float(FVector2D::DotProduct(XY(P) - A, AB)) / L2 : 0.f;
		const float T = FMath::Clamp(Tu, 0.f, 1.f);
		const FVector Q = S.A + (S.B - S.A) * T;
		const float Dl = FVector2D::Distance(XY(P), XY(Q));
		// (2026-11-28: past either end the floor keeps the segment's slope; held flat there, each end's round cap made a flat
		// landing and a ramp became stairs the knight could not climb, found by the walk test)
		Dz = P.Z - FMath::Max(Q.Z, S.A.Z + (S.B.Z - S.A.Z) * FMath::Clamp(Tu, -1.f, 2.f));   // (only ever raised: lowered, the cap dug a pit past a mouth passage's outer end)
		const float Hm = 0.45f * S.Ht;
		float E;
		if (Dz <= Hm) E = Dl - S.W;
		else { const float U = Dl / S.W, V = (Dz - Hm) / (S.Ht - Hm); E = (FMath::Sqrt(U * U + V * V) - 1.f) * FMath::Min(S.W, S.Ht - Hm); }
		return FMath::Max(E, -Dz);
	}
	float RoomD(const FRoom& R, const FVector& P, float& Dz)
	{
		const FVector Q = P - R.C;
		const float Cy = FMath::Cos(R.Yaw), Sy = FMath::Sin(R.Yaw);
		const float X = Q.X * Cy + Q.Y * Sy, Y = -Q.X * Sy + Q.Y * Cy;
		const float N = FMath::Sqrt(FMath::Square(X / R.Rx) + FMath::Square(Y / R.Ry));
		float Fz = 0.f;
		if (R.Basin > 0.f) Fz = -R.Basin * (1.f - Sm(0.5f, 1.f, FMath::Sqrt(FMath::Square(X / (0.62f * R.Rx)) + FMath::Square(Y / (0.55f * R.Ry)))));
		Dz = Q.Z - Fz;
		const float M = FMath::Min(R.Rx, R.Ry), Hm = 0.3f * R.Ht;
		float E;
		if (Q.Z <= Hm) E = (N - 1.f) * M;
		else { const float V = (Q.Z - Hm) / (R.Ht - Hm); E = (FMath::Sqrt(N * N + V * V) - 1.f) * FMath::Min(M, R.Ht - Hm); }
		return FMath::Max(E, -Dz);
	}
	float SolidD(const FSolid& S, const FVector& P)
	{
		const FVector Q = P - S.C;
		switch (S.T)
		{
		case 0: { const FVector N = Q / S.R; return (N.Size() - 1.f) * S.R.GetMin(); }
		case 1: case 2:
		{
			const float Zr = S.T == 1 ? Q.Z : -Q.Z;
			const float Rz = S.R.X * (1.f - FMath::Clamp(Zr / S.R.Z, 0.f, 1.f));
			return FMath::Max3(float(FVector2D(Q.X, Q.Y).Size()) - Rz, -Zr - 30.f, Zr - float(S.R.Z)) * 0.8f;
		}
		default: return FVector2D(Q.X, Q.Y).Size() - S.R.X;
		}
	}
	float WallNoise(const FVector& P)
	{
		const FVector Q(P.X * 0.799f - P.Y * 0.602f, P.X * 0.602f + P.Y * 0.799f, P.Z * 1.13f);
		return 44.f * FMath::PerlinNoise3D(P / 410.f + FVector(3.1f, -7.7f, 1.9f)) + 24.f * FMath::PerlinNoise3D(Q / 170.f + FVector(-2.3f, 5.5f, 8.8f))
			+ 10.f * FMath::PerlinNoise3D(P / 67.f + FVector(9.4f, 1.2f, -3.6f)) + 30.f * FMath::PerlinNoise3D(FVector(P.X / 1300.f, P.Y / 1300.f, P.Z / 350.f) + FVector(-6.1f, 2.4f, 4.4f));
	}

	// the air of the passages and rooms (< 0), without the noise; Dz: height over the nearest piece's floor; Rough: the noise's share
	float AirD(const FVector& P, float& Dz, float& Rough)
	{
		float Best = 1000.f; Dz = 1000.f; Rough = 1.f;
		if (const TArray<int32>* L = GGrid.Find(CellOf(P)))
			for (const int32 i : *L) { float Dzi; const float D = SegD(GSegs[i], P, Dzi); if (D < Best) { Best = D; Dz = Dzi; Rough = 1.f; } }
		for (const FRoom& R : GRooms)
		{
			if (!R.Box.IsInside(P)) continue;
			float Dzi; const float D = RoomD(R, P, Dzi);
			if (D < Best) { Best = D; Dz = Dzi; Rough = R.Kind == ERoom::Library ? 0.7f : 1.6f; }
			else if (D < Best + 150.f) Rough = FMath::Max(Rough, FMath::Lerp(1.6f, 1.f, (D - Best) / 150.f));
		}
		for (const FSolid& S : GSolids) if (S.Box.IsInside(P)) Best = FMath::Max(Best, -SolidD(S, P));
		return Best;
	}
	float CaveField(const FVector& P)
	{
		float Dz, Rough;
		const float D = AirD(P, Dz, Rough);
		if (D > 320.f || D < -320.f) return D;
		const float W = Sm(25.f, 150.f, Dz);
		return D + W * Rough * WallNoise(P) + (1.f - W) * 5.f * FMath::PerlinNoise3D(P / 420.f + FVector(1.7f, 4.4f, -2.2f));
	}
	// > 0 inside a mouth's rock frame (-1e9 outside every frame's box)
	float HoodIn(const FVector& P)
	{
		float Best = -1.0e9f;
		for (const FMouth& M : GMouths)
		{
			if (!M.Box.IsInside(P)) continue;
			const FVector2D Q = XY(P) - XY(M.At);
			const float A = FVector2D::DotProduct(Q, M.D), L = FVector2D::DotProduct(Q, M.Lat()), Z = P.Z - M.At.Z;
			const float A0 = -M.Lip, A1 = M.HoodLen, Z0 = -M.Below, Z1 = 520.f + 170.f;
			const float Hw = 260.f + 190.f - 0.35f * FMath::Max(0.f, Z - 300.f);
			constexpr float Rr = 150.f;
			const FVector Qd(FMath::Abs(A - 0.5f * (A0 + A1)) - (0.5f * (A1 - A0) - Rr), FMath::Abs(L) - (Hw - Rr), FMath::Abs(Z - 0.5f * (Z0 + Z1)) - (0.5f * (Z1 - Z0) - Rr));
			const float Sd = FVector(FMath::Max(Qd.X, 0.f), FMath::Max(Qd.Y, 0.f), FMath::Max(Qd.Z, 0.f)).Size() + FMath::Min(FMath::Max3(Qd.X, Qd.Y, Qd.Z), 0.f) - Rr
				+ 38.f * FMath::PerlinNoise3D(P / 190.f + FVector(4.2f, -1.1f, 2.6f)) + 16.f * FMath::PerlinNoise3D(P / 73.f + FVector(-5.5f, 3.3f, 0.7f));
			Best = FMath::Max(Best, -Sd);
		}
		return Best;
	}
	// the whole rock as meshed: the caves cut out of (the ground 1.5 m under the real ground, or a frame)
	inline float Full(const FVector& P, float GroundZ)
	{
		return FMath::Min(CaveField(P), FMath::Max(GroundZ - Soil - P.Z, HoodIn(P)));
	}

	// ------------------------------------------------------------------------------------------------ placing it
	struct FCtx
	{
		FVector2D Mid; float PhiE = 0.f, PhiC = 0.f;
		const TArray<FVector2D>* Road = nullptr;
	};

	float RoadDist(const FVector2D& P)
	{
		float Best = 1.0e18f;
		for (const TArray<FVector2D>& L : JeebOpen::RoadLines())
			for (int32 i = 0; i < L.Num(); i += 4) Best = FMath::Min(Best, float(FVector2D::DistSquared(P, L[i])));
		return FMath::Sqrt(Best);
	}
	// rock over a roof at (P, floor Z, height Ht), at the middle and both sides
	float CoverAt(const FVector2D& P, const FVector2D& Lat, float Z, float Ht, float W)
	{
		float Least = 1.0e9f;
		for (const float X : { -1.f, 0.f, 1.f }) Least = FMath::Min(Least, H(P + Lat * (X * (W + 60.f))) - (Z + Ht));
		return Least;
	}
	bool NodeOk(const FVector2D& P, float Z, float Ht, float R)
	{
		if (H(P) - (Z + Ht) < NeedCover) return false;
		for (int32 k = 0; k < 8; ++k) if (H(P + Dir(k * PI / 4.f) * R) - (Z + Ht) < NeedCover) return false;
		return true;
	}
	// how far a frame must run into the hill before the ground covers the passage (cm), or a large number
	float HoodLenAt(const FVector2D& M, const FVector2D& D, float Floor)
	{
		const FVector2D L(-D.Y, D.X);
		for (float A = 250.f; A <= 2200.f; A += 50.f)
		{
			bool bOk = true;
			for (const float X : { -450.f, 0.f, 450.f }) if (H(M + D * A + L * X) - Soil < Floor + 520.f + 260.f) { bOk = false; break; }
			if (bOk) return A + 200.f;
		}
		return 1.0e9f;
	}
	bool InSilverPatch(const FVector2D& P)
	{
		const JeebTerrain::FFinePatch* F = JeebTerrain::FinePatchAt(P.X, P.Y);
		return F && F->bWalls;
	}
	void FinishMouth(FMouth& M)
	{
		M.Box = FBox(ForceInit);
		const FVector2D L = M.Lat();
		for (const float A : { -M.Lip, M.HoodLen })
			for (const float X : { -520.f, 520.f })
				for (const float Z : { -M.Below, 720.f }) M.Box += V3(XY(M.At) + M.D * A + L * X, M.At.Z + Z);
		M.Box = M.Box.ExpandBy(260.f);
		M.Outside = M.Kind == 1 ? M.At - V3(M.D, 0.f) * (M.Lip - 60.f) + FVector(0.f, 0.f, 10.f) : V3(XY(M.At) - M.D * (M.Lip + 600.f), 0.f);
		if (M.Kind != 1) M.Outside.Z = H(XY(M.Outside));
	}

	bool FindCliff(const FCtx& Cx, TArray<FMouth>& Out)
	{
		struct FC { float Phi, R, Zf, Score; };
		TArray<FC> Cands;
		for (int32 a = -32; a <= 32; ++a)
		{
			const float Phi = Cx.PhiC + FMath::DegreesToRadians(float(a));
			const FVector2D Do = Dir(Phi);
			struct FW { float R, Hi, Lo; };
			TArray<FW> Walls;
			float Prev = H(Cx.Mid + Do * 7000.f);
			for (float R = 7025.f; R < 22000.f; R += 25.f)
			{
				const float Hn = H(Cx.Mid + Do * R);
				if (Prev - Hn > 700.f) Walls.Add({ R - 25.f, H(Cx.Mid + Do * (R - 75.f)), H(Cx.Mid + Do * (R + 75.f)) });
				Prev = Hn;
			}
			for (int32 w = 0; w < Walls.Num(); ++w)
			{
				const FW& Wl = Walls[w];
				const float Drop = Wl.Hi - Wl.Lo;
				if (Drop < 2200.f || Wl.Lo < 3500.f) continue;
				const float Zf = FMath::Clamp(Wl.Lo + 0.5f * Drop - 200.f, Wl.Lo + 1100.f, Wl.Hi - 1000.f);
				const float Ledge = w + 1 < Walls.Num() ? Walls[w + 1].R - Wl.R : 3000.f;
				const FVector2D Face = Cx.Mid + Do * Wl.R;
				if (H(Face - Do * 300.f) < Zf + 820.f) continue;
				if (H(Face + Do * 260.f) > Zf - 800.f) continue;
				const float Score = 0.4f * (Zf - Wl.Lo) + 0.8f * FMath::Min(Ledge, 1500.f) * -0.6f - 0.35f * FMath::Abs(Zf - 0.5f * (JeebOpen::SilverTop + 4000.f)) - 8.f * FMath::Abs(float(a));
				Cands.Add({ Phi, Wl.R, Zf, Score });
			}
		}
		Cands.Sort([](const FC& A, const FC& B) { return A.Score > B.Score; });
		for (const FC& C : Cands)
		{
			bool bFar = true;
			for (const FMouth& O : Out) if (O.Kind == 1) bFar &= FMath::Abs(FMath::FindDeltaAngleRadians(FMath::Atan2(XY(O.At).Y - Cx.Mid.Y, XY(O.At).X - Cx.Mid.X), C.Phi)) > FMath::DegreesToRadians(22.f);
			if (!bFar) continue;
			FMouth M; M.Kind = 1; M.Name = Out.FilterByPredicate([](const FMouth& O) { return O.Kind == 1; }).Num() == 0 ? TEXT("North Cliff Mouth") : TEXT("South Cliff Mouth");
			const FVector2D Do = Dir(C.Phi);
			M.D = -Do; M.At = V3(Cx.Mid + Do * C.R, C.Zf); M.Lip = 230.f; M.Below = 380.f;
			M.HoodLen = FMath::Clamp(HoodLenAt(XY(M.At), M.D, C.Zf), 450.f, 1600.f);
			FinishMouth(M);
			Out.Add(M);
			UE_LOG(LogTemp, Log, TEXT("JEEBDEEP mouth %s: %.0f %.0f floor %.1f m, bearing %.0f from the cliff's middle, frame %.1f m, ground %.1f m below the lip"), *M.Name, M.At.X, M.At.Y, M.At.Z / 100.f,
				FMath::RadiansToDegrees(FMath::FindDeltaAngleRadians(Cx.PhiC, C.Phi)), M.HoodLen / 100.f, (M.At.Z - H(XY(M.At) - M.D * 400.f)) / 100.f);
			if (Out.FilterByPredicate([](const FMouth& O) { return O.Kind == 1; }).Num() >= 2) break;
		}
		return Out.Num() > 0;
	}

	bool FindRoadMouth(const FCtx& Cx, TArray<FMouth>& Out)
	{
		const TArray<FVector2D>& L = *Cx.Road;
		if (L.Num() < 100) return false;
		const float Z0 = H(L[0]), Z1 = H(L.Last());
		float Best = -1.0e9f; FMouth Pick;
		for (int32 i = 40; i + 40 < L.Num(); i += 5)
		{
			const float Z = H(L[i]);
			const float F = (Z - Z0) / FMath::Max(Z1 - Z0, 1.f);
			if (F < 0.4f || F > 0.62f) continue;
			const FVector2D T = (L[i + 8] - L[i - 8]).GetSafeNormal(), Ta = (L[i - 8] - L[i - 40]).GetSafeNormal(), Tb = (L[i + 40] - L[i + 8]).GetSafeNormal();
			if (FVector2D::DotProduct(Ta, T) < 0.93f || FVector2D::DotProduct(Tb, T) < 0.93f) continue;   // (on a straight leg, not a hairpin)
			FVector2D Lt(-T.Y, T.X);
			if (H(L[i] + Lt * 800.f) < H(L[i] - Lt * 800.f)) Lt = -Lt;
			const FVector2D M = L[i] + Lt * 330.f;
			const float Floor = Z + 4.f;
			bool bLevel = true;
			for (const float X : { -450.f, 0.f, 450.f }) bLevel &= FMath::Abs(H(L[i] + T * X) - Z) < 60.f;
			if (!bLevel) continue;
			const float Hl = HoodLenAt(M, Lt, Floor);
			if (Hl > 1500.f || !InSilverPatch(M)) continue;
			const float Score = -Hl - 300.f * FMath::Abs(F - 0.5f);
			if (Score > Best) { Best = Score; Pick.At = V3(M, Floor); Pick.D = Lt; Pick.HoodLen = Hl; }
		}
		if (Best < -1.0e8f) { UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP no road mouth: no straight stretch half way up with a steep enough bank")); return false; }
		Pick.Kind = 2; Pick.Name = TEXT("Road Mouth"); Pick.Lip = 60.f; Pick.Below = 320.f;
		FinishMouth(Pick);
		Out.Add(Pick);
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP mouth %s: %.0f %.0f floor %.1f m, frame %.1f m"), *Pick.Name, Pick.At.X, Pick.At.Y, Pick.At.Z / 100.f, Pick.HoodLen / 100.f);
		return true;
	}

	// a spot on a slope where a mouth can go in level: level ground in front, the hill rising behind; the best by Score
	bool FindSlopeMouth(const FCtx& Cx, const FBox2D& Box, float StepCm, TFunctionRef<bool(const FVector2D&, float)> Ok, TFunctionRef<float(const FVector2D&, const FVector2D&, float, float)> Score, FMouth& Out)
	{
		float Best = -1.0e9f;
		for (float X = Box.Min.X; X <= Box.Max.X; X += StepCm)
			for (float Y = Box.Min.Y; Y <= Box.Max.Y; Y += StepCm)
			{
				const FVector2D M(X, Y);
				const float F = H(M);
				if (!Ok(M, F)) continue;
				for (int32 k = 0; k < 24; ++k)
				{
					const FVector2D D = Dir(k * PI / 12.f), L(-D.Y, D.X);
					if (H(M + D * 600.f) < F + 350.f) continue;
					bool bLevel = true;
					for (const float Xl : { -430.f, 430.f }) { const float Hs = H(M + L * Xl); bLevel &= Hs > F - 90.f && Hs < F + 120.f; }
					const float Fr = H(M - D * 350.f);
					bLevel &= Fr > F - 120.f && Fr < F + 25.f;
					if (!bLevel) continue;
					const float Hl = HoodLenAt(M, D, F + 4.f);
					if (Hl > 1500.f) continue;
					const float S = Score(M, D, F, Hl);
					if (S > Best) { Best = S; Out.At = V3(M, F + 4.f); Out.D = D; Out.HoodLen = Hl; }
				}
			}
		return Best > -1.0e8f;
	}

	FHelix MakeHelix(const FVector2D& C, float Z0, float A0, float Z1, float A1Want)
	{
		FHelix Hx; Hx.C = C; Hx.Z0 = Z0; Hx.Z1 = Z1; Hx.A0 = A0;
		const float Need = FMath::Abs(Z1 - Z0) / (Grade * Rh);
		float Sw = FMath::UnwindRadians(A1Want - A0);
		if (Sw < 0.f) Sw += 2.f * PI;
		while (Sw + PI < Need) Sw += 2.f * PI;
		Hx.Sweep = FMath::Max(Sw, 1.2f);
		return Hx;
	}
	void HelixPoints(const FHelix& Hx, TArray<FVector>& Out);
	float PathCover(const TArray<FVector>& P, float Ht, float W, int32 SkipFirst);
	bool HelixOk(const FHelix& Hx)
	{
		TArray<FVector> P; HelixPoints(Hx, P);
		return PathCover(P, 530.f, 260.f, 0) >= NeedCover && H(Hx.C) - (FMath::Max(Hx.Z0, Hx.Z1) + 530.f) >= NeedCover;
	}
	void HelixPoints(const FHelix& Hx, TArray<FVector>& Out)
	{
		const int32 N = FMath::Max(8, FMath::CeilToInt(Hx.Sweep * Rh / 250.f));
		for (int32 i = 0; i <= N; ++i)
		{
			const float T = float(i) / N;
			Out.Add(V3(Hx.C + Dir(Hx.A0 + Hx.Sweep * T) * Rh, FMath::Lerp(Hx.Z0, Hx.Z1, T)));
		}
	}

	// a passage from A to B: a gentle bow (Bow: share of its length sideways), sampled every 3 m
	void Bowed(const FVector& A, const FVector& B, float Bow, TArray<FVector>& Out)
	{
		const FVector2D a = XY(A), b = XY(B), Ab = b - a;
		const float Len = Ab.Size();
		const FVector2D Ctl = 0.5f * (a + b) + FVector2D(-Ab.Y, Ab.X) / FMath::Max(Len, 1.f) * (Bow * Len);
		const int32 N = FMath::Max(2, FMath::CeilToInt(Len / 300.f));
		for (int32 i = 0; i <= N; ++i)
		{
			const float T = float(i) / N;
			const FVector2D P = (1.f - T) * (1.f - T) * a + 2.f * (1.f - T) * T * Ctl + T * T * b;
			Out.Add(V3(P, FMath::Lerp(A.Z, B.Z, Sm(0.f, 1.f, T))));
		}
	}
	float PathCover(const TArray<FVector>& P, float Ht, float W, int32 SkipFirst)
	{
		float Least = 1.0e9f;
		for (int32 i = SkipFirst; i < P.Num(); ++i)
		{
			const FVector2D T = XY(P[FMath::Min(i + 1, P.Num() - 1)] - P[FMath::Max(i - 1, 0)]).GetSafeNormal();
			Least = FMath::Min(Least, CoverAt(XY(P[i]), FVector2D(-T.Y, T.X), P[i].Z, Ht, W));
		}
		return Least;
	}

	int32 AddTunnel(TArray<FVector>&& Pts, int32 A, int32 B, uint8 Kind, float W, float Ht)
	{
		FTunnel T; T.P = MoveTemp(Pts); T.A = A; T.B = B; T.Kind = Kind; T.W = W; T.Ht = Ht;
		const int32 I = GTun.Add(T);
		if (A >= 0) { GNodes[A].Tun.Add(I); if (B >= 0) GNodes[A].Links.Add(B); }
		if (B >= 0) { GNodes[B].Tun.Add(I); if (A >= 0) GNodes[B].Links.Add(A); }
		return I;
	}

	// ---- the wall: march from a point out along Dir (level) until the rock; the last air point, Back cm short of it
	bool WallAlong(const FVector& From, const FVector2D& D, float MaxCm, float Back, FVector& Out)
	{
		for (float S = 0.f; S < MaxCm; S += 12.f)
		{
			const FVector P = From + V3(D, 0.f) * S;
			if (CaveField(P) > -6.f) { Out = From + V3(D, 0.f) * FMath::Max(0.f, S - Back); return S > Back; }
		}
		return false;
	}
	float FloorUnder(const FVector& P)
	{
		for (float Z = 0.f; Z < 1500.f; Z += 6.f) if (CaveField(P - FVector(0.f, 0.f, Z)) > 0.f) return P.Z - Z + 6.f;
		return P.Z - 1500.f;
	}
	FVector RoomPoint(const FRoom& R, float U, float V)
	{
		const float Cy = FMath::Cos(R.Yaw), Sy = FMath::Sin(R.Yaw);
		const float X = U * R.Rx, Y = V * R.Ry;
		return R.C + FVector(X * Cy - Y * Sy, X * Sy + Y * Cy, 0.f);
	}

	void PlaceThings()
	{
		GTorches.Reset(); GLights.Reset(); GProps.Reset(); GCrystals.Reset(); GChests.Reset(); GSpots.Reset();
		const FLinearColor Warm(1.f, 0.58f, 0.27f);
		auto Torch = [&](const FVector& At, const FVector2D& Inward)
		{
			// (SM_TorchWall: its back plate at its origin, standing out along +Y, the flame 84-109 cm up)
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Inward.Y, Inward.X)) - 90.f;
			const FTransform Xf(FRotator(0.f, Yaw, 0.f), At);
			GTorches.Add(Xf);
			GLights.Add({ Xf.TransformPosition(FVector(0.f, 48.f, 104.f)), Warm, 1.f, 1.f, true });
		};
		// lit passages: a torch every 11 m, one wall then the other
		int32 LitLen = 0, AllLen = 0;
		for (int32 t = 0; t < GTun.Num(); ++t)
		{
			FTunnel& T = GTun[t];
			float Len = 0.f;
			for (int32 i = 1; i < T.P.Num(); ++i) Len += FVector::Dist(T.P[i], T.P[i - 1]);
			AllLen += FMath::RoundToInt(Len / 100.f);
			if (!T.bLit) continue;
			LitLen += FMath::RoundToInt(Len / 100.f);
			float Next = 500.f, Run = 0.f; int32 Side = (t & 1) ? 1 : -1;
			for (int32 i = 1; i < T.P.Num(); ++i)
			{
				const float Sl = FVector::Dist(T.P[i], T.P[i - 1]);
				Run += Sl;
				if (Run < Next || Run > Len - 450.f) continue;
				Next = Run + 1100.f;
				const FVector2D Tg = XY(T.P[i] - T.P[i - 1]).GetSafeNormal(), Lt(-Tg.Y * Side, Tg.X * Side);
				FVector W;
				if (WallAlong(T.P[i] + FVector(0.f, 0.f, 215.f), Lt, 600.f, 4.f, W)) Torch(W, -Lt);
				Side = -Side;
			}
		}
		// the rooms
		int32 RoomIdx = 0;
		auto Mesh = [&](const TCHAR* M, const FTransform& Xf, bool bCollide, const TCHAR* Mat = TEXT(""), FLinearColor Col = FLinearColor::White, float Glow = 0.f)
		{
			FProp P; P.Mesh = M; P.Xf = Xf; P.bCollide = bCollide; P.Mat = Mat; P.Col = Col; P.Glow = Glow; GProps.Add(P);
		};
		auto WallSpot = [&](const FRoom& R, float Ang, float Up, float Back, FVector& Out, FVector2D& Inward) -> bool
		{
			const FVector2D D = Dir(Ang);
			if (!WallAlong(R.C + FVector(0.f, 0.f, Up), D, FMath::Max(R.Rx, R.Ry) * 1.6f, Back, Out)) return false;
			Inward = -D;
			return true;
		};
		for (int32 r = 0; r < GRooms.Num(); ++r)
		{
			const FRoom& R = GRooms[r];
			FRandomStream S(7100 + r * 37);
			const FVector Mid = R.C;
			if (R.bLit)
			{
				const int32 N = R.Kind == ERoom::Great ? 7 : R.Kind == ERoom::Library ? 2 : 5;
				for (int32 k = 0; k < N; ++k) { FVector W; FVector2D In; if (WallSpot(R, R.Yaw + (k + 0.5f) * 2.f * PI / N + 0.3f, 215.f, 4.f, W, In)) Torch(W, In); }
			}
			switch (R.Kind)
			{
			case ERoom::Crystal: case ERoom::Crystal2:
			{
				const FLinearColor Col = R.Kind == ERoom::Crystal ? FLinearColor(0.35f, 0.75f, 1.f) : FLinearColor(0.7f, 0.42f, 1.f);
				const int32 NC = R.Kind == ERoom::Crystal ? 9 : 6;
				for (int32 k = 0; k < NC; ++k)
				{
					FVector W; FVector2D In;
					if (!WallSpot(R, S.FRandRange(0.f, 2.f * PI), S.FRandRange(30.f, 420.f), 20.f, W, In)) continue;
					const float Size = S.FRandRange(0.7f, 1.5f);
					for (int32 n = S.RandRange(4, 8); n > 0; --n)
						GCrystals.Add({ W + FVector(S.FRandRange(-40.f, 40.f), S.FRandRange(-40.f, 40.f), S.FRandRange(-20.f, 40.f)) * Size, (V3(In, 0.f) * S.FRandRange(0.3f, 0.9f) + FVector(S.FRandRange(-0.3f, 0.3f), S.FRandRange(-0.3f, 0.3f), S.FRandRange(0.3f, 1.f))).GetSafeNormal(), float(Size * S.FRandRange(0.5f, 1.f)), Col });
					if (k % 3 == 0) GLights.Add({ W + V3(In, 0.f) * 120.f + FVector(0.f, 0.f, 80.f), FLinearColor::LerpUsingHSV(Col, FLinearColor(1.f, 0.9f, 0.8f), 0.3f), 1.1f, 1.35f, false });
				}
				// (a cluster on the floor in the middle)
				for (int32 n = 0; n < 7; ++n) GCrystals.Add({ Mid + FVector(S.FRandRange(-120.f, 120.f), S.FRandRange(-120.f, 120.f), 0.f), FVector(S.FRandRange(-0.4f, 0.4f), S.FRandRange(-0.4f, 0.4f), 1.f).GetSafeNormal(), float(S.FRandRange(0.8f, 1.6f)), Col });
				GLights.Add({ Mid + FVector(0.f, 0.f, 160.f), FLinearColor::LerpUsingHSV(Col, FLinearColor::White, 0.3f), 1.2f, 1.5f, false });
				break;
			}
			case ERoom::Mushroom:
			{
				const FLinearColor Cap[3] = { FLinearColor(0.2f, 0.95f, 0.8f), FLinearColor(0.75f, 0.35f, 1.f), FLinearColor(0.45f, 0.9f, 0.3f) };
				for (int32 k = 0; k < 26; ++k)
				{
					const float U = S.FRandRange(-0.85f, 0.85f), V = S.FRandRange(-0.85f, 0.85f);
					if (U * U + V * V > 0.72f) continue;
					FVector P = RoomPoint(R, U, V); P.Z = FloorUnder(P + FVector(0.f, 0.f, 150.f)) - 4.f;
					const float Ht = S.FRandRange(40.f, 190.f), Wd = Ht * S.FRandRange(0.6f, 1.f);
					const int32 C = S.RandRange(0, 2);
					Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FTransform(FRotator(S.FRandRange(-6.f, 6.f), 0.f, S.FRandRange(-6.f, 6.f)), P + FVector(0.f, 0.f, Ht * 0.5f), FVector(Wd * 0.0018f, Wd * 0.0018f, Ht / 100.f)), false, TEXT("color"), FLinearColor(0.78f, 0.74f, 0.64f));
					Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"), FTransform(FRotator::ZeroRotator, P + FVector(0.f, 0.f, Ht), FVector(Wd / 100.f, Wd / 100.f, Wd * 0.0042f)), Ht > 120.f, TEXT("glow"), Cap[C], 1.6f);
					if (k % 7 == 0) GLights.Add({ P + FVector(0.f, 0.f, Ht + 90.f), Cap[C], 0.8f, 1.2f, false });
				}
				for (int32 k = 0; k < 4; ++k) { FVector P = RoomPoint(R, S.FRandRange(-0.6f, 0.6f), S.FRandRange(-0.6f, 0.6f)); P.Z = FloorUnder(P + FVector(0.f, 0.f, 150.f)) - 4.f; Mesh(TEXT("/Game/TownDecor/SM_StumpMushroom.SM_StumpMushroom"), FTransform(FRotator(0.f, S.FRandRange(0.f, 360.f), 0.f), P), true); }
				break;
			}
			case ERoom::Lake:
			{
				FProp W; W.Mesh = TEXT("water"); W.Xf = FTransform(FRotator(0.f, FMath::RadiansToDegrees(R.Yaw), 0.f), R.C + FVector(0.f, 0.f, -35.f), FVector(0.66f * R.Rx, 0.6f * R.Ry, 1.f)); GProps.Add(W);
				break;
			}
			case ERoom::Mine:
			{
				FVector P = RoomPoint(R, 0.25f, -0.35f); P.Z = R.C.Z;
				Mesh(TEXT("/Game/SteeleCreek/SM_MineCart.SM_MineCart"), FTransform(FRotator(0.f, FMath::RadiansToDegrees(R.Yaw), 0.f), P), true);
				for (int32 k = 0; k < 5; ++k)
				{
					FVector W; FVector2D In;
					if (!WallSpot(R, R.Yaw + 0.6f + k * 1.15f, 60.f, 75.f, W, In)) continue;
					W.Z = R.C.Z;
					const TCHAR* M = k % 3 == 0 ? TEXT("/Game/TownDecor/SM_Crate.SM_Crate") : k % 3 == 1 ? TEXT("/Game/TownDecor/SM_Barrel.SM_Barrel") : TEXT("/Game/TownDecor/SM_LogStackSmall.SM_LogStackSmall");
					Mesh(M, FTransform(FRotator(0.f, S.FRandRange(0.f, 360.f), 0.f), W), true);
				}
				{ FVector P2 = RoomPoint(R, -0.3f, 0.3f); P2.Z = R.C.Z + 6.f; Mesh(TEXT("/Game/Props/SM_Pickaxe.SM_Pickaxe"), FTransform(FRotator(0.f, 30.f, 88.f), P2), false); }
				break;
			}
			case ERoom::Lair:
				for (int32 k = 0; k < 16; ++k)
				{
					FVector P = RoomPoint(R, S.FRandRange(-0.8f, 0.8f), S.FRandRange(-0.8f, 0.8f)); P.Z = FloorUnder(P + FVector(0.f, 0.f, 150.f)) + 4.f;
					Mesh(TEXT("/Game/Props/SM_BoneClub.SM_BoneClub"), FTransform(FRotator(S.FRandRange(-8.f, 8.f), S.FRandRange(0.f, 360.f), 88.f), P, FVector(S.FRandRange(0.7f, 1.3f))), false);
				}
				break;
			case ERoom::Library:
			{
				for (int32 k = 0; k < 4; ++k)
				{
					FVector W; FVector2D In;
					if (!WallSpot(R, R.Yaw + PI * 0.5f + (k - 1.5f) * 0.55f, 120.f, 45.f, W, In)) continue;
					W.Z = R.C.Z;
					Mesh(TEXT("/Game/Interior/Bookshelf/SM_Int_Bookshelf.SM_Int_Bookshelf"), FTransform(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(In.Y, In.X)), 0.f), W), true);
				}
				const FVector T = RoomPoint(R, 0.f, -0.15f);
				Mesh(TEXT("/Game/Interior/Trestle/SM_Int_Trestle.SM_Int_Trestle"), FTransform(FRotator(0.f, FMath::RadiansToDegrees(R.Yaw), 0.f), T), true);
				Mesh(TEXT("/Game/Interior/BookStack/SM_Int_BookStack.SM_Int_BookStack"), FTransform(FRotator(0.f, 20.f, 0.f), RoomPoint(R, 0.45f, 0.35f)), false);
				Mesh(TEXT("/Game/Interior/BookStack/SM_Int_BookStack.SM_Int_BookStack"), FTransform(FRotator(0.f, 75.f, 0.f), RoomPoint(R, -0.5f, 0.4f)), false);
				Mesh(TEXT("/Game/Interior/Scroll/SM_Int_Scroll.SM_Int_Scroll"), FTransform(FRotator(0.f, 40.f, 0.f), RoomPoint(R, 0.3f, -0.45f)), false);
				Mesh(TEXT("/Game/Interior/Candelabra/SM_Int_Candelabra.SM_Int_Candelabra"), FTransform(FRotator::ZeroRotator, RoomPoint(R, -0.55f, -0.3f)), true);
				GLights.Add({ RoomPoint(R, -0.55f, -0.3f) + FVector(0.f, 0.f, 160.f), FLinearColor(1.f, 0.7f, 0.4f), 0.45f, 0.75f, true });
				GLights.Add({ T + FVector(0.f, 0.f, 150.f), FLinearColor(1.f, 0.7f, 0.4f), 0.35f, 0.7f, true });
				break;
			}
			case ERoom::Shrine:
			{
				const FVector Al = RoomPoint(R, 0.62f, 0.f);
				Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(FRotator(0.f, FMath::RadiansToDegrees(R.Yaw), 0.f), Al + FVector(0.f, 0.f, 50.f), FVector(1.f, 2.2f, 1.f)), true, TEXT("/Game/M_Stone.M_Stone"));
				Mesh(TEXT("/Game/Interior/Candelabra/SM_Int_Candelabra.SM_Int_Candelabra"), FTransform(FRotator::ZeroRotator, RoomPoint(R, 0.55f, 0.32f)), true);
				Mesh(TEXT("/Game/Interior/Candelabra/SM_Int_Candelabra.SM_Int_Candelabra"), FTransform(FRotator::ZeroRotator, RoomPoint(R, 0.55f, -0.32f)), true);
				GLights.Add({ Al + FVector(-60.f, 0.f, 230.f), FLinearColor(1.f, 0.75f, 0.45f), 0.6f, 1.f, true });
				break;
			}
			case ERoom::Great:
				Mesh(TEXT("/Game/TownDecor/SM_LogPile.SM_LogPile"), FTransform(FRotator(0.f, 40.f, 0.f), RoomPoint(R, -0.4f, 0.45f)), true);
				Mesh(TEXT("/Game/TownDecor/SM_Firewood.SM_Firewood"), FTransform(FRotator(0.f, 0.f, 0.f), RoomPoint(R, 0.f, 0.f) + FVector(0.f, 0.f, -2.f)), false);
				break;
			default: break;
			}
			++RoomIdx;
		}
		// mine timbers along the mine's passages: posts by the walls and a beam across, every 4 m
		for (const FRoom& R : GRooms)
		{
			if (R.Kind != ERoom::Mine || R.Node < 0) continue;
			for (const int32 t : GNodes[R.Node].Tun)
			{
				const FTunnel& T = GTun[t];
				const bool bFwd = T.A == R.Node;
				float Run = 0.f, Next = R.Rx + 300.f;
				for (int32 j = 1; j < T.P.Num(); ++j)
				{
					const int32 i = bFwd ? j : T.P.Num() - 1 - j, ip = bFwd ? j - 1 : T.P.Num() - j;
					Run += FVector::Dist(T.P[i], T.P[ip]);
					if (Run < Next) continue;
					if (Run > 3600.f) break;
					Next = Run + 400.f;
					const FVector2D Tg = XY(T.P[i] - T.P[ip]).GetSafeNormal(), Lt(-Tg.Y, Tg.X);
					const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Tg.Y, Tg.X));
					FVector Wa, Wb;
					if (!WallAlong(T.P[i] + FVector(0.f, 0.f, 120.f), Lt, 500.f, 30.f, Wa) || !WallAlong(T.P[i] + FVector(0.f, 0.f, 120.f), -Lt, 500.f, 30.f, Wb)) continue;
					Wa.Z = Wb.Z = T.P[i].Z;
					const float Span = FVector2D::Distance(XY(Wa), XY(Wb));
					Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(FRotator(0.f, Yaw, 0.f), Wa + FVector(0.f, 0.f, 165.f), FVector(0.24f, 0.24f, 3.4f)), true, TEXT("/Game/M_WoodV.M_WoodV"));
					Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(FRotator(0.f, Yaw, 0.f), Wb + FVector(0.f, 0.f, 165.f), FVector(0.24f, 0.24f, 3.4f)), true, TEXT("/Game/M_WoodV.M_WoodV"));
					Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(FRotator(0.f, Yaw, 0.f), 0.5f * (Wa + Wb) + FVector(0.f, 0.f, 345.f), FVector(0.26f, Span / 100.f + 0.1f, 0.26f)), false, TEXT("/Game/M_WoodH.M_WoodH"));
				}
				// rails down the middle of the first 30 m, with sleepers
				float Rr = 0.f;
				for (int32 j = 1; j < T.P.Num(); ++j)
				{
					const int32 i = bFwd ? j : T.P.Num() - 1 - j, ip = bFwd ? j - 1 : T.P.Num() - j;
					const FVector A = T.P[ip], B = T.P[i];
					Rr += FVector::Dist(A, B);
					if (Rr > 3000.f) break;
					const FVector2D Tg = XY(B - A).GetSafeNormal(), Lt(-Tg.Y, Tg.X);
					const FRotator Ro = (B - A).Rotation();
					const float L = FVector::Dist(A, B);
					for (const float X : { -55.f, 55.f })
						Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(Ro, 0.5f * (A + B) + V3(Lt * X, 9.f), FVector(L / 100.f + 0.02f, 0.06f, 0.08f)), false, TEXT("/Game/M_Metal.M_Metal"));
					for (float S2 = 0.f; S2 < L; S2 += 75.f)
						Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(Ro, A + (B - A) * (S2 / L) + FVector(0.f, 0.f, 2.f), FVector(0.16f, 1.5f, 0.06f)), false, TEXT("/Game/M_WoodH.M_WoodH"));
				}
			}
		}
		// the quest-ready spots (Content/Data/CaveSpots.csv): a room's kind, what goes there, and where (shares of the room's radii, along its
		// long way and across), snapped onto its floor and in from its walls; a chest row puts a chest there
		{
			TArray<FString> Lines;
			const FString Path = FPaths::ProjectContentDir() / TEXT("Data/CaveSpots.csv");
			if (!FFileHelper::LoadFileToStringArray(Lines, *Path)) UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP no %s: no spots and no chests in the Deeps"), *Path);
			for (int32 n = 1; n < Lines.Num(); ++n)
			{
				FString L = Lines[n].TrimStartAndEnd();
				if (L.IsEmpty() || L.StartsWith(TEXT("#"))) continue;
				TArray<FString> C; L.ParseIntoArray(C, TEXT(","), false);
				if (C.Num() < 5) continue;
				const FString Room = C[0].TrimStartAndEnd(), Kind = C[1].TrimStartAndEnd().ToLower();
				const FRoom* R = nullptr;
				for (const FRoom& Q : GRooms) if (Room.Equals(RoomName(Q.Kind), ESearchCase::IgnoreCase)) R = &Q;
				if (!R) { UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP CaveSpots.csv line %d: no room called %s"), n + 1, *Room); continue; }
				FVector P = RoomPoint(*R, FCString::Atof(*C[2]), FCString::Atof(*C[3]));
				// (in from the wall: 1.2 m clear round it, toward the room's middle)
				for (int32 k = 0; k < 40; ++k)
				{
					const FVector Up = P + FVector(0.f, 0.f, 200.f);
					bool bClear = CaveField(Up) < -110.f;
					if (bClear) break;
					P += (R->C - P).GetSafeNormal2D() * 25.f;
				}
				P.Z = FloorUnder(P + FVector(0.f, 0.f, 200.f));
				FSpot Sp; Sp.Room = RoomName(R->Kind); Sp.Kind = Kind; Sp.At = P; Sp.Yaw = FMath::RadiansToDegrees(R->Yaw) + FCString::Atof(*C[4]); Sp.Note = C.Num() > 5 ? C[5].TrimStartAndEnd() : FString();
				GSpots.Add(Sp);
				if (Kind == TEXT("chest")) GChests.Add({ FTransform(FRotator(0.f, Sp.Yaw, 0.f), P - FVector(0.f, 0.f, 2.f)), R->C, int32(R - GRooms.GetData()) });
				UE_LOG(LogTemp, Log, TEXT("JEEBDEEP spot %s %s at %.0f %.0f %.0f yaw %.0f %s"), *Sp.Room, *Sp.Kind, P.X, P.Y, P.Z, Sp.Yaw, *Sp.Note);
			}
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP things: %d wall torches, %d lights in all, %d props, %d crystals, %d spots (%d chests); passages %d m, %d m of them lit (%.0f%%)"),
			GTorches.Num(), GLights.Num(), GProps.Num(), GCrystals.Num(), GSpots.Num(), GChests.Num(), AllLen, LitLen, 100.f * LitLen / FMath::Max(AllLen, 1));
	}

	// lumps, cones and pillars of rock in the rooms
	void AddSolids()
	{
		GSolids.Reset();
		auto Add = [&](uint8 T, const FVector& C, const FVector& R)
		{
			// (2026-11-28: never in a passage's way: the walk test stuck on a cone where a ramp leaves the Great cavern)
			const float Keep = FMath::Max(R.X, R.Y) + 180.f;
			if (T != 3) for (const FTunnel& Tn : GTun)
				for (int32 i = 0; i + 1 < Tn.P.Num(); ++i)
				{
					const FVector A = Tn.P[i], B = Tn.P[i + 1];
					if (FMath::Min(A.Z, B.Z) - 300.f > C.Z + R.Z + 100.f || FMath::Max(A.Z, B.Z) + Tn.Ht < C.Z - R.Z - 100.f) continue;
					if (FMath::PointDistToSegment(FVector(C.X, C.Y, 0.f), FVector(A.X, A.Y, 0.f), FVector(B.X, B.Y, 0.f)) < Tn.W + Keep) return;
				}
			FSolid S; S.T = T; S.C = C; S.R = R;
			const float Rm = FMath::Max3(R.X, R.Y, R.Z) + 120.f;
			S.Box = T == 3 ? FBox(C - FVector(R.X + 150.f, R.X + 150.f, 300.f), C + FVector(R.X + 150.f, R.X + 150.f, 2500.f)) : FBox(C - FVector(Rm), C + FVector(Rm));
			GSolids.Add(S);
		};
		for (int32 r = 0; r < GRooms.Num(); ++r)
		{
			const FRoom& R = GRooms[r];
			FRandomStream S(3300 + r * 53);
			const bool bCones = R.Kind == ERoom::Great || R.Kind == ERoom::Lake || R.Kind == ERoom::Crystal || R.Kind == ERoom::Collapsed || R.Kind == ERoom::Lair || R.Kind == ERoom::Mushroom;
			if (bCones)
			{
				const int32 N = R.Kind == ERoom::Great ? 14 : 8;
				for (int32 k = 0; k < N; ++k)
				{
					const float A = S.FRandRange(0.f, 2.f * PI), F = S.FRandRange(0.55f, 0.92f);
					const FVector P = RoomPoint(R, F * FMath::Cos(A), F * FMath::Sin(A));
					const float Hh = S.FRandRange(90.f, 0.32f * R.Ht);
					if (k & 1) Add(1, FVector(P.X, P.Y, R.C.Z - 20.f), FVector(Hh * S.FRandRange(0.22f, 0.34f), 0.f, Hh));
					else Add(2, FVector(P.X, P.Y, R.C.Z + R.Ht * (0.9f - 0.5f * F * F)), FVector(Hh * S.FRandRange(0.18f, 0.28f), 0.f, Hh * 0.8f));
				}
			}
			if (R.Kind == ERoom::Collapsed)
			{
				// (a fall of rock heaped against one side, and a broken column)
				for (int32 k = 0; k < 22; ++k)
				{
					const float U = S.FRandRange(0.15f, 0.85f), V = S.FRandRange(-0.75f, 0.1f);
					const FVector P = RoomPoint(R, U, V) + FVector(0.f, 0.f, S.FRandRange(-60.f, 80.f) * (1.f - U));
					const float Rr = S.FRandRange(70.f, 260.f) * (1.2f - 0.5f * U);
					Add(0, P, FVector(Rr * S.FRandRange(0.9f, 1.4f), Rr * S.FRandRange(0.9f, 1.4f), Rr * S.FRandRange(0.6f, 0.9f)));
				}
				Add(3, RoomPoint(R, -0.45f, 0.4f) - FVector(0.f, 0.f, 50.f), FVector(150.f, 0.f, 0.f));
			}
			if (R.Kind == ERoom::Shrine)
				for (int32 k = 0; k < 6; ++k)
				{
					const float U = (k % 3 - 1) * 0.55f, V = (k < 3 ? -1.f : 1.f) * 0.5f;
					Add(3, RoomPoint(R, U, V) - FVector(0.f, 0.f, 50.f), FVector(85.f, 0.f, 0.f));
				}
		}
	}

	void BuildGrid()
	{
		GSegs.Reset(); GGrid.Reset(); GAll = FBox(ForceInit);
		for (const FTunnel& T : GTun)
			for (int32 i = 0; i + 1 < T.P.Num(); ++i)
			{
				FSeg S; S.A = T.P[i]; S.B = T.P[i + 1]; S.W = T.W; S.Ht = T.Ht;
				const int32 I = GSegs.Add(S);
				FBox B(ForceInit); B += S.A; B += S.B;
				B.Min -= FVector(S.W + 360.f, S.W + 360.f, 360.f); B.Max += FVector(S.W + 360.f, S.W + 360.f, S.Ht + 360.f);
				GAll += B;
				const FIntVector C0 = CellOf(B.Min), C1 = CellOf(B.Max);
				for (int32 x = C0.X; x <= C1.X; ++x) for (int32 y = C0.Y; y <= C1.Y; ++y) for (int32 z = C0.Z; z <= C1.Z; ++z) GGrid.FindOrAdd(FIntVector(x, y, z)).Add(I);
			}
		for (FRoom& R : GRooms)
		{
			const float M = FMath::Max(R.Rx, R.Ry) + 450.f;
			R.Box = FBox(R.C - FVector(M, M, R.Basin + 450.f), R.C + FVector(M, M, R.Ht + 450.f));
			GAll += R.Box;
		}
		for (const FMouth& M : GMouths) GAll += M.Box;
	}

	bool Build(const FCtx& Cx)
	{
		GMouths.Reset(); GRooms.Reset(); GTun.Reset(); GNodes.Reset(); GHelix.Reset();
		const float Top = JeebOpen::SilverTop;
		// ---- the mouths
		FindCliff(Cx, GMouths);
		FindRoadMouth(Cx, GMouths);
		{
			// the foot: by the road just before the climb begins, the hill rising behind
			const FVector2D F = (*Cx.Road)[0];
			const float Rf = FVector2D::Distance(F, Cx.Mid);
			FMouth M;
			const bool bOk = FindSlopeMouth(Cx, FBox2D(F - FVector2D(6000.f, 6000.f), F + FVector2D(6000.f, 6000.f)), 200.f,
				[&](const FVector2D& P, float) { const float Rd = RoadDist(P); return Rd > 1000.f && Rd < 4500.f && FVector2D::Distance(P, Cx.Mid) > Rf - 2500.f && InSilverPatch(P) && JeebWorld::WaterEdge(P.X, P.Y) > 1500.f; },
				[&](const FVector2D& P, const FVector2D& D, float, float Hl) { return -Hl - 0.4f * float(FVector2D::Distance(P, F)) + 300.f * FVector2D::DotProduct(D, (Cx.Mid - P).GetSafeNormal()); }, M);
			if (bOk) { M.Kind = 3; M.Name = TEXT("Foot Mouth"); M.Lip = 60.f; M.Below = 320.f; FinishMouth(M); GMouths.Add(M); UE_LOG(LogTemp, Log, TEXT("JEEBDEEP mouth %s: %.0f %.0f floor %.1f m, frame %.1f m, %.0f m from the road's start"), *M.Name, M.At.X, M.At.Y, M.At.Z / 100.f, M.HoodLen / 100.f, FVector2D::Distance(XY(M.At), F) / 100.f); }
			else UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP no foot mouth near the road's start %.0f %.0f"), F.X, F.Y);
		}
		{
			// the summit: in a slope rising from the village's edge (the knolls on the rim), clear of the streets and the houses
			FMouth M;
			const bool bOk = FindSlopeMouth(Cx, FBox2D(Cx.Mid - FVector2D(11000.f, 11000.f), Cx.Mid + FVector2D(11000.f, 11000.f)), 150.f,
				[&](const FVector2D& P, float F)
				{
					if (F < Top - 700.f || F > Top + 300.f || JeebSilver::StreetClearance(P.X, P.Y) < 700.f) return false;
					for (const JeebSilver::FPlot& Pl : JeebSilver::Plots()) if (FVector2D::Distance(P, Pl.Centre) < Pl.Reach * 0.5f + 900.f) return false;
					return true;
				},
				[&](const FVector2D& P, const FVector2D&, float F, float Hl) { return -Hl - 0.5f * FMath::Abs(F - Top) - 0.2f * float(FVector2D::Distance(P, Cx.Mid)); }, M);
			if (bOk) { M.Kind = 0; M.Name = TEXT("Summit Mouth"); M.Lip = 60.f; M.Below = 320.f; FinishMouth(M); GMouths.Add(M); UE_LOG(LogTemp, Log, TEXT("JEEBDEEP mouth %s: %.0f %.0f floor %.1f m (village %.1f), frame %.1f m, %.0f m from the middle"), *M.Name, M.At.X, M.At.Y, M.At.Z / 100.f, Top / 100.f, M.HoodLen / 100.f, FVector2D::Distance(XY(M.At), Cx.Mid) / 100.f); }
			else UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP no summit mouth"));
		}
		const FMouth* Foot = GMouths.FindByPredicate([](const FMouth& M) { return M.Kind == 3; });
		float Zmid = 0.f; int32 Nmid = 0;
		for (const FMouth& M : GMouths) if (M.Kind == 1 || M.Kind == 2) { Zmid += M.At.Z; ++Nmid; }
		if (!Foot || Nmid == 0) { UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP not enough mouths (foot %d, middle %d): no Deeps"), Foot ? 1 : 0, Nmid); return false; }
		Zmid /= Nmid;
		GLevels[0] = Foot->At.Z + 50.f; GLevels[2] = Zmid; GLevels[1] = 0.5f * (GLevels[0] + GLevels[2]); GLevels[4] = Top - 2600.f; GLevels[3] = 0.5f * (GLevels[2] + GLevels[4]);
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP levels %.1f %.1f %.1f %.1f %.1f m"), GLevels[0] / 100.f, GLevels[1] / 100.f, GLevels[2] / 100.f, GLevels[3] / 100.f, GLevels[4] / 100.f);

		// ---- the levels' grid points
		constexpr int32 NL = 5;
		const float RMax[NL] = { 17500.f, 15500.f, 13500.f, 11000.f, 8500.f };
		TArray<TMap<FIntPoint, int32>> GridOf; GridOf.SetNum(NL);
		for (int32 k = 0; k < NL; ++k)
		{
			const float Th = 0.61f * k + 0.2f;
			const FVector2D Ax = Dir(Th), Ay = Dir(Th + 0.5f * PI);
			const int32 N = FMath::CeilToInt(RMax[k] / Spacing);
			for (int32 i = -N; i <= N; ++i)
				for (int32 j = -N; j <= N; ++j)
				{
					const uint32 Hs = uint32(k * 7919 + (i + 50) * 104729 + (j + 50) * 1299709);
					const FVector2D P = Cx.Mid + Ax * (i * Spacing + (Hash01(Hs, 1) - 0.5f) * 1100.f) + Ay * (j * Spacing + (Hash01(Hs, 2) - 0.5f) * 1100.f);
					if (FVector2D::Distance(P, Cx.Mid) > RMax[k]) continue;
					const float Z = GLevels[k] + (Hash01(Hs, 3) - 0.5f) * 260.f;
					if (!NodeOk(P, Z, 600.f, 900.f)) continue;
					FNode Nd; Nd.P = V3(P, Z); Nd.Level = k;
					GridOf[k].Add(FIntPoint(i, j), GNodes.Add(Nd));
				}
		}
		TArray<bool> Gone; Gone.Init(false, GNodes.Num());
		auto Exclude = [&](const FVector2D& C, int32 Level, int32 Keep, float R)
		{
			for (int32 n = 0; n < GNodes.Num(); ++n) if (n != Keep && GNodes[n].Level == Level && FVector2D::Distance(XY(GNodes[n].P), C) < R) { if (n < Gone.Num()) Gone[n] = true; }
		};
		TArray<TPair<int32, int32>> Must;   // node pairs that must be joined (with the path given in MustPath)
		TArray<TArray<FVector>> MustPath;
		TArray<uint8> MustKind;
		auto Nearest = [&](const FVector& P, int32 Level, float MinD, float MaxGrade) -> int32
		{
			int32 Best = -1; float Bd = 1.0e18f;
			for (int32 n = 0; n < GNodes.Num(); ++n)
			{
				if (GNodes[n].Level != Level || GNodes[n].bHelix || GNodes[n].Mouth >= 0 || (n < Gone.Num() && Gone[n])) continue;
				const float D = FVector2D::Distance(XY(GNodes[n].P), XY(P));
				if (D < MinD || FMath::Abs(GNodes[n].P.Z - P.Z) > MaxGrade * D) continue;
				if (D < Bd) { Bd = D; Best = n; }
			}
			return Best;
		};
		// ---- the spirals between the levels: two between each pair, round a pillar beside a grid point of the lower level
		for (int32 k = 0; k + 1 < NL; ++k)
		{
			int32 Made = 0;
			TArray<int32> Cands;
			for (const TPair<FIntPoint, int32>& G : GridOf[k]) Cands.Add(G.Value);
			Cands.Sort([&](int32 A, int32 B) { return Hash01(A * 31 + k) < Hash01(B * 31 + k); });
			for (const int32 n : Cands)
			{
				if (Made >= 2 || Gone[n] || GNodes[n].bHelix) continue;
				bool bFar = true;
				for (const FHelix& O : GHelix) bFar &= FVector2D::Distance(O.C, XY(GNodes[n].P)) > 5000.f;
				for (const FMouth& M : GMouths) bFar &= FVector2D::Distance(XY(M.At), XY(GNodes[n].P)) > 3500.f;
				if (!bFar) continue;
				for (int32 q = 0; q < 4; ++q)
				{
					const float Aout = 0.61f * k + 0.2f + q * 0.5f * PI;
					const FVector2D C = XY(GNodes[n].P) + Dir(Aout) * Rh;
					FHelix Hx = MakeHelix(C, GNodes[n].P.Z, Aout + PI, GLevels[k + 1], 0.f);
					// (the top's way out: toward the nearest point of the level above beyond the spiral)
					int32 Up = -1; float Bd = 1.0e18f;
					for (const TPair<FIntPoint, int32>& G : GridOf[k + 1])
					{
						const float D = FVector2D::Distance(XY(GNodes[G.Value].P), C);
						if (D > 2600.f && D < Bd && !Gone[G.Value]) { Bd = D; Up = G.Value; }
					}
					if (Up < 0) continue;
					const FVector2D Tu = XY(GNodes[Up].P) - C;
					Hx = MakeHelix(C, GNodes[n].P.Z, Aout + PI, GLevels[k + 1], FMath::Atan2(Tu.Y, Tu.X));
					if (!HelixOk(Hx)) continue;
					Exclude(C, k, n, 2400.f); Exclude(C, k + 1, -1, 2400.f);
					if (Gone[Up]) continue;
					FNode Port; Port.P = V3(C + Dir(Hx.A0 + Hx.Sweep) * Rh, GLevels[k + 1]); Port.Level = k + 1; Port.bHelix = true;
					const int32 Pi = GNodes.Add(Port); Gone.Add(false);
					GNodes[n].bHelix = true;
					Hx.Lo = n; Hx.Hi = Pi;
					GHelix.Add(Hx);
					TArray<FVector> Pts; HelixPoints(Hx, Pts);
					Must.Add({ n, Pi }); MustPath.Add(Pts); MustKind.Add(1);
					TArray<FVector> Out; Out.Add(Port.P); Out.Add(V3(XY(Port.P) + (XY(GNodes[Up].P) - XY(Port.P)).GetSafeNormal() * 300.f, Port.P.Z));
					Bowed(Out.Last(), GNodes[Up].P, 0.f, Out); Out.RemoveAt(2);
					Must.Add({ Pi, Up }); MustPath.Add(Out); MustKind.Add(0);
					++Made;
					UE_LOG(LogTemp, Log, TEXT("JEEBDEEP spiral %d-%d at %.0f %.0f: %.1f to %.1f m, %.2f turns"), k, k + 1, C.X, C.Y, Hx.Z0 / 100.f, Hx.Z1 / 100.f, Hx.Sweep / (2.f * PI));
					break;
				}
			}
			if (Made < 2) UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP only %d spirals from level %d up (of %d points there)"), Made, k, Cands.Num());
		}
		// ---- the mouths' passages: in level through the frame, then on to the nearest point of their level (the summit: a spiral down)
		for (int32 m = 0; m < GMouths.Num(); ++m)
		{
			FMouth& M = GMouths[m];
			FNode Mn; Mn.P = M.At; Mn.Level = -1; Mn.Mouth = m;
			M.Node = GNodes.Add(Mn); Gone.Add(false);
			TArray<FVector> Pts;
			const float Ain = M.HoodLen + 300.f;
			for (float A = -M.Lip - 40.f; A < Ain; A += 250.f) Pts.Add(V3(XY(M.At) + M.D * A, M.At.Z));
			Pts.Add(V3(XY(M.At) + M.D * Ain, M.At.Z));
			if (M.Kind == 0)
			{
				// (in and down a straight ramp, then a spiral down to the highest level it can reach)
				bool bDone = false;
				const int32 Skip = Pts.Num();
				for (int32 Lv = 4; Lv >= 2 && !bDone; --Lv)
					for (float Run = 0.f; Run <= 4500.f && !bDone; Run += 500.f)
					{
						TArray<FVector> P2 = Pts;
						for (float A = 300.f; A <= Run; A += 300.f) P2.Add(V3(XY(M.At) + M.D * (Ain + A), M.At.Z - Grade * A));
						const FVector S0 = P2.Last();
						if (S0.Z < GLevels[Lv] + 600.f) continue;
						const FVector2D C = XY(S0) + M.D * Rh;
						const int32 To = Nearest(V3(C, GLevels[Lv]), Lv, 2800.f, 0.25f);
						if (To < 0) continue;
						const FVector2D Tu = XY(GNodes[To].P) - C;
						const FHelix Hx = MakeHelix(C, S0.Z, FMath::Atan2(-M.D.Y, -M.D.X), GLevels[Lv], FMath::Atan2(Tu.Y, Tu.X));
						TArray<FVector> Hp; HelixPoints(Hx, Hp);
						if (PathCover(P2, 530.f, 260.f, Skip) < NeedCover || PathCover(Hp, 530.f, 260.f, 0) < NeedCover) continue;
						Hp.RemoveAt(0);
						P2.Append(Hp);
						Pts = MoveTemp(P2);
						Exclude(C, Lv, -1, 2400.f);
						FNode Port; Port.P = Pts.Last(); Port.Level = Lv; Port.bHelix = true;
						const int32 Pi = GNodes.Add(Port); Gone.Add(false);
						GHelix.Add(Hx);
						Must.Add({ M.Node, Pi }); MustPath.Add(Pts); MustKind.Add(2);
						const int32 To2 = Nearest(Port.P, Lv, 1500.f, 0.25f);
						if (To2 >= 0) { TArray<FVector> Out; Bowed(Port.P, GNodes[To2].P, 0.f, Out); Must.Add({ Pi, To2 }); MustPath.Add(Out); MustKind.Add(0); }
						UE_LOG(LogTemp, Log, TEXT("JEEBDEEP summit way down: a %.0f m ramp, then a spiral at %.0f %.0f from %.1f to %.1f m (level %d), %.2f turns"), Run / 100.f, C.X, C.Y, Hx.Z0 / 100.f, Hx.Z1 / 100.f, Lv, Hx.Sweep / (2.f * PI));
						bDone = true;
					}
				if (!bDone) UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP the summit mouth found no room for its spiral"));
				continue;
			}
			const int32 Lv = M.Kind == 3 ? 0 : 2;
			const int32 To = Nearest(Pts.Last(), Lv, 1200.f, 0.2f);
			if (To < 0) { UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP %s: no point of level %d near"), *M.Name, Lv); continue; }
			TArray<FVector> Rest;
			Bowed(Pts.Last(), GNodes[To].P, 0.12f * ((m & 1) ? 1.f : -1.f), Rest);
			Rest.RemoveAt(0);
			Pts.Append(Rest);
			Must.Add({ M.Node, To }); MustPath.Add(Pts); MustKind.Add(2);
		}
		// ---- the maze on each level: a random depth-first walk over the grid (a perfect maze), then a few loops
		auto EdgeOk = [&](int32 A, int32 B) -> bool
		{
			const FVector Pa = GNodes[A].P, Pb = GNodes[B].P;
			for (const FHelix& Hx : GHelix)
			{
				const FVector2D C = Hx.C;
				const FVector2D Ab = XY(Pb) - XY(Pa);
				for (float T = 0.f; T <= 1.f; T += 0.05f)
				{
					const FVector2D Q = XY(Pa) + Ab * T;
					if (FVector2D::Distance(Q, C) >= 1900.f) continue;
					if ((Hx.Lo == A && T < 0.25f) || (Hx.Lo == B && T > 0.75f)) continue;
					if (FMath::Abs(FMath::Lerp(Pa.Z, Pb.Z, T) - FMath::Clamp(FMath::Lerp(Pa.Z, Pb.Z, T), FMath::Min(Hx.Z0, Hx.Z1) - 900.f, FMath::Max(Hx.Z0, Hx.Z1) + 900.f)) > 0.f) continue;
					return false;
				}
			}
			TArray<FVector> P; Bowed(Pa, Pb, 0.f, P);
			return PathCover(P, 560.f, 270.f, 0) >= NeedCover;
		};
		TArray<TArray<int32>> Cand; Cand.SetNum(GNodes.Num());
		for (int32 k = 0; k < NL; ++k)
			for (const TPair<FIntPoint, int32>& G : GridOf[k])
			{
				if (Gone[G.Value]) continue;
				for (const FIntPoint Dn : { FIntPoint(1, 0), FIntPoint(0, 1) })
					if (const int32* O = GridOf[k].Find(G.Key + Dn))
						if (!Gone[*O] && EdgeOk(G.Value, *O)) { Cand[G.Value].Add(*O); Cand[*O].Add(G.Value); }
			}
		TSet<uint64> Joined;
		auto Key = [](int32 A, int32 B) { return (uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B)); };
		TArray<bool> Seen; Seen.Init(false, GNodes.Num());
		FRandomStream Rs(4471);
		TArray<TPair<int32, int32>> TreeEdges;
		auto Walk = [&](int32 Root)
		{
			if (Seen[Root]) return;
			TArray<int32> Stack; Stack.Add(Root); Seen[Root] = true;
			while (Stack.Num())
			{
				const int32 Cur = Stack.Last();
				TArray<int32> Free;
				for (const int32 O : Cand[Cur]) if (!Seen[O]) Free.Add(O);
				if (Free.Num() == 0) { Stack.Pop(); continue; }
				const int32 Nx = Free[Rs.RandRange(0, Free.Num() - 1)];
				Seen[Nx] = true; Stack.Add(Nx);
				TreeEdges.Add({ Cur, Nx }); Joined.Add(Key(Cur, Nx));
			}
		};
		for (const TPair<int32, int32>& Mp : Must) { if (Mp.Key < Cand.Num()) Walk(Mp.Key); if (Mp.Value < Cand.Num()) Walk(Mp.Value); }
		// (loops: about one grid edge in seven not in the tree)
		TArray<TPair<int32, int32>> Loops;
		for (int32 a = 0; a < Cand.Num(); ++a)
			for (const int32 b : Cand[a])
				if (a < b && Seen[a] && !Joined.Contains(Key(a, b)) && Hash01(a * 977 + b, 77) < 0.15f) { Loops.Add({ a, b }); Joined.Add(Key(a, b)); }
		// ---- the passages
		for (int32 i = 0; i < Must.Num(); ++i)
		{
			const uint8 Kd = MustKind[i];
			const int32 T = AddTunnel(MoveTemp(MustPath[i]), Must[i].Key, Must[i].Value, Kd, Kd == 0 ? 250.f : 260.f, Kd == 0 ? 520.f : 530.f);
			(void)T;
		}
		int32 Dropped = 0;
		auto Edge = [&](int32 A, int32 B, bool bLoop)
		{
			const float Bow = (Hash01(A * 31 + B, 5) - 0.5f) * 0.5f;
			const float W = 225.f + 45.f * Hash01(A + B * 13, 6), Ht = 470.f + 90.f * Hash01(A * 7 + B, 8);
			TArray<FVector> P; Bowed(GNodes[A].P, GNodes[B].P, Bow, P);
			if (PathCover(P, Ht + 40.f, W, 0) < NeedCover) { P.Reset(); Bowed(GNodes[A].P, GNodes[B].P, 0.f, P); }
			if (bLoop && PathCover(P, Ht + 40.f, W, 0) < NeedCover) { ++Dropped; return; }
			AddTunnel(MoveTemp(P), A, B, 0, W, Ht);
		};
		for (const TPair<int32, int32>& E : TreeEdges) Edge(E.Key, E.Value, false);
		for (const TPair<int32, int32>& E : Loops) Edge(E.Key, E.Value, true);
		// (what no mouth reaches is left out)
		TArray<bool> Reach; Reach.Init(false, GNodes.Num());
		{
			TArray<int32> Q;
			for (const FMouth& M : GMouths) if (M.Node >= 0) { Reach[M.Node] = true; Q.Add(M.Node); }
			while (Q.Num()) { const int32 C = Q.Pop(); for (const int32 O : GNodes[C].Links) if (!Reach[O]) { Reach[O] = true; Q.Add(O); } }
		}
		int32 Unreached = 0;
		for (int32 t = GTun.Num() - 1; t >= 0; --t) if (!Reach[GTun[t].A] && !Reach[GTun[t].B]) { GTun.RemoveAt(t); ++Unreached; }
		for (FNode& N : GNodes) { N.Tun.Reset(); N.Links.Reset(); }
		for (int32 t = 0; t < GTun.Num(); ++t) { const FTunnel& T = GTun[t]; GNodes[T.A].Tun.Add(t); GNodes[T.B].Tun.Add(t); GNodes[T.A].Links.Add(T.B); GNodes[T.B].Links.Add(T.A); }

		// ---- the rooms: on points of the maze (none on a spiral's foot or a mouth's), well apart
		struct FRk { ERoom K; int32 Level; float Rx, Ry, Ht; bool bLit; int32 Want; /* 0 any, 1 a dead end, 2 a crossing, 3 near the foot mouth */ };
		const FRk Kinds[] = {
			{ ERoom::Mine, 0, 1200.f, 900.f, 650.f, true, 3 }, { ERoom::Lake, 0, 1650.f, 1300.f, 900.f, false, 2 }, { ERoom::Lair, 0, 1250.f, 1000.f, 800.f, false, 1 },
			{ ERoom::Mushroom, 1, 1350.f, 1100.f, 900.f, false, 2 }, { ERoom::Collapsed, 1, 1500.f, 1200.f, 1100.f, false, 0 },
			{ ERoom::Great, 2, 2000.f, 1600.f, 1400.f, true, 2 }, { ERoom::Crystal, 2, 1100.f, 900.f, 800.f, false, 0 },
			{ ERoom::Shrine, 3, 1250.f, 1000.f, 1000.f, true, 2 }, { ERoom::Library, 3, 650.f, 560.f, 520.f, true, 1 },
			{ ERoom::Crystal2, 4, 900.f, 800.f, 700.f, false, 0 },
		};
		TArray<FRk> Todo(Kinds, UE_ARRAY_COUNT(Kinds)), Retry;
		for (int32 Pass = 0; Pass < 2; ++Pass, Todo = Retry)
		for (const FRk& Rk : Todo)
		{
			int32 Pick = -1; float Bs = -1.0e9f;
			for (int32 n = 0; n < GNodes.Num(); ++n)
			{
				const FNode& N = GNodes[n];
				if (N.Level != Rk.Level || N.bHelix || N.Mouth >= 0 || N.Room >= 0 || N.Tun.Num() == 0) continue;
				bool bMouthNode = false;
				for (const int32 o : N.Links) bMouthNode |= GNodes[o].Mouth >= 0;
				if (bMouthNode && Rk.Want != 3) continue;
				bool bFar = true;
				for (const FRoom& O : GRooms) bFar &= FVector::Dist(O.C, N.P) > 5000.f;
				for (const FHelix& Hx : GHelix) bFar &= FVector2D::Distance(Hx.C, XY(N.P)) > 2600.f + FMath::Max(Rk.Rx, Rk.Ry);
				if (!bFar) continue;
				const float Yaw = Hash01(n, 9) * PI;
				FRoom R; R.Kind = Rk.K; R.C = N.P; R.Rx = Rk.Rx; R.Ry = Rk.Ry; R.Yaw = Yaw; R.Ht = Rk.Ht;
				bool bCover = H(XY(N.P)) - (N.P.Z + Rk.Ht) >= NeedCover;
				for (int32 a = 0; a < 8 && bCover; ++a) { const FVector P = RoomPoint(R, 1.1f * FMath::Cos(a * PI / 4.f), 1.1f * FMath::Sin(a * PI / 4.f)); bCover &= H(XY(P)) - (N.P.Z + Rk.Ht * 0.75f) >= NeedCover; }
				if (!bCover) continue;
				float S = Hash01(n, 11) * 100.f;
				const int32 Deg = N.Tun.Num();
				if (Rk.Want == 1) S += Deg == 1 ? 1000.f : 0.f;
				if (Rk.Want == 2) S += Deg >= 3 ? 1000.f : Deg == 2 ? 300.f : 0.f;
				if (Rk.Want == 3) { const FMouth* F = GMouths.FindByPredicate([](const FMouth& M) { return M.Kind == 3; }); if (F) S -= FVector::Dist(F->At, N.P) / 10.f; }
				if (S > Bs) { Bs = S; Pick = n; }
			}
			if (Pick < 0 && Rk.Level == 4) { FRk Lower = Rk; Lower.Level = 3; Retry.Add(Lower); UE_LOG(LogTemp, Log, TEXT("JEEBDEEP no place for the %s on level 4: tried on level 3"), RoomName(Rk.K)); continue; }
			if (Pick < 0) { UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP no place for the %s on level %d"), RoomName(Rk.K), Rk.Level); continue; }
			FRoom R; R.Kind = Rk.K; R.C = GNodes[Pick].P; R.Rx = Rk.Rx; R.Ry = Rk.Ry; R.Yaw = Hash01(Pick, 9) * PI; R.Ht = Rk.Ht; R.bLit = Rk.bLit; R.Level = Rk.Level; R.Node = Pick;
			if (Rk.K == ERoom::Lake) R.Basin = 110.f;
			GNodes[Pick].Room = GRooms.Add(R);
		}
		// ---- which passages are lit: broad patches (a slow noise over the plan), about half; the foot's and the road's mouths lit, the cliffs' dark
		for (FTunnel& T : GTun)
		{
			const FVector Mp = T.P[T.P.Num() / 2];
			const float N = FMath::PerlinNoise2D(FVector2D(Mp.X / 6500.f + 3.7f, Mp.Y / 6500.f - 1.3f + Mp.Z / 4000.f));
			T.bLit = N > -0.02f;
			if (T.Kind == 2) { const FMouth& M = GMouths[GNodes[T.A].Mouth >= 0 ? GNodes[T.A].Mouth : GNodes[T.B].Mouth]; T.bLit = M.Kind == 2 || M.Kind == 3; }
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP maze: %d points, %d passages (%d tree, %d loops tried, %d loops dropped for thin rock, %d unreached left out), %d spirals, %d rooms"),
			GNodes.Num(), GTun.Num(), TreeEdges.Num(), Loops.Num(), Dropped, Unreached, GHelix.Num(), GRooms.Num());
		return GTun.Num() > 0;
	}
}

const TCHAR* JeebDeep::RoomName(ERoom K)
{
	switch (K)
	{
	case ERoom::Crystal: return TEXT("Crystal");
	case ERoom::Lake: return TEXT("Lake");
	case ERoom::Collapsed: return TEXT("Collapsed");
	case ERoom::Mine: return TEXT("Mine");
	case ERoom::Mushroom: return TEXT("Mushroom");
	case ERoom::Shrine: return TEXT("Shrine");
	case ERoom::Lair: return TEXT("Lair");
	case ERoom::Library: return TEXT("Library");
	case ERoom::Great: return TEXT("Great");
	case ERoom::Crystal2: return TEXT("Crystal2");
	}
	return TEXT("?");
}

bool JeebDeep::On()
{
#if !UE_BUILD_SHIPPING
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("NoDeeps"));
	if (bOff) return false;
#endif
	return true;
}
bool JeebDeep::Ready() { return GOk; }
const TArray<FMouth>& JeebDeep::Mouths() { return GMouths; }
const TArray<FRoom>& JeebDeep::Rooms() { return GRooms; }
const TArray<FTunnel>& JeebDeep::Tunnels() { return GTun; }
const TArray<FSpot>& JeebDeep::Spots() { return GSpots; }
uint32 JeebDeep::Hash() { return GHash; }

void JeebDeep::Prepare(UWorld* World)
{
	if (GPrepared) return;
	GPrepared = true;
	if (!On() || !World || !World->GetMapName().Contains(TEXT("Field"))) { UE_LOG(LogTemp, Log, TEXT("JEEBDEEP none (switched off, or not the real map)")); return; }
	const TArray<FVector2D>& Road = JeebOpen::SilverRoadLine();
	if (Road.Num() < 100) { UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP no mountain road: no Deeps")); return; }
	const double T0 = FPlatformTime::Seconds();
	FCtx Cx; Cx.Mid = JeebOpen::SilverMid(); Cx.Road = &Road;
	for (const FVector2D& P : Road) if (FVector2D::Distance(P, Cx.Mid) < 7700.f) { const FVector2D D = P - Cx.Mid; Cx.PhiE = FMath::Atan2(D.Y, D.X); break; }
	Cx.PhiC = FMath::UnwindRadians(Cx.PhiE + PI);
	if (!Build(Cx)) { GMouths.Reset(); GTun.Reset(); GRooms.Reset(); return; }
	AddSolids();
	BuildGrid();
	// (how thin the rock gets over the passages, away from the mouths' frames)
	{
		int32 Thin = 0; float Worst = 1.0e9f; FVector WorstAt = FVector::ZeroVector;
		for (const FTunnel& T : GTun)
			for (int32 i = 0; i < T.P.Num(); ++i)
			{
				bool bNearMouth = false;
				for (const FMouth& M : GMouths) bNearMouth |= M.Box.ExpandBy(400.f).IsInside(T.P[i]);
				if (bNearMouth) continue;
				const FVector2D Tg = XY(T.P[FMath::Min(i + 1, T.P.Num() - 1)] - T.P[FMath::Max(i - 1, 0)]).GetSafeNormal();
				const float C = CoverAt(XY(T.P[i]), FVector2D(-Tg.Y, Tg.X), T.P[i].Z, T.Ht, T.W);
				if (C < 350.f) ++Thin;
				if (C < Worst) { Worst = C; WorstAt = T.P[i]; }
			}
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP rock over the roofs: least %.1f m at %.0f %.0f %.0f; %d points under 3.5 m"), Worst / 100.f, WorstAt.X, WorstAt.Y, WorstAt.Z, Thin);
	}
	PlaceThings();
	GHash = 0;
	for (const FNode& N : GNodes) GHash = FCrc::MemCrc32(&N.P, sizeof(FVector), GHash);
	for (const FTunnel& T : GTun) GHash = FCrc::MemCrc32(T.P.GetData(), T.P.Num() * sizeof(FVector), GHash);
	for (const FRoom& R : GRooms) GHash = FCrc::MemCrc32(&R.C, sizeof(FVector), GHash);
	for (const FSpot& S : GSpots) GHash = FCrc::MemCrc32(&S.At, sizeof(FVector), GHash);
	GOk = true;
	float Len = 0.f; for (const FTunnel& T : GTun) for (int32 i = 1; i < T.P.Num(); ++i) Len += FVector::Dist(T.P[i], T.P[i - 1]);
	UE_LOG(LogTemp, Log, TEXT("JEEBDEEP all: %d mouths, %d rooms, %d passages (%.0f m), %d spirals, box %.0f x %.0f x %.0f m, in %.0f ms; hash=%08x"), GMouths.Num(), GRooms.Num(), GTun.Num(), Len / 100.f, GHelix.Num(),
		GAll.GetSize().X / 100.f, GAll.GetSize().Y / 100.f, GAll.GetSize().Z / 100.f, (FPlatformTime::Seconds() - T0) * 1000.0, GHash);
	for (int32 r = 0; r < GRooms.Num(); ++r)
	{
		const FRoom& R = GRooms[r];
		const float Yd = FMath::RadiansToDegrees(R.Yaw);
		const FVector E = RoomPoint(R, -0.8f, 0.f) + FVector(0.f, 0.f, 260.f);
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP room %s (level %d, %s) at %.0f %.0f floor %.1f m, %.0f x %.0f x %.0f m; eye %.0f_%.0f_A%.0f_%.0f_-8_0"), RoomName(R.Kind), R.Level, R.bLit ? TEXT("lit") : TEXT("dark"), R.C.X, R.C.Y, R.C.Z / 100.f, 2.f * R.Rx / 100.f, 2.f * R.Ry / 100.f, R.Ht / 100.f, E.X, E.Y, E.Z, Yd);
	}
	for (const FMouth& M : GMouths)
	{
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(M.D.Y, M.D.X));
		const FVector2D O = XY(M.At) - M.D * 2200.f + M.Lat() * 600.f;
		const FVector2D I = XY(M.At) + M.D * (M.HoodLen + 200.f);
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP eyes %s: outside %.0f_%.0f_A%.0f_%.0f_-8_0 inside %.0f_%.0f_A%.0f_%.0f_-3_0 back %.0f_%.0f_A%.0f_%.0f_0_0"), *M.Name,
			O.X, O.Y, M.Kind == 1 ? M.At.Z + 300.f : FMath::Max(H(O), M.At.Z) + 400.f, FMath::RadiansToDegrees(FMath::Atan2((XY(M.At) - O).Y, (XY(M.At) - O).X)),
			I.X, I.Y, M.At.Z + 190.f, Yaw, I.X, I.Y, M.At.Z + 190.f, Yaw + 180.f);
	}
	// (eyes in a lit and a dark passage, for test pictures)
	int32 NL = 0, ND = 0;
	for (const FTunnel& T : GTun)
	{
		if (T.Kind != 0 || T.P.Num() < 8) continue;
		if ((T.bLit ? NL : ND) >= 3) continue;
		(T.bLit ? NL : ND)++;
		const FVector A = T.P[T.P.Num() / 2 - 2], B = T.P[T.P.Num() / 2 + 1];
		UE_LOG(LogTemp, Log, TEXT("JEEBDEEP eyes %s passage: %.0f_%.0f_A%.0f_%.0f_-4_0"), T.bLit ? TEXT("lit") : TEXT("dark"), A.X, A.Y, A.Z + 180.f, FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X)));
	}
}

float JeebDeep::Field(const FVector& P) { return GOk ? CaveField(P) : 1.0e9f; }

bool JeebDeep::CutsTri(const FVector& A, const FVector& B, const FVector& C)
{
	if (!GOk) return false;
	for (const FMouth& M : GMouths)
	{
		if (!M.Box.IsInsideXY(A) || !M.Box.IsInsideXY(B) || !M.Box.IsInsideXY(C)) continue;
		// (2026-11-28: cut a triangle with any corner in the passage's air (it would poke into the passage) or all three inside the
		// frame; one only partly inside the frame's solid rock is hidden by it and kept, or the cut showed the shelf under the ground)
		int32 InHood = 0; bool bAir = false, bFront = false;
		for (const FVector& V : { A, B, C })
		{
			const float Al = FVector2D::DotProduct(XY(V) - XY(M.At), M.D);
			if (Al < -M.Lip - 150.f) { bFront = true; break; }
			const float Cf = CaveField(V);
			if (Cf < 0.f) bAir = true;
			if (Cf < 0.f || HoodIn(V) > 0.f) ++InHood;
		}
		if (!bFront && (bAir || InHood == 3)) return true;
	}
	return false;
}

bool JeebDeep::Covered(float X, float Y)
{
	for (const FMouth& M : GMouths)
	{
		const FVector2D Q = FVector2D(X, Y) - XY(M.At);
		const float A = FVector2D::DotProduct(Q, M.D), L = FVector2D::DotProduct(Q, M.Lat());
		if (A > -M.Lip - 200.f && A < M.HoodLen + 200.f && FMath::Abs(L) < 650.f) return true;
	}
	return false;
}

float JeebDeep::InsideAt(const FVector& P)
{
	if (!GOk || !GAll.IsInside(P)) return 0.f;
	if (CaveField(P) > 120.f) return 0.f;
	float Near = 1.0e9f;
	for (const FMouth& M : GMouths) Near = FMath::Min(Near, FVector::Dist(P, M.At + FVector(0.f, 0.f, 200.f)));
	return Sm(250.f, 1100.f, Near);
}

bool JeebDeep::Route(int32 FromMouth, int32 ToMouth, TArray<FVector>& Out)
{
	Out.Reset();
	if (!GOk || !GMouths.IsValidIndex(FromMouth) || !GMouths.IsValidIndex(ToMouth)) return false;
	const int32 S = GMouths[FromMouth].Node, E = GMouths[ToMouth].Node;
	TArray<int32> Prev; Prev.Init(-1, GNodes.Num());
	TArray<int32> PrevT; PrevT.Init(-1, GNodes.Num());
	TArray<int32> Q; Q.Add(S); Prev[S] = S;
	for (int32 h = 0; h < Q.Num(); ++h)
	{
		const int32 C = Q[h];
		if (C == E) break;
		for (const int32 t : GNodes[C].Tun)
		{
			const int32 O = GTun[t].A == C ? GTun[t].B : GTun[t].A;
			if (Prev[O] >= 0) continue;
			Prev[O] = C; PrevT[O] = t; Q.Add(O);
		}
	}
	if (Prev[E] < 0) return false;
	TArray<int32> Chain;
	for (int32 C = E; C != S; C = Prev[C]) Chain.Insert(C, 0);
	Out.Add(GMouths[FromMouth].Outside);
	int32 Cur = S;
	for (const int32 N : Chain)
	{
		const FTunnel& T = GTun[PrevT[N]];
		if (T.A == Cur) for (int32 i = 0; i < T.P.Num(); ++i) Out.Add(T.P[i]);
		else for (int32 i = T.P.Num() - 1; i >= 0; --i) Out.Add(T.P[i]);
		Cur = N;
	}
	Out.Add(GMouths[ToMouth].Outside);
	return true;
}

// ------------------------------------------------------------------------------------------------ the actor
ADeepCaves::ADeepCaves()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADeepCaves::BeginPlay()
{
	Super::BeginPlay();
	if (!JeebDeep::Ready()) { SetActorTickEnabled(false); return; }
	const bool bServer = IsNetMode(NM_DedicatedServer);
	BuildRock(bServer);
	BuildThings(bServer);
	if (bServer) SetActorTickEnabled(false);
}

void ADeepCaves::BuildRock(bool bServer)
{
	const double T0 = FPlatformTime::Seconds();
	// the chunks any piece of the caves reaches
	TSet<FIntVector> Want;
	const float CS = VS * CN;
	auto AddBox = [&](const FBox& B)
	{
		const FIntVector A(FMath::FloorToInt(B.Min.X / CS), FMath::FloorToInt(B.Min.Y / CS), FMath::FloorToInt(B.Min.Z / CS));
		const FIntVector Z(FMath::FloorToInt(B.Max.X / CS), FMath::FloorToInt(B.Max.Y / CS), FMath::FloorToInt(B.Max.Z / CS));
		for (int32 x = A.X; x <= Z.X; ++x) for (int32 y = A.Y; y <= Z.Y; ++y) for (int32 z = A.Z; z <= Z.Z; ++z) Want.Add(FIntVector(x, y, z));
	};
	for (const FSeg& S : GSegs) { FBox B(ForceInit); B += S.A; B += S.B; B.Min -= FVector(S.W + 200.f, S.W + 200.f, 200.f); B.Max += FVector(S.W + 200.f, S.W + 200.f, S.Ht + 200.f); AddBox(B); }
	for (const FRoom& R : GRooms) AddBox(R.Box);
	for (const FMouth& M : GMouths) AddBox(M.Box);
	TArray<FIntVector> Chunks = Want.Array();
	Chunks.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z; });
	struct FOut { TArray<FVector> V, N; TArray<FLinearColor> C; TArray<FVector2D> U1, U2; TArray<int32> T; bool bHood = false; };
	TArray<FOut> Outs; Outs.SetNum(Chunks.Num());
	ParallelFor(Chunks.Num(), [&](int32 ci)
	{
		const FIntVector Ck = Chunks[ci];
		constexpr int32 NP = CN + 3;   // grid points -1 .. CN+1
		const FIntVector I0(Ck.X * CN - 1, Ck.Y * CN - 1, Ck.Z * CN - 1);
		TArray<float> Gz; Gz.SetNumUninitialized(NP * NP);
		for (int32 a = 0; a < NP; ++a) for (int32 b = 0; b < NP; ++b) Gz[a * NP + b] = H((I0.X + a) * VS, (I0.Y + b) * VS);
		TArray<float> F; F.SetNumUninitialized(NP * NP * NP);
		bool bAir = false, bRock = false;
		auto Ix = [](int32 a, int32 b, int32 c) { return (a * NP + b) * NP + c; };
		for (int32 a = 0; a < NP; ++a) for (int32 b = 0; b < NP; ++b) for (int32 c = 0; c < NP; ++c)
		{
			const float V = Full(FVector((I0.X + a) * VS, (I0.Y + b) * VS, (I0.Z + c) * VS), Gz[a * NP + b]);
			F[Ix(a, b, c)] = V; (V < 0.f ? bAir : bRock) = true;
		}
		if (!bAir || !bRock) return;
		FOut& O = Outs[ci];
		constexpr int32 NCl = NP - 1;
		TArray<int32> CellV; CellV.Init(-1, NCl * NCl * NCl);
		auto Cx = [](int32 a, int32 b, int32 c) { return (a * NCl + b) * NCl + c; };
		static const int32 E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (int32 a = 0; a < NCl; ++a) for (int32 b = 0; b < NCl; ++b) for (int32 c = 0; c < NCl; ++c)
		{
			float Cv[8]; int32 Mask = 0;
			for (int32 k = 0; k < 8; ++k) { Cv[k] = F[Ix(a + (k & 1), b + ((k >> 1) & 1), c + ((k >> 2) & 1))]; if (Cv[k] < 0.f) Mask |= 1 << k; }
			if (Mask == 0 || Mask == 255) continue;
			FVector Acc = FVector::ZeroVector; int32 Nn = 0;
			for (int32 e = 0; e < 12; ++e)
			{
				const int32 p = E[e][0], q = E[e][1];
				if ((Cv[p] < 0.f) == (Cv[q] < 0.f)) continue;
				const float T = Cv[p] / (Cv[p] - Cv[q]);
				const FVector Pp(p & 1, (p >> 1) & 1, (p >> 2) & 1), Pq(q & 1, (q >> 1) & 1, (q >> 2) & 1);
				Acc += FMath::Lerp(Pp, Pq, T); ++Nn;
			}
			const FVector Lp = Acc / Nn;
			const FVector W((I0.X + a + Lp.X) * VS, (I0.Y + b + Lp.Y) * VS, (I0.Z + c + Lp.Z) * VS);
			// (the normal: the field's slope over the cell, out of the rock)
			FVector G(
				(Cv[1] - Cv[0]) + (Cv[3] - Cv[2]) + (Cv[5] - Cv[4]) + (Cv[7] - Cv[6]),
				(Cv[2] - Cv[0]) + (Cv[3] - Cv[1]) + (Cv[6] - Cv[4]) + (Cv[7] - Cv[5]),
				(Cv[4] - Cv[0]) + (Cv[5] - Cv[1]) + (Cv[6] - Cv[2]) + (Cv[7] - Cv[3]));
			const FVector N = (-G).GetSafeNormal();
			CellV[Cx(a, b, c)] = O.V.Add(W);
			O.N.Add(N.IsNearlyZero() ? FVector::UpVector : N);
		}
		// faces: one per crossed grid edge owned by this chunk (its low end inside the chunk), between the four cells round it
		TArray<int32> Tri;
		for (int32 a = 1; a <= CN; ++a) for (int32 b = 1; b <= CN; ++b) for (int32 c = 1; c <= CN; ++c)
		{
			const float F0 = F[Ix(a, b, c)];
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 a1 = a + (Axis == 0), b1 = b + (Axis == 1), c1 = c + (Axis == 2);
				const float F1 = F[Ix(a1, b1, c1)];
				if ((F0 < 0.f) == (F1 < 0.f)) continue;
				int32 Q[4];
				if (Axis == 0) { Q[0] = CellV[Cx(a, b - 1, c - 1)]; Q[1] = CellV[Cx(a, b, c - 1)]; Q[2] = CellV[Cx(a, b, c)]; Q[3] = CellV[Cx(a, b - 1, c)]; }
				else if (Axis == 1) { Q[0] = CellV[Cx(a - 1, b, c - 1)]; Q[1] = CellV[Cx(a - 1, b, c)]; Q[2] = CellV[Cx(a, b, c)]; Q[3] = CellV[Cx(a, b, c - 1)]; }
				else { Q[0] = CellV[Cx(a - 1, b - 1, c)]; Q[1] = CellV[Cx(a, b - 1, c)]; Q[2] = CellV[Cx(a, b, c)]; Q[3] = CellV[Cx(a - 1, b, c)]; }
				if (Q[0] < 0 || Q[1] < 0 || Q[2] < 0 || Q[3] < 0) continue;
				// (wound with the cross product into the rock, as the ground's)
				FVector Rock = FVector::ZeroVector; Rock[Axis] = F0 >= 0.f ? -1.f : 1.f;
				const FVector Cr = FVector::CrossProduct(O.V[Q[1]] - O.V[Q[0]], O.V[Q[2]] - O.V[Q[0]]);
				if (FVector::DotProduct(Cr, Rock) < 0.f) { Swap(Q[1], Q[3]); }
				if (FVector::DistSquared(O.V[Q[0]], O.V[Q[2]]) <= FVector::DistSquared(O.V[Q[1]], O.V[Q[3]])) Tri.Append({ Q[0], Q[1], Q[2], Q[0], Q[2], Q[3] });
				else Tri.Append({ Q[1], Q[2], Q[3], Q[1], Q[3], Q[0] });
			}
		}
		// what each point is: the caves' wall, a frame's outside, or the hidden ground (left out)
		TArray<uint8> Kind; Kind.SetNumZeroed(O.V.Num());
		O.C.SetNum(O.V.Num()); O.U1.SetNumZeroed(O.V.Num()); O.U2.SetNumZeroed(O.V.Num());
		for (int32 v = 0; v < O.V.Num(); ++v)
		{
			const FVector P = O.V[v];
			const float Cf = CaveField(P), Hd = HoodIn(P);
			const bool bCave = Cf < 400.f;   // (wide: the wall noise put some surface vertices past 150 and single triangles went missing)
			const bool bHood = !bCave && Hd > -400.f;   // (wide: the shelf where the frame meets the ground under the grass stays whole)
			Kind[v] = bCave ? 1 : bHood ? 2 : 0;
			if (bHood) O.bHood = true;
			float Near = 1.0e9f;
			for (const FMouth& M : GMouths) Near = FMath::Min(Near, FVector::Dist(P, M.At + FVector(0.f, 0.f, 200.f)));
			O.C[v] = (bCave && Cf > -150.f && O.N[v].Z > 0.72f) ? FLinearColor(0.55f, 0.75f, 0.f, 0.f) : FLinearColor(0.f, 1.f, 0.f, 0.f);
			O.U2[v] = FVector2D(bCave ? Sm(250.f, 900.f, Near) : 0.f, 0.f);
			if (!bServer && bHood) O.U1[v] = FVector2D(JeebOpen::SnowRaw(P.X, P.Y, P.Z, O.N[v].Z) + 1.f, 0.f);
		}
		for (int32 t = 0; t + 2 < Tri.Num(); t += 3) if (Kind[Tri[t]] || Kind[Tri[t + 1]] || Kind[Tri[t + 2]]) O.T.Append({ Tri[t], Tri[t + 1], Tri[t + 2] });
	});
	// one mesh per 48 m block
	TMap<FIntVector, TArray<int32>> Blocks;
	for (int32 ci = 0; ci < Chunks.Num(); ++ci) if (Outs[ci].T.Num()) Blocks.FindOrAdd(FIntVector(FMath::FloorToInt(Chunks[ci].X / 4.f), FMath::FloorToInt(Chunks[ci].Y / 4.f), FMath::FloorToInt(Chunks[ci].Z / 4.f))).Add(ci);
	TArray<FIntVector> Keys; Blocks.GetKeys(Keys);
	Keys.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z; });
	UMaterialInterface* Mat = bServer ? nullptr : AWorldTerrain::SharedGround(GetWorld());
	uint32 MeshHash = 0; int32 AllTris = 0;
	for (const FIntVector& K : Keys)
	{
		TArray<FVector> V, N; TArray<FLinearColor> C; TArray<FVector2D> U0, U1, U2; TArray<int32> T;
		bool bHood = false;
		for (const int32 ci : Blocks[K])
		{
			const FOut& O = Outs[ci];
			const int32 Base = V.Num();
			V.Append(O.V); N.Append(O.N); C.Append(O.C); U1.Append(O.U1); U2.Append(O.U2);
			for (const int32 t : O.T) T.Add(Base + t);
			bHood |= O.bHood;
			MeshHash = FCrc::MemCrc32(O.V.GetData(), O.V.Num() * sizeof(FVector), MeshHash);
		}
		U0.SetNum(V.Num());
		for (int32 i = 0; i < V.Num(); ++i) U0[i] = FVector2D(V[i].X / 1000.f, V[i].Y / 1000.f);
		UProceduralMeshComponent* PM = NewObject<UProceduralMeshComponent>(this);
		PM->SetupAttachment(RootComponent);
		PM->bUseAsyncCooking = true;
		PM->RegisterComponent();
		PM->CreateMeshSection_LinearColor(0, V, T, N, U0, U1, U2, TArray<FVector2D>(), C, TArray<FProcMeshTangent>(), true);
		PM->SetCollisionProfileName(TEXT("BlockAll"));
		PM->ComponentTags.Add(FName(TEXT("JeebCave")));
		PM->SetLightingChannels(true, true, false);   // (channel 1: the torches and glows light only the rock, the things in it and people)
		// (the sun gets in only at the mouths: the ground over the rest shadows it, so the rock deeper in casts none, which saves the sun's
		// shadow maps drawing 800 thousand hidden triangles)
		bool bNearMouth = false;
		for (const FMouth& M : GMouths) bNearMouth |= PM->Bounds.GetBox().Intersect(M.Box.ExpandBy(3000.f));
		PM->SetCastShadow(bNearMouth);
		PM->bCastShadowAsTwoSided = true;
		if (!bServer)
		{
			if (Mat) PM->SetMaterial(0, Mat);
			if (!bHood) { PM->SetCachedMaxDrawDistance(14000.f); PM->LDMaxDrawDistance = 14000.f; }
		}
		else PM->SetVisibility(false);
		Rock.Add(PM);
		RockCentres.Add(PM->Bounds.Origin);
		AllTris += T.Num() / 3;
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBDEEP mesh: %d chunks of %d, %d meshes, %d triangles, in %.0f ms; hash=%08x"), Outs.FilterByPredicate([](const FOut& O) { return O.T.Num() > 0; }).Num(), Chunks.Num(), Rock.Num(), AllTris, (FPlatformTime::Seconds() - T0) * 1000.0, MeshHash);
}

void ADeepCaves::BuildThings(bool bServer)
{
	// the chests (server and players alike: HouseLoot names them by their spot)
	UStaticMesh* ChestMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Interior/Chest/SM_Int_Chest.SM_Int_Chest"));
	TArray<FProp> Props = GProps;
	for (int32 i = 0; i < GChests.Num(); ++i)
	{
		const FChest& Ch = GChests[i];
		JeebHouseLoot::AddBox(this, TEXT("Deeps"), i, JeebHouseLoot::EBoxKind::Chest, Ch.Xf, Ch.RoomMid, Ch.Xf.GetLocation().Z);
		FProp P; P.Mesh = TEXT("/Game/Interior/Chest/SM_Int_Chest.SM_Int_Chest"); P.Xf = Ch.Xf; P.bCollide = true; Props.Add(P);
	}
	(void)ChestMesh;
	// the props, one instanced set per mesh and look
	UMaterialInterface* ColorBase = bServer ? nullptr : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
	UMaterialInterface* GlowBase = bServer ? nullptr : LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_CaveCrystal.M_CaveCrystal"));
	TMap<FString, UInstancedStaticMeshComponent*> ByKey;
	int32 Missing = 0;
	auto SetFor = [&](const FString& MeshPath, const FString& Mat, const FLinearColor& Col, float Glow, bool bCollide) -> UInstancedStaticMeshComponent*
	{
		const FString K = FString::Printf(TEXT("%s|%s|%s|%.2f|%d"), *MeshPath, *Mat, *Col.ToString(), Glow, bCollide ? 1 : 0);
		if (UInstancedStaticMeshComponent** F = ByKey.Find(K)) return *F;
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!M) { ++Missing; UE_LOG(LogTemp, Warning, TEXT("JEEBDEEP missing mesh %s"), *MeshPath); ByKey.Add(K, nullptr); return nullptr; }
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
		I->SetupAttachment(RootComponent);
		I->SetStaticMesh(M);
		I->SetMobility(EComponentMobility::Static);
		I->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bCollide) I->SetCollisionProfileName(TEXT("BlockAll"));
		I->SetCastShadow(false);
		I->SetLightingChannels(true, true, false);
		I->SetCachedMaxDrawDistance(7000.f); I->LDMaxDrawDistance = 7000.f;
		if (!bServer)
		{
			if (Mat == TEXT("color") && ColorBase) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(ColorBase, this); D->SetVectorParameterValue(TEXT("Color"), Col); I->SetMaterial(0, D); }
			else if (Mat == TEXT("glow") && GlowBase) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(GlowBase, this); D->SetVectorParameterValue(TEXT("Tint"), Col); D->SetVectorParameterValue(TEXT("Color"), Col); D->SetScalarParameterValue(TEXT("Glow"), Glow); I->SetMaterial(0, D); }
			else if (Mat.StartsWith(TEXT("/Game"))) { if (UMaterialInterface* Mi = LoadObject<UMaterialInterface>(nullptr, *Mat)) for (int32 s = 0; s < FMath::Max(1, M->GetStaticMaterials().Num()); ++s) I->SetMaterial(s, Mi); }
		}
		else I->SetVisibility(false);
		I->RegisterComponent();
		Sets.Add(I);
		ByKey.Add(K, I);
		return I;
	};
	for (const FProp& P : Props)
	{
		if (P.Mesh == TEXT("water"))
		{
			if (bServer) continue;
			// (a still pool: a flat disc of the world's water)
			UProceduralMeshComponent* Wm = NewObject<UProceduralMeshComponent>(this);
			Wm->SetupAttachment(RootComponent);
			Wm->RegisterComponent();
			TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> U;
			V.Add(P.Xf.GetLocation()); N.Add(FVector::UpVector); U.Add(FVector2D(V[0].X / 1000.f, V[0].Y / 1000.f));
			constexpr int32 NR = 40;
			for (int32 k = 0; k < NR; ++k) { const FVector Q = P.Xf.TransformPosition(FVector(FMath::Cos(2.f * PI * k / NR), FMath::Sin(2.f * PI * k / NR), 0.f)); V.Add(Q); N.Add(FVector::UpVector); U.Add(FVector2D(Q.X / 1000.f, Q.Y / 1000.f)); }
			for (int32 k = 0; k < NR; ++k) T.Append({ 0, 1 + (k + 1) % NR, 1 + k });
			Wm->CreateMeshSection(0, V, T, N, U, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
			Wm->SetCastShadow(false);
			Wm->SetLightingChannels(true, true, false);
			if (UMaterialInterface* Wmat = JeebWaterLook::Material()) Wm->SetMaterial(0, Wmat);
			continue;
		}
		if (P.Mat == TEXT("color") || P.Mat == TEXT("glow"))
		{
			// (plain parts: M_Color and M_CaveCrystal are not made for instanced meshes)
			if (bServer && !P.bCollide) continue;
			UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *P.Mesh);
			if (!M) { ++Missing; continue; }
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetupAttachment(RootComponent);
			C->SetStaticMesh(M);
			C->SetWorldTransform(P.Xf);
			C->SetCollisionEnabled(P.bCollide ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			if (P.bCollide) C->SetCollisionProfileName(TEXT("BlockAll"));
			C->SetCastShadow(false);
			C->SetLightingChannels(true, true, false);
			C->SetCachedMaxDrawDistance(7000.f); C->LDMaxDrawDistance = 7000.f;
			if (!bServer)
			{
				UMaterialInterface* Base = P.Mat == TEXT("color") ? ColorBase : GlowBase;
				if (Base) { UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Base, this); D->SetVectorParameterValue(TEXT("Color"), P.Col); D->SetVectorParameterValue(TEXT("Tint"), P.Col); D->SetScalarParameterValue(TEXT("Glow"), P.Glow); C->SetMaterial(0, D); }
			}
			else C->SetVisibility(false);
			C->RegisterComponent();
			continue;
		}
		if (UInstancedStaticMeshComponent* I = SetFor(P.Mesh, P.Mat, P.Col, P.Glow, P.bCollide)) I->AddInstance(P.Xf, true);
	}
	// the wall torches and their flames (no collision, high on the wall)
	if (!bServer)
	{
		UInstancedStaticMeshComponent* Tm = SetFor(TEXT("/Game/TownDecor/SM_TorchWall.SM_TorchWall"), TEXT(""), FLinearColor::White, 0.f, false);
		UInstancedStaticMeshComponent* Fm = SetFor(TEXT("/Game/TownDecor/SM_TorchWallFlame.SM_TorchWallFlame"), TEXT(""), FLinearColor::White, 0.f, false);
		for (const FTransform& Xf : GTorches) { if (Tm) Tm->AddInstance(Xf, true); if (Fm) Fm->AddInstance(Xf, true); }
		if (Fm) { Fm->SetCachedMaxDrawDistance(9000.f); Fm->LDMaxDrawDistance = 9000.f; }
		// the crystals: six-sided prisms with a pointed tip (as the old caves')
		TMap<FString, int32> ByCol;
		TArray<TArray<FVector>> Vs, Ns; TArray<TArray<int32>> Ts; TArray<FLinearColor> Cols;
		FRandomStream R(919);
		for (const FCrystal& Cr : GCrystals)
		{
			const FString K = Cr.Col.ToString();
			int32* Gi = ByCol.Find(K);
			if (!Gi) { Gi = &ByCol.Add(K, Vs.Num()); Vs.AddDefaulted(); Ns.AddDefaulted(); Ts.AddDefaulted(); Cols.Add(Cr.Col); }
			TArray<FVector>& V = Vs[*Gi]; TArray<FVector>& N = Ns[*Gi]; TArray<int32>& T = Ts[*Gi];
			const FVector Up = Cr.Up, Ax = FVector::CrossProduct(Up, FMath::Abs(Up.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal(), Ay = FVector::CrossProduct(Up, Ax);
			const float Rad = 14.f * Cr.Size * R.FRandRange(0.8f, 1.2f), Ln = 70.f * Cr.Size * R.FRandRange(0.8f, 1.3f), Tip = Ln * R.FRandRange(0.28f, 0.4f);
			const FVector Foot = Cr.At - Up * 12.f * Cr.Size, TopP = Foot + Up * Ln, Point = TopP + Up * Tip;
			auto Face = [&](const FVector& A, const FVector& B, const FVector& C)
			{
				const FVector Nn = -FVector::CrossProduct(B - A, C - A).GetSafeNormal();
				const int32 I = V.Num();
				V.Append({ A, B, C }); N.Append({ Nn, Nn, Nn }); T.Append({ I, I + 1, I + 2 });
			};
			const float Tw = R.FRandRange(0.f, PI);
			for (int32 k = 0; k < 6; ++k)
			{
				const float A0 = Tw + 2.f * PI * k / 6.f, A1 = Tw + 2.f * PI * (k + 1) / 6.f;
				const FVector O0 = (Ax * FMath::Cos(A0) + Ay * FMath::Sin(A0)) * Rad, O1 = (Ax * FMath::Cos(A1) + Ay * FMath::Sin(A1)) * Rad;
				Face(Foot + O0, TopP + O0, TopP + O1); Face(Foot + O0, TopP + O1, Foot + O1); Face(TopP + O0, Point, TopP + O1);
			}
		}
		for (int32 g = 0; g < Vs.Num(); ++g)
		{
			UProceduralMeshComponent* G = NewObject<UProceduralMeshComponent>(this);
			G->SetupAttachment(RootComponent);
			G->RegisterComponent();
			TArray<FLinearColor> Cc; Cc.Init(FLinearColor(0.85f, 0.f, 0.f, 1.f), Vs[g].Num());
			G->CreateMeshSection_LinearColor(0, Vs[g], Ts[g], Ns[g], TArray<FVector2D>(), Cc, TArray<FProcMeshTangent>(), false);
			G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			G->SetCastShadow(false);
			G->SetLightingChannels(true, true, false);
			if (GlowBase) { UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(GlowBase, this); M->SetVectorParameterValue(TEXT("Tint"), Cols[g]); M->SetVectorParameterValue(TEXT("Color"), Cols[g]); M->SetScalarParameterValue(TEXT("Glow"), 2.5f); G->SetMaterial(0, M); }
		}
		// the pool of lights
		Srcs = GLights;
		const int32 NP = FMath::Clamp(CVarDeepLights.GetValueOnGameThread(), 0, 64);
		for (int32 i = 0; i < NP; ++i)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetIntensityUnits(ELightUnits::Lumens);
			L->SetCastShadows(false);
			L->SetLightingChannels(false, true, false);
			L->SetSourceRadius(15.f);
			L->SetVisibility(false);
			L->RegisterComponent();
			Pool.Add(L); PoolSrc.Add(-1);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("JEEBDEEP things built: %d sets, %d chests, %d torches, %d light spots, pool of %d (%d meshes missing)"), Sets.Num(), GChests.Num(), GTorches.Num(), Srcs.Num(), Pool.Num(), Missing);
}

void ADeepCaves::Tick(float Dt)
{
	Super::Tick(Dt);
	Clock += Dt;
	const APlayerController* Pc = GetWorld()->GetFirstPlayerController();   // (this machine's own view: looks only)
	if (!Pc || !Pc->PlayerCameraManager || Pool.Num() == 0) return;
	const FVector Cam = Pc->PlayerCameraManager->GetCameraLocation();
	const float Near = CVarDeepNear.GetValueOnGameThread();
	PickIn -= Dt;
	if (PickIn <= 0.f)
	{
		PickIn = 0.2f;
		// the nearest sources get a light; one that keeps its light keeps the same one (no blinking)
		TArray<TPair<float, int32>> Close;
		if (GAll.ExpandBy(Near).IsInside(Cam))
			for (int32 s = 0; s < Srcs.Num(); ++s) { const float D = FVector::DistSquared(Srcs[s].At, Cam); if (D < Near * Near) Close.Add({ D, s }); }
		Close.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		if (Close.Num() > Pool.Num()) Close.SetNum(Pool.Num());
		TSet<int32> Want; for (const TPair<float, int32>& C : Close) Want.Add(C.Value);
		for (int32 i = 0; i < Pool.Num(); ++i) if (PoolSrc[i] >= 0 && !Want.Contains(PoolSrc[i])) { PoolSrc[i] = -1; Pool[i]->SetVisibility(false); }
		for (const TPair<float, int32>& C : Close)
		{
			if (PoolSrc.Contains(C.Value)) continue;
			const int32 Free = PoolSrc.Find(-1);
			if (Free == INDEX_NONE) break;
			PoolSrc[Free] = C.Value;
			UPointLightComponent* L = Pool[Free];
			const FLightSrc& S = Srcs[C.Value];
			L->SetWorldLocation(S.At);
			L->SetLightColor(S.Col);
			L->SetAttenuationRadius(CVarDeepTorchReach.GetValueOnGameThread() * S.Radius);
			L->SetVisibility(true);
		}
	}
	const float Lm = CVarDeepTorch.GetValueOnGameThread();
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		if (PoolSrc[i] < 0) continue;
		const FLightSrc& S = Srcs[PoolSrc[i]];
		const float Ph = PoolSrc[i] * 1.7f;
		const float Fl = S.bFlicker ? 1.f + 0.16f * (FMath::Sin(Clock * 9.3f + Ph) + 0.6f * FMath::Sin(Clock * 15.1f + Ph * 2.3f) + 0.3f * FMath::Sin(Clock * 23.7f + Ph * 0.7f)) / 1.9f : 1.f + 0.05f * FMath::Sin(Clock * 0.9f + Ph);
		const float Fade = 1.f - Sm(Near * 0.7f, Near, FVector::Dist(S.At, Cam));
		Pool[i]->SetIntensity(Lm * S.Lumens * Fl * Fade);
	}
}
