#pragma once

#include "CoreMinimal.h"
#include "Inventory.h"
#include "Misc/DateTime.h"

// The trading post's wares. Every real-world hour (UTC) the store restocks with a fresh random mix:
// four cheap things, four middling ones and four valuable ones, each with a few in stock.
// The same hour always rolls the same stock (the hour number is the seed), so it will match between players later.
//
// The mystery satchel: once per hour, for a small price, a random item from the odds table below.
namespace JeebStore
{
	using namespace JeebItems;

	struct FWare { int32 Id = 0; int32 Count = 1; int32 Price = 0; int32 Stock = 1; };   // Count > 1 for arrow bundles

	inline int64 HourNow() { return FDateTime::UtcNow().GetTicks() / ETimespan::TicksPerHour; }
	inline int32 MinutesToRestock() { return 59 - FDateTime::UtcNow().GetMinute(); }

	inline FWare GearWare(int32 Kind, int32 Tier, float PriceMul = 1.f)
	{
		FWare W;
		W.Id = GearId(Kind, Tier);
		W.Price = FMath::Max(1, FMath::RoundToInt(BuyPrice(Kind, Tier) * PriceMul));
		return W;
	}
	inline FWare PotionWare(int32 Type)
	{
		FWare W;
		W.Id = PotionId(Type);
		W.Count = 1;
		W.Price = PotionPrice(Type);
		W.Stock = 5;
		return W;
	}
	constexpr int32 RockBundle = 20, RockBundlePrice = 15;   // (the troll's throwing rocks, Steele Creek)
	inline FWare ArrowWare(int32 Row)
	{
		FWare W;
		W.Id = ArrowId(Row);
		W.Count = ArrowBundle;
		W.Price = ArrowBundlePrice(Row);
		W.Stock = 5;
		return W;
	}

	inline TArray<FWare> RollStock(int64 Hour)
	{
		FRandomStream R(int32(Hour * 2654435761ll));
		TArray<FWare> Out;
		auto AnyKind = [&](bool bHeavy) -> int32
		{
			static const int32 Light[] = { K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Wand, K_Bow, K_Rod, K_Shield, K_Helmet, K_Chest, K_Shoulders, K_Gauntlets, K_Boots, K_Cape };
			static const int32 Heavy[] = { K_Broadsword, K_BattleAxe, K_Staff, K_Chest, K_Shield, K_Bow, K_Longbow, K_Crossbow };
			return bHeavy ? Heavy[R.RandRange(0, UE_ARRAY_COUNT(Heavy) - 1)] : Light[R.RandRange(0, UE_ARRAY_COUNT(Light) - 1)];
		};
		auto Has = [&](int32 Id) { for (const FWare& W : Out) if (W.Id == Id) return true; return false; };
		auto Push = [&](FWare W, int32 MinStock, int32 MaxStock)
		{
			if (Has(W.Id)) return false;
			if (W.Stock <= 1) W.Stock = R.RandRange(MinStock, MaxStock);
			Out.Add(W);
			return true;
		};

		// Cheap: always a bundle of plain arrows, then bronze and steel bits 
		// Every potion is always in stock (Potions.csv)
		for (int32 T = 0; T < NumPotions(); ++T) Push(PotionWare(T), 5, 5);
		Push(ArrowWare(R.RandRange(A_Wooden, A_Broadhead)), 5, 5);
		for (int32 Guard = 0; Out.Num() < 6 && Guard < 50; ++Guard)
		{
			Push(GearWare(AnyKind(false), R.RandRange(T_Tin, T_Iron)), 2, 3);
		}
		// Middling: iron pieces, iron heavy weapons, rings, iron arrows
		for (int32 Guard = 0; Out.Num() < 9 && Guard < 50; ++Guard)
		{
			const float P = R.FRand();
			if (P < 0.30f) Push(GearWare(K_Ring, R.RandRange(0, FMath::Max(1, NumRings()) - 1)), 1, 2);
			else if (P < 0.42f) { static const int32 Mid[] = { A_Broadhead, A_Bodkin, A_Heavy, A_Fire }; const int32 Rw = Mid[R.RandRange(0, 3)]; if (ArrowRow(Rw).bShop) Push(ArrowWare(Rw), 3, 3); }   // (2026-11-26: one of the bought arrows, Arrows.csv Shop)
			else if (P < 0.52f) { const int32 T = R.RandRange(T_Tin, T_Iron); FWare W = GearWare(K_Shield, T); W.Id = ShieldId(R.RandRange(1, 2), T); Push(W, 1, 2); }   // a round or a tower shield
			else if (P < 0.70f) Push(GearWare(AnyKind(false), T_Iron), 1, 2);
			else Push(GearWare(AnyKind(true), T_Iron), 1, 2);
		}
		// Valuable: iron heavy weapons, amulets, and now and then a silver piece the smith did not want to keep
		for (int32 Guard = 0; Out.Num() < 12 && Guard < 50; ++Guard)
		{
			const float P = R.FRand();
			if (P < 0.35f) Push(GearWare(K_Amulet, R.RandRange(0, FMath::Max(1, NumAmulets()) - 1)), 1, 1);
			else if (P < 0.80f) Push(GearWare(AnyKind(true), T_Iron), 1, 1);
			else Push(GearWare(AnyKind(R.FRand() < 0.5f), T_Silver, 1.25f), 1, 1);
		}
		// armour comes in a random style (2026-11-03): classic, light or heavy, priced for it
		for (FWare& W : Out)
			if (IsGear(W.Id) && IsArmorKind(KindOf(W.Id)))
			{
				const float Mul = float(W.Price) / FMath::Max(1, BuyPrice(KindOf(W.Id), VariantOf(W.Id)));   // (keeps a ware's own mark-up)
				W.Id = ArmourId(KindOf(W.Id), R.RandRange(0, NumRolledArmourStyles - 1), VariantOf(W.Id));
				W.Price = FMath::Max(1, FMath::RoundToInt(BuyPrice(KindOf(W.Id), FullVariant(W.Id)) * Mul));
			}
		// (2026-11-26, Docs/EconomyPlan.md) Shops by town: the wizard's wares (sold only at Silver Point) and the troll's (only at Steele Creek), after
		// everything above so the older wares keep their places and prices. Each shop shows only what its town sells (JeebShops::Sells).
		auto ById = [&](int32 Id, int32 MinStock, int32 MaxStock) { FWare W; W.Id = Id; W.Price = FMath::Max(1, BuyPrice(KindOf(Id), FullVariant(Id))); Push(W, MinStock, MaxStock); };
		ById(GearId(K_Wand, R.RandRange(T_Tin, T_Iron)), 2, 3);
		ById(GearId(K_Staff, R.RandRange(T_Tin, T_Iron)), 1, 2);
		ById(WizOffId(R.RandRange(WO_Book, WO_Pipe), R.RandRange(T_Tin, T_Iron)), 1, 2);
		ById(ArmourId(K_Helmet, AS_Wizard, R.RandRange(T_Tin, T_Iron)), 1, 2);
		ById(ArmourId(K_Chest, AS_Wizard, R.RandRange(T_Tin, T_Iron)), 1, 2);
		ById(ArmourId(K_Boots, AS_Wizard, R.RandRange(T_Tin, T_Iron)), 1, 2);
		ById(GearId(K_Club, R.RandRange(T_Tin, T_Iron)), 2, 3);
		ById(GearId(K_Broadsword, R.RandRange(T_Tin, T_Iron)), 1, 2);
		{ static const int32 TP[] = { K_Helmet, K_Chest, K_Shoulders, K_Gauntlets, K_Boots }; ById(ArmourId(TP[R.RandRange(0, 4)], AS_Troll, R.RandRange(T_Tin, T_Iron)), 1, 2); }
		ById(ArmourId(K_Chest, AS_Troll, R.RandRange(T_Tin, T_Iron)), 1, 2);
		{ FWare W; W.Id = IdRocks; W.Count = RockBundle; W.Price = RockBundlePrice; W.Stock = 5; Push(W, 5, 5); }
		ById(GearId(K_BoneClub, R.RandRange(T_Tin, T_Iron)), 1, 2);   // (2026-11-27: the bone club, Steele Creek only by its Class troll; after the rocks so older wares keep their places)
		// (2026-11-26/27, owner) vaulted things are never sold, and every knight armour piece is now the one Knight's Armor piece: one ware per item
		TArray<FWare> Kept;
		for (const FWare& W : Out)
		{
			if (IsVaulted(W.Id)) continue;
			if (FWare* Same = Kept.FindByPredicate([&](const FWare& K) { return K.Id == W.Id; })) { Same->Stock += W.Stock; continue; }
			Kept.Add(W);
		}
		return Kept;
	}

	// This hour's wares, with whatever has been bought so far taken off the stock
	inline TArray<FWare>& Stock()
	{
		static int64 Hour = -1;
		static TArray<FWare> S;
		const int64 H = HourNow();
		if (H != Hour) { Hour = H; S = RollStock(H); }
		return S;
	}

	// ----------------------------------------------------------------- the mystery satchel
	constexpr int32 GamblePrice = 30;

	struct FOdds { const TCHAR* Label; float Percent; };
	inline const TArray<FOdds>& GambleTable()
	{
		static const TArray<FOdds> T = {
			{ TEXT("10 broadhead arrows"),               28.0f },
			{ TEXT("A bronze or steel tool or weapon"), 22.0f },
			{ TEXT("40 ingots"),                    15.0f },
			{ TEXT("25 ingots"),                    12.0f },   // (was a cape, until capes became the PvP rank)
			{ TEXT("A steel piece of gear"),       10.0f },
			{ TEXT("A ring"),                        7.0f },
			{ TEXT("An amulet"),                     4.0f },
			{ TEXT("A silver piece of gear"),         1.5f },
			{ TEXT("A silver piece (lucky)"),         0.5f },
		};
		return T;
	}

	// Rolls the satchel. Returns the outcome row; OutId / OutCount is the item (OutId 0 = ingots, OutCount = how many).
	inline int32 RollGamble(FRandomStream& R, int32& OutId, int32& OutCount)
	{
		const TArray<FOdds>& T = GambleTable();
		float Roll = R.FRandRange(0.f, 100.f);
		int32 Row = T.Num() - 1;
		for (int32 i = 0; i < T.Num(); ++i)
		{
			if (Roll < T[i].Percent) { Row = i; break; }
			Roll -= T[i].Percent;
		}
		static const int32 Tools[] = { K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Wand, K_Bow, K_Rod, K_Shield };
		static const int32 AnyGear[] = { K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Broadsword, K_BattleAxe, K_Wand, K_Staff, K_Bow, K_Longbow, K_Crossbow, K_Shield, K_Helmet, K_Chest, K_Shoulders, K_Gauntlets, K_Boots };
		auto Pick = [&](const int32* A, int32 N) { return A[R.RandRange(0, N - 1)]; };
		OutCount = 1;
		switch (Row)
		{
		case 0: OutId = ArrowId(A_Broadhead); OutCount = ArrowBundle; break;
		case 1: OutId = GearId(Pick(Tools, UE_ARRAY_COUNT(Tools)), R.RandRange(T_Tin, T_Iron)); break;
		case 2: OutId = 0; OutCount = 40; break;
		case 3: OutId = 0; OutCount = 25; break;
		case 4: OutId = GearId(Pick(AnyGear, UE_ARRAY_COUNT(AnyGear)), T_Iron); break;
		case 5: OutId = GearId(K_Ring, R.RandRange(0, FMath::Max(1, NumRings()) - 1)); break;
		case 6: OutId = GearId(K_Amulet, R.RandRange(0, FMath::Max(1, NumAmulets()) - 1)); break;
		case 7: OutId = GearId(Pick(AnyGear, UE_ARRAY_COUNT(AnyGear)), T_Silver); break;
		default: OutId = GearId(Pick(AnyGear, UE_ARRAY_COUNT(AnyGear)), T_Gold); break;
		}
		if (IsGear(OutId) && IsArmorKind(KindOf(OutId))) OutId = ArmourId(KindOf(OutId), R.RandRange(0, NumRolledArmourStyles - 1), VariantOf(OutId));   // (armour in any style, 2026-11-03)
		if (IsGear(OutId) && IsVaulted(OutId)) OutId = IsArmorKind(KindOf(OutId)) ? ArmourId(KindOf(OutId), AS_Classic, TopGameTier()) : GearId(KindOf(OutId), TopGameTier());   // (vaulted ranks, 2026-11-27: the best rank in the game instead)
		return Row;
	}
}
