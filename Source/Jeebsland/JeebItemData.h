#pragma once

#include "CoreMinimal.h"
#include "SkillComponent.h"

// ===================================================================================================
// The item tables: every number and line of text about items lives in spreadsheet files (CSV: open them in Excel or Google
// Sheets) in Content/Data/, read once when the game starts. Inventory.h answers every question about items from them.
//   Tiers.csv         the nine ranks (Tin ... Diamond): levels, damage and defence steps, prices, smithing, colours
//   Kinds.csv         each kind of gear (sword, helmet, ...): damage, reach, defence, price, smithing mass, description
//   Materials.csv     ores, gems, logs, fish, animal drops: names, stack sizes, prices, gathering levels
//   Potions.csv       potions: effect, strength, cooldown, price, colour, text
//   Accessories.csv   rings, amulets and the helm brush: their effects
//   ShieldStyles.csv  kite, round, tower
// Each file says at its top which rows must stay as they are (saves remember items by number). Change a file, then restart the
// game (and the world servers): the server's copy decides everything, so a world uses its own files; players' copies only
// change what their screens show. A file that cannot be read keeps the game going with plain defaults and writes the problem
// to the log ("Jeebsland items:").
// ===================================================================================================
namespace JeebItems
{
	// What rings, amulets and the brush change
	struct FMods
	{
		float AttackMul = 1.f, DefenseMul = 1.f;
		float CritChance = 0.f, CritBonus = 0.5f;   // chance, and extra damage on a crit
		int32 FloorDelta = 0, CeilDelta = 0;        // moves the lowest / highest damage
		bool bWild = false;                         // each hit is multiplied by a random 0.5 - 2.0
		float SpeedMul = 1.f;                       // running and walking speed
		int32 MaxHealthBonus = 0;
	};

	struct FTierRow
	{
		FString Name, Blurb;
		int32 Class = 0;                 // 0 everyone, 1 knights, 2 trolls, 3 wizards
		int32 EquipLevel = 1;
		float DefenceMult = 1.f, DamageSteps = 0.f, BootStamina = 1.f, PriceMult = 1.f;
		int32 SmithLevel = 1, SmithXP = 0;
		EItemType Mat[2] = { EItemType::TinOre, EItemType::TinOre };
		float Share[2] = { 0.f, 0.f };   // share of the piece's mass (0 = unused)
		FLinearColor Gem = FLinearColor::White;
		FLinearColor Palette[7];         // EArmorRole order: Plate, Under, Trim, Cloth, Accent, Leather, BootBody
		FLinearColor CapeColor = FLinearColor(0.5f, 0.5f, 0.5f);   // the cape of a player at this PvP rank
	};
	struct FKindRow
	{
		FString Name, Blurb;
		int32 DamageMin = 1, DamageMax = 2;
		float Range = 200.f, Cooldown = 1.f;
		int32 Defence = 0, BuyPrice = 0, Mass = 0, SmithExtraLevel = 0;
		bool bTwoHanded = false;
	};
	enum EMaterialGroup : int32 { MG_Ore, MG_Gem, MG_Wood, MG_Fish, MG_Animal };
	struct FMaterialRow
	{
		FString Name;
		int32 Group = MG_Animal;
		int32 StackSize = 5, SellValue = 1;
		FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f);
		int32 GatherLevel = 1, FishXP = 8;
	};
	enum EPotionEffect : int32 { PE_None, PE_Speed, PE_Heal, PE_Vigour, PE_Radar };
	struct FPotionRow
	{
		FString Name, Text, Shop;
		int32 Effect = PE_None;
		float Amount = 1.f, Seconds = 0.f, Cooldown = 60.f;
		int32 Price = 10, StackSize = 10;
		FLinearColor Color = FLinearColor::White;
	};
	struct FAccessoryRow
	{
		FString Name, Text;
		int32 Price = 100;
		FMods Mods;
		FLinearColor Band = FLinearColor::White, Gem = FLinearColor::White;
	};
	struct FShieldStyleRow { FString Name, Note; float GuardMul = 1.f, MoveMul = 1.f, RecoverDelay = 10.f; };

	struct FItemTables
	{
		TArray<FTierRow> Tiers;          // always NumTiers rows, in ETier order
		TArray<FKindRow> Kinds;          // always K_Count rows, in EKind order
		TArray<FMaterialRow> Materials;  // always one row per EItemType, in its order
		TArray<FPotionRow> Potions;      // row = potion number (id 950 + row)
		TArray<FAccessoryRow> Rings, Amulets;
		FAccessoryRow Crest;
		TArray<FShieldStyleRow> ShieldStyles;   // always 3 rows (kite, round, tower)
	};

	// The tables (read from Content/Data the first time they are needed)
	const FItemTables& Tables();
}
