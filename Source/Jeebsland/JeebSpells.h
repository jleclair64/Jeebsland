#pragma once

#include "CoreMinimal.h"

class USkillComponent;

// The wizard's spells (owner, 2026-11-22: "make the F key switch spells, similar to how the knight switches arrows", a second spell in
// another colour, and a spell book of learned and upgraded spells to equip). One row per spell in Content/Data/Spells.csv, read once
// on every machine (the bolt's look comes from its row, so every player sees the right colour). Rows are numbered by their order in
// the file: never reorder or delete rows (saves and bolts name spells by row), only add at the end.
//
// The numbers are shares of the wand's or staff's own: DamageMul scales the weapon's rolled damage, SpeedMul the bolt's speed
// (Tuning.ini [Wizard] StaffBoltSpeed / WandBoltSpeed), CostMul its magic cost (MagicTapCost / MagicFullCost). Each upgrade level past
// the first adds PerLevel to DamageMul (level 3 of a 0.1 spell: +20%).
namespace JeebSpells
{
	enum ELook : uint8 { Look_Bolt = 0, Look_Fire = 1 };

	struct FSpell
	{
		FString Key, Name, Blurb;
		FLinearColor Color = FLinearColor(0.1f, 0.45f, 1.f);
		float DamageMul = 1.f, SpeedMul = 1.f, CostMul = 1.f, Size = 1.f, PerLevel = 0.1f;
		int32 LevelNeeded = 1, MaxLevel = 5;
		uint8 Look = Look_Bolt;
	};


	const TArray<FSpell>& All();
	int32 Num();
	const FSpell& Get(int32 Row);   // (a bad row: row 0)
	// The damage share at an upgrade level (1 = as learned)
	float DamageMul(int32 Row, int32 Level);
}
