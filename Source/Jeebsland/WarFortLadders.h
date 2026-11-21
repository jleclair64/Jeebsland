#pragma once
// Ladders up to the fighting walk inside Oak Hill's two log forts (2026-11-20, owner: "I want all fort builds like this to have the
// ability for the player to get up onto the top platform inside the fort via ladder").
// The fort (SM_TL_Fort, the Meshy model OakHillFort_300k as building_clean.py sized it, drawn at FModel Scale 1.4) has a plank walk along
// its back wall: measured on the model in Blender (ray casts down, model cm before Scale, Unreal's frame: X along, Y with the gate at +Y),
// its top is 213 cm over the model's floor, from Y -130 (its front edge) back to -230 (the palisade), X -480 to +440. Each fort gets
// one ladder at X +300 against the walk's front edge, climbed from the yard (Out toward the gate) with the same code as every other
// ladder (KnightCharacterMovement.cpp: W at the foot climbs, S on the walk climbs down). Data only, worked out the same on the server
// and every client from the town's plots and the ground; ATownLabTown draws them.
#include "CoreMinimal.h"
#include "Terrain.h"
#include "OakHill.h"
#include "TownLab.h"

namespace JeebWarFort
{
	constexpr float WalkTop = 213.f, WalkFront = -130.f, LadderX = 300.f;   // (model cm, before Scale)

	// Where a war building stands (TownLabTown.cpp places it here): a quarter of the way up from the lowest ground under its walls
	inline float GroundZ(const JeebOakHill::FPlot& P, const JeebOakHill::FModel* Mdl, const FVector2D& C)
	{
		const float R0 = FMath::DegreesToRadians(JeebOakHill::BuildingYaw(P, Mdl)), Cs0 = FMath::Cos(R0), Sn0 = FMath::Sin(R0);
		float Lo = 1.0e9f, Hi = -1.0e9f;
		for (int32 a = 0; a <= 6; ++a) for (int32 b = 0; b <= 6; ++b)
		{
			const float Lx = FMath::Lerp(Mdl->WX0, Mdl->WX1, a / 6.f) * Mdl->Scale, Ly = FMath::Lerp(-Mdl->WY1, -Mdl->WY0, b / 6.f) * Mdl->Scale;
			const float H = JeebTerrain::HeightAt(C.X + Lx * Cs0 - Ly * Sn0, C.Y + Lx * Sn0 + Ly * Cs0);
			Lo = FMath::Min(Lo, H); Hi = FMath::Max(Hi, H);
		}
		return Lo + 0.25f * (Hi - Lo);
	}

	inline const TArray<JeebTerrain::FLadder>& Ladders()
	{
		static const TArray<JeebTerrain::FLadder> All = []()
		{
			TArray<JeebTerrain::FLadder> A;
			if (!JeebTownLab::RealTown()) return A;
			const JeebTownLab::FSite& St = JeebTownLab::RealSite();
			for (const JeebOakHill::FPlot& P : JeebOakHill::Plots())
			{
				if (P.Kind != JeebOakHill::EPlot::Building || !P.Model || FCString::Strcmp(P.Model, TEXT("OakHillFort")) != 0) continue;
				const JeebOakHill::FModel* Mdl = JeebOakHill::FindModel(P.Model);
				if (!Mdl) continue;
				const FVector2D C = JeebOakHill::FromPx(P.Px, P.Py) + FVector2D(0.f, St.OffY);
				const float Yaw = FMath::DegreesToRadians(JeebOakHill::BuildingYaw(P, Mdl)), Cs = FMath::Cos(Yaw), Sn = FMath::Sin(Yaw);
				const float Lx = LadderX * Mdl->Scale, Ly = WalkFront * Mdl->Scale;
				JeebTerrain::FLadder L;
				L.Pos = C + FVector2D(Lx * Cs - Ly * Sn, Lx * Sn + Ly * Cs);
				L.Out = FVector2D(-Sn, Cs);   // (the model's +Y: toward the gate, the yard side)
				L.Top = GroundZ(P, Mdl, C) + WalkTop * Mdl->Scale;
				L.Bottom = JeebTerrain::HeightAt(L.Pos.X + L.Out.X * 60.f, L.Pos.Y + L.Out.Y * 60.f);
				L.bAbsolute = true;
				A.Add(L);
			}
			return A;
		}();
		return All;
	}
}
