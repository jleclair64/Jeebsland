#pragma once

#include "CoreMinimal.h"

// WHICH MODEL EACH BUILDING OF THE NEW TOWN USES (moved here from TownLabTown.cpp on 2026-11-16 so the town fill (Fill.cpp) reads the
// same table instead of a copy). Any of the town's models can be swapped here for a new one (a Fab pack's, a Meshy remake) without
// touching anything else: Mesh is the new static mesh's path (its solid shape is Mesh + "_Col" when that exists, else the old
// model's), Scale replaces OakHill.h's FModel Scale when above 0, YawAdd turns it (its door must face +Y as the old models' do, or
// say so here), Lift raises it in cm. A missing mesh falls back to the old model, so a row can wait for its asset.
// Keep the old model's frame and Scale 0 where possible: the shop counters (OakHill.h Counters), the grass's footprints
// (OakHillFootprints.h) and the chimneys (OakHillLife.cpp) are all measured on the old models.
namespace JeebTownLab
{
	struct FLabSwap { const TCHAR* Model; const TCHAR* Mesh; float Scale; float YawAdd; float Lift; };
	inline const FLabSwap ModelSwaps[] = {
		{ TEXT("OakHillHouse"), TEXT("/Game/TownLab/Buildings/SM_TL_House"), 0.f, 0.f, 0.f },   // townbuild's rebuilt house
		{ TEXT("OakHillStore"), TEXT("/Game/TownLab/Buildings/SM_TL_Store"), 0.f, 0.f, 0.f },   // townbuild's rebuilt store
		// build-b's four (townbuild2, 2026-11-14)
		{ TEXT("OakHillTownHall"), TEXT("/Game/TownLab/Buildings/SM_TL_TownHall"), 0.f, 0.f, 0.f },
		{ TEXT("OakHillChurch"), TEXT("/Game/TownLab/Buildings/SM_TL_Church"), 0.f, 0.f, 0.f },   // (owner, 2026-11-15: the rebuilt church, for its clean window glass; its -X side wall is Docs/TownLab/Buildings.md "for later")
		{ TEXT("OakHillWell"), TEXT("/Game/TownLab/Buildings/SM_TL_Well"), 0.f, 0.f, 0.f },
		{ TEXT("OakHillPotionShop"), TEXT("/Game/TownLab/Buildings/SM_TL_PotionShop"), 0.f, 0.f, 0.f },
		{ TEXT("OakHillBank"), TEXT("/Game/TownLab/Buildings/SM_TL_Bank"), 0.f, 0.f, 0.f },   // build-a: the bank
		{ TEXT("BankStall"), TEXT("/Game/TownLab/Buildings/SM_TL_BankStall"), 0.f, 0.f, 0.f },   // build-a: the bank stall
		{ TEXT("OakHillSmith"), TEXT("/Game/TownLab/Buildings/SM_TL_Smith"), 0.f, 0.f, 0.f },   // build-a: the smithy (its forge burns all day)
		{ TEXT("OakHillCornerHouse"), TEXT("/Game/TownLab/Buildings/SM_TL_CornerHouse"), 0.f, 0.f, 0.f },   // build-a: the corner house
		{ TEXT("GeneralStore"), TEXT("/Game/TownLab/Buildings/SM_TL_MarketStall"), 0.f, 0.f, 0.f },   // build-c's rebuilt market stall
		{ TEXT("PotionStall"), TEXT("/Game/TownLab/Buildings/SM_TL_PotionStall"), 0.f, 0.f, 0.f },    // build-c's rebuilt potion stall
	};

	// The rebuilt model's short name for an Oak Hill building model ("OakHillHouse" -> "TL_House", the name TownLabDecor.h files its
	// pieces under), or nullptr when it has none
	inline const TCHAR* RebuiltName(const TCHAR* Model)
	{
		if (!Model) return nullptr;
		for (const FLabSwap& S : ModelSwaps)
			if (FCString::Strcmp(S.Model, Model) == 0)
				if (const TCHAR* P = FCString::Strstr(S.Mesh, TEXT("/SM_"))) return P + 4;
		return nullptr;
	}
}
