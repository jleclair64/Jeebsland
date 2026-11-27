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

	// The catalogue, made from the data tables (Kinds, Tiers, ShieldStyles, ArmourStyles, Accessories, Potions, Materials), so a new
	// weapon kind, ring or potion shows up by itself. Each page has rows; a row is a label and its items (rank by rank for gear).
	// Two sections (owner, 2026-11-26): "In game" (what players can get now) and "Vaulted" (kept in the files, off in the game: JeebItems::IsVaulted).
	// Heads: the column names over a page's rows (the ranks), empty for none.
	struct FRow { FString Label; TArray<int32> Ids; };
	struct FPage { FString Name; TArray<FRow> Rows; bool bVaulted = false; TArray<FString> Heads; };
	inline TArray<FPage> Pages()
	{
		using namespace JeebItems;
		TArray<FPage> P;
		const TArray<int32> Game = GameTiers();
		TArray<int32> Old;   // the vaulted ranks (Gold, Emerald, Diamond)
		for (int32 T = 0; T < NumTiers; ++T) if (!IsTierInGame(T)) Old.Add(T);
		auto Heads = [](const TArray<int32>& Ts) { TArray<FString> H; for (int32 T : Ts) H.Add(TierName(T)); return H; };
		auto Of = [](const TArray<int32>& Ts, TFunctionRef<int32(int32)> IdFor, bool bVaulted)
		{
			TArray<int32> A;
			for (int32 T : Ts) { const int32 Id = IdFor(T); if (IsVaulted(Id) == bVaulted && !A.Contains(Id)) A.Add(Id); }
			return A;
		};
		auto AddRow = [](FPage& Pg, const FString& Label, const TArray<int32>& Ids) { if (Ids.Num()) Pg.Rows.Add({ Label, Ids }); };
		TArray<int32> AllTiers; for (int32 T = 0; T < NumTiers; ++T) AllTiers.Add(T);

		// ---------------- In game ----------------
		{
			FPage& W = P.AddDefaulted_GetRef(); W.Name = TEXT("Weapons"); W.Heads = Heads(Game);
			for (int32 K = 0; K < K_Count; ++K)
				if (IsWeaponKind(K)) AddRow(W, KindName(K), Of(Game, [&](int32 T) { return GearId(K, T); }, false));
		}
		{
			FPage& A = P.AddDefaulted_GetRef(); A.Name = *ArmourStyleRow(AS_Classic).Name;   // Knight's Armor: one piece a slot
			for (int32 K = K_Helmet; K <= K_Boots; ++K) AddRow(A, KindName(K), { KnightArmourId(K) });
		}
		for (int32 S : { int32(AS_Wizard), int32(AS_Troll) })
		{
			FPage& A = P.AddDefaulted_GetRef(); A.Name = FString::Printf(TEXT("%s armour"), *ArmourStyleRow(S).Name); A.Heads = Heads(Game);
			for (int32 K = K_Helmet; K <= K_Boots; ++K)
				if (StyleHasPiece(K, S)) AddRow(A, KindName(K), Of(Game, [&](int32 T) { return ArmourId(K, S, T); }, false));
		}
		{
			FPage& Sh = P.AddDefaulted_GetRef(); Sh.Name = TEXT("Shields"); Sh.Heads = Heads(Game);
			for (int32 S = 0; S < NumShieldStyles; ++S) AddRow(Sh, ShieldStyleName(S), Of(Game, [&](int32 T) { return ShieldId(S, T); }, false));
		}
		{
			FPage& O = P.AddDefaulted_GetRef(); O.Name = TEXT("Other");
			AddRow(O, TEXT("Capes"), Of(Game, [&](int32 T) { return GearId(K_Cape, T); }, false));
			FRow Rings{ TEXT("Rings") }; for (int32 V = 0; V < FMath::Min(NumRings(), MaxVariants); ++V) Rings.Ids.Add(GearId(K_Ring, V));
			FRow Amulets{ TEXT("Amulets") }; for (int32 V = 0; V < FMath::Min(NumAmulets(), MaxVariants); ++V) Amulets.Ids.Add(GearId(K_Amulet, V));
			if (Rings.Ids.Num()) O.Rows.Add(Rings);
			if (Amulets.Ids.Num()) O.Rows.Add(Amulets);
			O.Rows.Add({ KindName(K_Crest), { GearId(K_Crest, 0) } });
			for (int32 S = 0; S < NumWizOffStyles; ++S) AddRow(O, WizOffName(S), Of(Game, [&](int32 T) { return WizOffId(S, T); }, false));   // (the wizard's off hand: spell book, crystal ball, pipe)
			FRow Potions{ TEXT("Potions") }; for (int32 T = 0; T < NumPotions(); ++T) Potions.Ids.Add(PotionId(T));
			if (Potions.Ids.Num()) O.Rows.Add(Potions);
			FRow Arrows{ TEXT("Arrows") }; for (int32 i = 0; i < NumArrowKinds(); ++i) if (!IsVaulted(ArrowKindId(i))) Arrows.Ids.Add(ArrowKindId(i));
			O.Rows.Add(Arrows);
			O.Rows.Add({ TEXT("Throwing rocks"), { IdRocks } });   // (the troll's ammunition, 2026-11-22)
			FRow Brooms{ TEXT("Flying brooms") }; for (int32 b = 0; b < JeebBroom::NumBrooms(); ++b) Brooms.Ids.Add(BroomId(b));   // (the wizard's brooms, 2026-11-22)
			if (Brooms.Ids.Num()) O.Rows.Add(Brooms);
			// (2026-11-27, the metals rework) the smithy's ores and bars, a stack a click
			FRow Ores{ TEXT("Ores") }, Bars{ TEXT("Bars") };
			for (int32 m = 0; m < Tables().Materials.Num(); ++m)
			{
				const EItemType T = static_cast<EItemType>(m);
				if (MaterialRow(T).bVaulted) continue;
				if (MaterialRow(T).Group == MG_Ore) Ores.Ids.Add(MaterialId(T));
				else if (MaterialRow(T).Group == MG_Bar) Bars.Ids.Add(MaterialId(T));
			}
			if (Ores.Ids.Num()) O.Rows.Add(Ores);
			if (Bars.Ids.Num()) O.Rows.Add(Bars);
		}

		// ---------------- Vaulted (kept in the files, off in the game) ----------------
		{
			FPage& W = P.AddDefaulted_GetRef(); W.Name = TEXT("Weapons"); W.bVaulted = true; W.Heads = Heads(Old);
			for (int32 K = 0; K < K_Count; ++K)
				if (IsWeaponKind(K))
				{
					const bool bKindGone = IsRetiredKind(K) || KindRow(K).bVaulted;
					AddRow(W, bKindGone ? FString::Printf(TEXT("%s (every rank)"), KindName(K)) : FString(KindName(K)), Of(bKindGone ? AllTiers : Old, [&](int32 T) { return GearId(K, T); }, true));
				}
		}
		{
			FPage& A = P.AddDefaulted_GetRef(); A.Name = TEXT("Armour"); A.bVaulted = true;
			for (int32 S = 0; S < NumArmourStyles; ++S)
			{
				if (IsKnightStyle(S)) continue;   // (folded into Knight's Armor: those ids are gone)
				for (int32 K = K_Helmet; K <= K_Boots; ++K)
					if (StyleHasPiece(K, S)) AddRow(A, FString::Printf(TEXT("%s %s"), *ArmourStyleRow(S).Name, KindName(K)), Of(AllTiers, [&](int32 T) { return ArmourId(K, S, T); }, true));
			}
		}
		{
			FPage& O = P.AddDefaulted_GetRef(); O.Name = TEXT("Other"); O.bVaulted = true; O.Heads = Heads(Old);
			for (int32 S = 0; S < NumShieldStyles; ++S) AddRow(O, ShieldStyleName(S), Of(Old, [&](int32 T) { return ShieldId(S, T); }, true));
			AddRow(O, TEXT("Capes"), Of(Old, [&](int32 T) { return GearId(K_Cape, T); }, true));
			for (int32 S = 0; S < NumWizOffStyles; ++S) AddRow(O, WizOffName(S), Of(Old, [&](int32 T) { return WizOffId(S, T); }, true));
			FRow Mats{ TEXT("Materials") };
			for (int32 m = 0; m < Tables().Materials.Num(); ++m) if (MaterialRow(static_cast<EItemType>(m)).bVaulted) Mats.Ids.Add(MaterialId(static_cast<EItemType>(m)));
			if (Mats.Ids.Num()) O.Rows.Add(Mats);
		}
		P.RemoveAll([](const FPage& Pg) { return Pg.Rows.Num() == 0; });
		return P;
	}
	inline bool InCatalogue(int32 Id)
	{
		for (const FPage& Pg : Pages()) for (const FRow& R : Pg.Rows) if (R.Ids.Contains(Id)) return true;
		return false;
	}
	// How many one click gives: a full stack of arrows or potions, one of anything else
	inline int32 GiveCount(int32 Id) { if (JeebItems::IsMaterial(Id)) return FMath::Min(JeebItems::StackSize(Id), 28); return (JeebItems::IsArrow(Id) || JeebItems::IsRock(Id) || JeebItems::IsPotion(Id)) ? JeebItems::StackSize(Id) : 1; }   // (ores and bars: 28 a click)
}
