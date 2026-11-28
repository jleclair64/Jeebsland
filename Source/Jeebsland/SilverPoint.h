#pragma once

#include "CoreMinimal.h"
#include "Terrain.h"

// Silver Point, the wizard village (2026-11-26, agent wizvillage; Docs/SilverPointPlan.md). The buildings are Meshy models made in the
// style of the community "Enchanted Crooked Cottage" (/Game/TownLab/Silver, Tools/BuildSilverPoint.ps1). Their plots are worked out here
// on every machine from the site's middle (JeebOpen::Sites) and the way the road comes in, so every machine has the same town:
//  - a ring of shops and halls round the plaza (doors toward the middle), the academy tower behind it on the far side from the road;
//  - a short high street out along the road, shops either side facing across it;
//  - houses on the outer ring.
// AOpenWorld::BuildHouses draws them and their collision; the townsfolk (NpcLife.cpp, Town "Silver Point") use the doors.
namespace JeebSilver
{
	struct FPlot
	{
		const TCHAR* Id = nullptr;                    // "Bank", "Home3" ... (Townsfolk.csv Place and Errands use these)
		const TCHAR* Model = nullptr;                 // JeebOakHill::Models() row, also the mesh /Game/TownLab/Silver/SM_TL_SP<Kind>
		const TCHAR* Kind = nullptr;                  // "Home", "Bank" ...
		FVector2D Centre = FVector2D::ZeroVector;     // the model's origin (game cm)
		float Yaw = 0.f;                              // the actor's yaw
		float DoorYaw = 0.f;                          // the way its door faces
		FVector2D WallLo = FVector2D::ZeroVector, WallHi = FVector2D::ZeroVector;   // the walls' box in the model's own frame (cm, scaled)
		float Reach = 0.f;
	};

	inline FVector2D Dir(float Deg) { const float R = FMath::DegreesToRadians(Deg); return FVector2D(FMath::Cos(R), FMath::Sin(R)); }

	inline const JeebOpen::FSite* Site()
	{
		for (const JeebOpen::FSite& S : JeebOpen::Sites()) if (FCString::Strcmp(S.Name, TEXT("Silver Point")) == 0) return &S;
		return nullptr;
	}

	// The way the road comes in: of 72 headings 45 m out, the one nearest a road
	inline float RoadHeading(const FVector2D& Mid)
	{
		float Best = 1.0e9f, H = 0.f;
		for (int32 a = 0; a < 72; ++a)
		{
			const FVector2D Q = Mid + Dir(a * 5.f) * 4500.f;
			const float C = JeebTerrain::PathClearance(Q.X, Q.Y);
			if (C < Best - 1.f) { Best = C; H = a * 5.f; }
		}
		return H;
	}

	inline const TArray<FPlot>& Plots()
	{
		static const TArray<FPlot> All = []()
		{
			TArray<FPlot> A;
			const JeebOpen::FSite* S = Site();
			if (!S) return A;
			const FVector2D Mid = S->Pos;
			const float H = RoadHeading(Mid);
			auto Add = [&](const TCHAR* Id, const TCHAR* Kind, const FVector2D& C, float DoorYaw)
			{
				const JeebOakHill::FModel* M = JeebOakHill::FindModel(*FString::Printf(TEXT("Silver%s"), Kind));
				if (!M) return;
				FPlot P; P.Id = Id; P.Model = M->Name; P.Kind = Kind; P.Centre = C; P.DoorYaw = FRotator::NormalizeAxis(DoorYaw);
				P.Yaw = FRotator::NormalizeAxis(DoorYaw - M->DoorYaw);
				// (Unreal mirrors Blender's Y: the walls run from (WX0, -WY1) to (WX1, -WY0))
				P.WallLo = FVector2D(M->WX0, -M->WY1) * M->Scale; P.WallHi = FVector2D(M->WX1, -M->WY0) * M->Scale;
				P.Reach = FMath::Max(P.WallLo.Size(), P.WallHi.Size()) + FMath::Max(FVector2D(P.WallLo.X, P.WallHi.Y).Size(), FVector2D(P.WallHi.X, P.WallLo.Y).Size());
				A.Add(P);
			};
			// (2026-11-27, agent silverpt; owner: narrower, winding lanes) every plot by the middle of its front wall in the plan frame (m: U out
			// along the road, V to its right) and the way its door faces (degrees from the road's heading); laid out with Saved/sp/layout.js-style
			// checks: 1.5 m or more between any two buildings, every door on the plaza or a lane (Lanes below)
			auto At = [&](const TCHAR* Id, const TCHAR* Kind, float U, float V, float DoorDeg)
			{
				const JeebOakHill::FModel* M = JeebOakHill::FindModel(*FString::Printf(TEXT("Silver%s"), Kind));
				if (!M) return;
				const float DoorYaw = H + DoorDeg;
				const FVector2D Front = Mid + Dir(H) * (U * 100.f) + Dir(H + 90.f) * (V * 100.f);
				const FVector2D Ax = Dir(DoorYaw - 90.f), Ay = Dir(DoorYaw);   // (the model's own X and Y once turned)
				const float MidX = (M->WX0 + M->WX1) * 0.5f * M->Scale, FrontY = -M->WY0 * M->Scale;
				Add(Id, Kind, Front - Ax * MidX - Ay * FrontY, DoorYaw);
			};
			At(TEXT("PotionShop"), TEXT("PotionShop"), 11.06f, 7.74f, -145.0f);
			At(TEXT("Tavern"), TEXT("Tavern"), 0.47f, 13.49f, -92.0f);
			At(TEXT("Bank"), TEXT("Bank"), -8.12f, 10.78f, -53.0f);
			At(TEXT("TownHall"), TEXT("TownHall"), -13.50f, 0.00f, 0.0f);
			At(TEXT("Smith"), TEXT("Smith"), -7.74f, -11.06f, 55.0f);
			At(TEXT("CrystalShop"), TEXT("CrystalShop"), 3.49f, -13.04f, 105.0f);
			At(TEXT("WandShop"), TEXT("WandShop"), 10.34f, -8.68f, 140.0f);
			At(TEXT("Store"), TEXT("Store"), 31.84f, 3.04f, -94.9f);
			At(TEXT("BroomShop"), TEXT("BroomShop"), 28.38f, -2.89f, 85.1f);
			At(TEXT("PetShop"), TEXT("PetShop"), 46.13f, 2.87f, -87.4f);
			At(TEXT("Home2"), TEXT("Home"), 41.51f, -3.54f, 92.6f);
			At(TEXT("Home3"), TEXT("Home"), 57.41f, 3.39f, -87.4f);
			At(TEXT("Home4"), TEXT("Home"), 52.29f, -3.05f, 92.6f);
			At(TEXT("Home5"), TEXT("Home"), 12.30f, 22.89f, -172.9f);
			At(TEXT("Home6"), TEXT("Home"), 4.21f, 33.11f, 26.6f);
			At(TEXT("Academy"), TEXT("Academy"), -30.00f, 8.00f, 0.0f);
			At(TEXT("Home7"), TEXT("Home"), 0.36f, -23.56f, -159.4f);
			At(TEXT("Home8"), TEXT("Home"), -3.39f, -34.52f, -12.5f);
			At(TEXT("Home1"), TEXT("Home"), 7.38f, 25.88f, 26.6f);
			return A;
		}();
		return All;
	}

	// The cobbled ground (2026-11-27): the plaza (PlazaR round the middle) and the lanes, as polylines in the plan frame (m) with a width
	constexpr float PlazaR = 1250.f;
	struct FLane { float W; TArray<FVector2D> Pts; };
	inline const TArray<FLane>& LanesPlan()
	{
		static const TArray<FLane> L = {
			{ 5.0f, { {11.f, 0.f}, {24.f, 0.6f}, {38.f, -0.6f}, {60.f, 0.4f} } },        // the high street, out along the road
			{ 3.4f, { {7.2f, 11.4f}, {11.f, 18.f}, {10.f, 26.f}, {6.f, 34.f} } },     // past the potion shop to the houses on the right
			{ 3.4f, { {-8.8f, 6.6f}, {-16.f, 11.f}, {-24.f, 11.f}, {-29.f, 8.f} } },  // round the town hall up to the academy
			{ 3.4f, { {-1.f, -11.5f}, {-3.f, -20.f}, {0.f, -28.f}, {-2.f, -37.f} } }, // past the smith to the houses on the left
		};
		return L;
	}
	// The lanes in game cm, on every machine
	inline const TArray<FLane>& Lanes()
	{
		static const TArray<FLane> All = []()
		{
			TArray<FLane> Out;
			const JeebOpen::FSite* S = Site();
			if (!S) return Out;
			const float H = RoadHeading(S->Pos);
			for (const FLane& L : LanesPlan())
			{
				FLane W; W.W = L.W * 100.f;
				for (const FVector2D& P : L.Pts) W.Pts.Add(S->Pos + Dir(H) * (P.X * 100.f) + Dir(H + 90.f) * (P.Y * 100.f));
				Out.Add(W);
			}
			return Out;
		}();
		return All;
	}
	// Distance (cm) from a spot to the cobbles' edge (negative on them): the plaza or a lane
	inline float StreetClearance(float X, float Y)
	{
		const JeebOpen::FSite* S = Site();
		if (!S) return 1.0e9f;
		const FVector2D P(X, Y);
		if (FVector2D::DistSquared(P, S->Pos) > FMath::Square(8000.f)) return 1.0e9f;
		float Best = float(FVector2D::Distance(P, S->Pos)) - PlazaR;
		for (const FLane& L : Lanes())
			for (int32 i = 0; i + 1 < L.Pts.Num(); ++i)
			{
				const FVector2D A = L.Pts[i], AB = L.Pts[i + 1] - A;
				const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / FMath::Max(AB.SizeSquared(), 1.f), 0.f, 1.f);
				Best = FMath::Min(Best, float(FVector2D::Distance(P, A + AB * T)) - L.W * 0.5f);
			}
		return Best;
	}
	// A spot along one lane (Lane 0 the high street), T 0..1 from the plaza out, and the lane's heading there
	inline FVector2D LaneSpot(int32 Lane, float T, float* OutYaw = nullptr)
	{
		const TArray<FLane>& L = Lanes();
		if (!L.IsValidIndex(Lane) || L[Lane].Pts.Num() < 2) return FVector2D::ZeroVector;
		const TArray<FVector2D>& P = L[Lane].Pts;
		float Total = 0.f; for (int32 i = 0; i + 1 < P.Num(); ++i) Total += FVector2D::Distance(P[i], P[i + 1]);
		float S = FMath::Clamp(T, 0.f, 1.f) * Total;
		for (int32 i = 0; i + 1 < P.Num(); ++i)
		{
			const float Seg = FVector2D::Distance(P[i], P[i + 1]);
			if (S <= Seg || i + 2 == P.Num())
			{
				if (OutYaw) *OutYaw = FMath::RadiansToDegrees(FMath::Atan2(P[i + 1].Y - P[i].Y, P[i + 1].X - P[i].X));
				return FMath::Lerp(P[i], P[i + 1], FMath::Clamp(S / FMath::Max(Seg, 1.f), 0.f, 1.f));
			}
			S -= Seg;
		}
		return P.Last();
	}

	inline const FPlot* FindPlot(const FString& Id)
	{
		for (const FPlot& P : Plots()) if (Id.Equals(P.Id, ESearchCase::IgnoreCase)) return &P;
		return nullptr;
	}

	// Signed distance (cm) from a point to a plot's walls (negative inside)
	inline float WallSD(const FPlot& P, float X, float Y)
	{
		const FVector2D D(X - P.Centre.X, Y - P.Centre.Y);
		const FVector2D Ax = Dir(P.Yaw), Ay = Dir(P.Yaw + 90.f);
		const FVector2D L(FVector2D::DotProduct(D, Ax), FVector2D::DotProduct(D, Ay));
		const FVector2D Mid = (P.WallLo + P.WallHi) * 0.5f, Half = (P.WallHi - P.WallLo) * 0.5f;
		const FVector2D Q(FMath::Abs(L.X - Mid.X) - Half.X, FMath::Abs(L.Y - Mid.Y) - Half.Y);
		return FVector2D(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f)).Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.f);
	}

	inline bool Near(float X, float Y, float Margin = 0.f)
	{
		const JeebOpen::FSite* S = Site();
		return S && FVector2D::DistSquared(S->Pos, FVector2D(X, Y)) < FMath::Square(7500.f + Margin);
	}

	// On a building's walls, with a margin in metres
	inline bool OnBuilding(float X, float Y, float MarginM = 0.f)
	{
		if (!Near(X, Y, 1000.f)) return false;
		for (const FPlot& P : Plots())
		{
			if (FVector2D::DistSquared(P.Centre, FVector2D(X, Y)) > FMath::Square(P.Reach + MarginM * 100.f + 100.f)) continue;
			if (WallSD(P, X, Y) < MarginM * 100.f) return true;
		}
		return false;
	}

	// A spot Out cm in front of a plot's door (the middle of its front wall), and the way out of the door
	inline FVector2D DoorSpot(const FPlot& P, float Out, float* OutYaw = nullptr)
	{
		const FVector2D Ax = Dir(P.Yaw), Ay = Dir(P.Yaw + 90.f);
		const float MidX = (P.WallLo.X + P.WallHi.X) * 0.5f;
		if (OutYaw) *OutYaw = P.DoorYaw;
		return P.Centre + Ax * MidX + Ay * (P.WallHi.Y + Out);
	}
}
