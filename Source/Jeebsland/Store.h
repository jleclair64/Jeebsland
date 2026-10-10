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
	inline FWare ArrowWare(int32 Tier)
	{
		FWare W;
		W.Id = ArrowId(Tier);
		W.Count = ArrowBundle;
		W.Price = ArrowBundlePrice(Tier);
		W.Stock = 5;
		return W;
	}

	inline TArray<FWare> RollStock(int64 Hour)
	{
		FRandomStream R(int32(Hour * 2654435761ll));
		TArray<FWare> Out;
		auto AnyKind = [&](bool bHeavy) -> int32
		{
			static const int32 Light[] = { K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Wand, K_Bow, K_Rod, K_Shield, K_Helmet, K_Chest, K_Shoulders, K_Gloves, K_Boots };
			static const int32 Heavy[] = { K_Broadsword, K_BattleAxe, K_Katana, K_Staff, K_Chest, K_Shield, K_Bow };
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

		// Cheap: always a bundle of plain arrows, then wood and bronze bits and a cape
		Push(ArrowWare(R.RandRange(0, 1)), 5, 5);
		for (int32 Guard = 0; Out.Num() < 4 && Guard < 50; ++Guard)
		{
			if (R.FRand() < 0.25f) Push(GearWare(K_Cape, R.RandRange(0, NumCapeColors - 1)), 1, 2);
			else Push(GearWare(AnyKind(false), R.RandRange(0, 1)), 2, 3);
		}
		// Middling: iron pieces, bronze heavy weapons, rings, iron arrows
		for (int32 Guard = 0; Out.Num() < 8 && Guard < 50; ++Guard)
		{
			const float P = R.FRand();
			if (P < 0.30f) Push(GearWare(K_Ring, R.RandRange(0, NumRings - 1)), 1, 2);
			else if (P < 0.42f) Push(ArrowWare(2), 3, 3);
			else if (P < 0.70f) Push(GearWare(AnyKind(false), 2), 1, 2);
			else Push(GearWare(AnyKind(true), 1), 1, 2);
		}
		// Valuable: iron heavy weapons, amulets, and now and then a steel piece the smith did not want to keep
		for (int32 Guard = 0; Out.Num() < 12 && Guard < 50; ++Guard)
		{
			const float P = R.FRand();
			if (P < 0.35f) Push(GearWare(K_Amulet, R.RandRange(0, NumAmulets - 1)), 1, 1);
			else if (P < 0.80f) Push(GearWare(AnyKind(true), 2), 1, 1);
			else Push(GearWare(AnyKind(R.FRand() < 0.5f), 3, 1.25f), 1, 1);
		}
		return Out;
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
			{ TEXT("10 bronze arrows"),            28.0f },
			{ TEXT("A wood or bronze tool or weapon"), 22.0f },
			{ TEXT("40 ingots"),                    15.0f },
			{ TEXT("A cape"),                       12.0f },
			{ TEXT("An iron piece of gear"),        10.0f },
			{ TEXT("A ring"),                        7.0f },
			{ TEXT("An amulet"),                     4.0f },
			{ TEXT("A steel piece of gear"),         1.5f },
			{ TEXT("A diamond piece of gear"),       0.5f },
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
		static const int32 AnyGear[] = { K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Broadsword, K_BattleAxe, K_Katana, K_Wand, K_Staff, K_Bow, K_Shield, K_Helmet, K_Chest, K_Shoulders, K_Gloves, K_Boots };
		auto Pick = [&](const int32* A, int32 N) { return A[R.RandRange(0, N - 1)]; };
		OutCount = 1;
		switch (Row)
		{
		case 0: OutId = ArrowId(1); OutCount = ArrowBundle; break;
		case 1: OutId = GearId(Pick(Tools, UE_ARRAY_COUNT(Tools)), R.RandRange(0, 1)); break;
		case 2: OutId = 0; OutCount = 40; break;
		case 3: OutId = GearId(K_Cape, R.RandRange(0, NumCapeColors - 1)); break;
		case 4: OutId = GearId(Pick(AnyGear, UE_ARRAY_COUNT(AnyGear)), 2); break;
		case 5: OutId = GearId(K_Ring, R.RandRange(0, NumRings - 1)); break;
		case 6: OutId = GearId(K_Amulet, R.RandRange(0, NumAmulets - 1)); break;
		case 7: OutId = GearId(Pick(AnyGear, UE_ARRAY_COUNT(AnyGear)), 3); break;
		default: OutId = GearId(Pick(AnyGear, UE_ARRAY_COUNT(AnyGear)), 4); break;
		}
		return Row;
	}
}
