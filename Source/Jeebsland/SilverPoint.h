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
			// round the plaza (angle from the road's heading, distance in m), door toward the middle
			auto Ring = [&](const TCHAR* Id, const TCHAR* Kind, float Ang, float DistM)
			{
				const FVector2D C = Mid + Dir(H + Ang) * (DistM * 100.f);
				Add(Id, Kind, C, H + Ang + 180.f);
			};
			// along the high street (m out along the road, m to its side: + is to the right looking out), door toward the street
			auto Street = [&](const TCHAR* Id, const TCHAR* Kind, float AlongM, float SideM)
			{
				const FVector2D C = Mid + Dir(H) * (AlongM * 100.f) + Dir(H + 90.f) * (SideM * 100.f);
				Add(Id, Kind, C, SideM > 0.f ? H - 90.f : H + 90.f);
			};
			Ring(TEXT("BroomShop"), TEXT("BroomShop"), 42.f, 26.f);
			Ring(TEXT("Store"), TEXT("Store"), 80.f, 26.f);
			Ring(TEXT("Bank"), TEXT("Bank"), 118.f, 26.f);
			Ring(TEXT("TownHall"), TEXT("TownHall"), 156.f, 30.f);
			Ring(TEXT("Academy"), TEXT("Academy"), 190.f, 52.f);
			Ring(TEXT("Smith"), TEXT("Smith"), -150.f, 29.f);
			Ring(TEXT("Tavern"), TEXT("Tavern"), -114.f, 28.f);
			Ring(TEXT("PotionShop"), TEXT("PotionShop"), -78.f, 26.f);
			Ring(TEXT("Home1"), TEXT("Home"), -44.f, 26.f);
			Street(TEXT("WandShop"), TEXT("WandShop"), 30.f, 10.f);
			Street(TEXT("CrystalShop"), TEXT("CrystalShop"), 30.f, -10.5f);
			Street(TEXT("PetShop"), TEXT("PetShop"), 45.f, 11.5f);
			Street(TEXT("Home2"), TEXT("Home"), 45.f, -11.f);
			Street(TEXT("Home3"), TEXT("Home"), 58.f, 11.f);
			Street(TEXT("Home4"), TEXT("Home"), 58.f, -11.f);
			Ring(TEXT("Home5"), TEXT("Home"), 62.f, 47.f);
			Ring(TEXT("Home6"), TEXT("Home"), 100.f, 47.f);
			Ring(TEXT("Home7"), TEXT("Home"), -98.f, 48.f);
			Ring(TEXT("Home8"), TEXT("Home"), -62.f, 47.f);
			return A;
		}();
		return All;
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
