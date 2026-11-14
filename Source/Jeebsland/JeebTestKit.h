#pragma once

#include "CoreMinimal.h"
#include "Inventory.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"

// TEMPORARY, testing only (owner, 2026-11-13): the test kit in the Tab screen, a list of every item that can be worn or held
// (one click wears it or puts it in the pack) and a box that sets every skill to one level. Only for testers, by the same
// gate as the other [Jeebsland.Testing] switches in DefaultGame.ini: an account listed under MaxLevelAccounts, or editor Play
// on this PC while MaxLevelInEditor is on. Take that section out (or those two lines) and the kit is gone everywhere.
// The server decides (AKnightPlayerController::ServerTestGive / ServerTestLevels check Allowed); the player's machine only
// shows the panel when the server says so (bTestKit, replicated to its owner).
namespace JeebTestKit
{
	// Is this player a tester? Server side: AccountKey is the logged-in account (empty offline)
	inline bool Allowed(const UWorld* World, bool bLoggedIn, const FString& AccountKey)
	{
		if (!GConfig) return false;
		bool bEditorMax = false;
		GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("MaxLevelInEditor"), bEditorMax, GGameIni);
		if (bEditorMax && World && World->IsPlayInEditor()) return true;
#if !UE_BUILD_SHIPPING
		// (test pictures: -game -JeebShot on this PC, standalone, with the same switch on; never in a shipped game)
		if (bEditorMax && World && World->GetNetMode() == NM_Standalone && FParse::Param(FCommandLine::Get(), TEXT("JeebShot"))) return true;
#endif
		if (!bLoggedIn || AccountKey.IsEmpty()) return false;
		TArray<FString> Accounts;
		GConfig->GetArray(TEXT("Jeebsland.Testing"), TEXT("MaxLevelAccounts"), Accounts, GGameIni);
		return Accounts.ContainsByPredicate([&](const FString& A) { return A.Equals(AccountKey, ESearchCase::IgnoreCase); });
	}

	// The catalogue, made from the data tables (Kinds, Tiers, ShieldStyles, ArmourStyles, Accessories, Potions), so a new
	// weapon kind, ring or potion shows up by itself. Each page has rows; a row is a label and its items (tier by tier for gear).
	struct FRow { FString Label; TArray<int32> Ids; };
	struct FPage { FString Name; TArray<FRow> Rows; };
	inline TArray<FPage> Pages()
	{
		using namespace JeebItems;
		TArray<FPage> P;
		auto Tiers = [](TFunctionRef<int32(int32)> IdFor) { TArray<int32> A; for (int32 T = 0; T < NumTiers; ++T) A.Add(IdFor(T)); return A; };
		{
			FPage& W = P.AddDefaulted_GetRef(); W.Name = TEXT("Weapons");
			for (int32 K = 0; K < K_Count; ++K)
				if (IsWeaponKind(K) && !IsRetiredKind(K)) W.Rows.Add({ KindName(K), Tiers([&](int32 T) { return GearId(K, T); }) });
		}
		for (int32 S = 0; S < NumArmourStyles; ++S)   // one page per armour style (Classic, Light, Heavy)
		{
			FPage& A = P.AddDefaulted_GetRef(); A.Name = FString::Printf(TEXT("%s armour"), *ArmourStyleRow(S).Name);
			for (int32 K = K_Helmet; K <= K_Boots; ++K)
				if (StyleHasPiece(K, S)) A.Rows.Add({ KindName(K), Tiers([&](int32 T) { return ArmourId(K, S, T); }) });
		}
		{
			FPage& Sh = P.AddDefaulted_GetRef(); Sh.Name = TEXT("Shields");
			for (int32 S = 0; S < NumShieldStyles; ++S) Sh.Rows.Add({ ShieldStyleName(S), Tiers([&](int32 T) { return ShieldId(S, T); }) });
		}
		{
			FPage& O = P.AddDefaulted_GetRef(); O.Name = TEXT("Other");
			O.Rows.Add({ TEXT("Capes"), Tiers([&](int32 T) { return GearId(K_Cape, T); }) });
			FRow Rings{ TEXT("Rings") }; for (int32 V = 0; V < FMath::Min(NumRings(), MaxVariants); ++V) Rings.Ids.Add(GearId(K_Ring, V));
			FRow Amulets{ TEXT("Amulets") }; for (int32 V = 0; V < FMath::Min(NumAmulets(), MaxVariants); ++V) Amulets.Ids.Add(GearId(K_Amulet, V));
			if (Rings.Ids.Num()) O.Rows.Add(Rings);
			if (Amulets.Ids.Num()) O.Rows.Add(Amulets);
			O.Rows.Add({ KindName(K_Crest), { GearId(K_Crest, 0) } });
			FRow Potions{ TEXT("Potions") }; for (int32 T = 0; T < NumPotions(); ++T) Potions.Ids.Add(PotionId(T));
			if (Potions.Ids.Num()) O.Rows.Add(Potions);
			FRow Arrows{ TEXT("Arrows") }; for (int32 i = 0; i < NumArrowKinds(); ++i) Arrows.Ids.Add(ArrowKindId(i));
			O.Rows.Add(Arrows);
		}
		return P;
	}
	inline bool InCatalogue(int32 Id)
	{
		for (const FPage& Pg : Pages()) for (const FRow& R : Pg.Rows) if (R.Ids.Contains(Id)) return true;
		return false;
	}
	// How many one click gives: a full stack of arrows or potions, one of anything else
	inline int32 GiveCount(int32 Id) { return (JeebItems::IsArrow(Id) || JeebItems::IsPotion(Id)) ? JeebItems::StackSize(Id) : 1; }
}
