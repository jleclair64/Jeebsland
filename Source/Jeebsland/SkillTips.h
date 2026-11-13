#pragma once
// What a skill does at its level, and its progress, shown when a skill is hovered in the skills windows (owner, 2026-11-12).
// A line that starts with TipBarMark is drawn by AKnightHUD::DrawTip as a green progress bar (the rest of the line is the fraction).
#include "CoreMinimal.h"
#include "SkillComponent.h"
#include "KnightCharacter.h"
#include "Inventory.h"

#define TipBarMark TEXT("\u0001")

namespace JeebSkillTip
{
	inline FString Secs(float S)
	{
		FString F = FString::Printf(TEXT("%.2f"), S);
		while (F.EndsWith(TEXT("0"))) F.LeftChopInline(1);
		if (F.EndsWith(TEXT("."))) F.LeftChopInline(1);
		return F;
	}
	inline FString Num(int32 N) { return FText::AsNumber(N).ToString(); }   // 7,775
	// The best tier this level lets you use, and the next one with the level it needs
	inline void TierLines(TArray<FString>& L, int32 Level, const TCHAR* What)
	{
		using namespace JeebItems;
		int32 Best = -1;
		for (int32 t = 0; t < NumTiers; ++t) if (TierRow(t).EquipLevel <= Level) Best = t;
		if (Best >= 0) L.Add(FString::Printf(TEXT("Can use: %s %s"), TierName(Best), What));
		for (int32 t = 0; t < NumTiers; ++t)
			if (TierRow(t).EquipLevel > Level) { L.Add(FString::Printf(TEXT("Next: %s at level %d"), TierName(t), TierRow(t).EquipLevel)); break; }
	}
	// What this skill gathers now, and the next thing it unlocks
	inline void GatherLines(TArray<FString>& L, int32 Level, int32 GroupA, int32 GroupB)
	{
		using namespace JeebItems;
		FString Can; int32 NextLv = 1000; FString Next;
		for (int32 i = 0; i < Tables().Materials.Num(); ++i)
		{
			const EItemType T = static_cast<EItemType>(i);
			if (T == EItemType::CopperOre || T == EItemType::Coal || T == EItemType::Onyx) continue;   // (retired: nothing holds them)
			const FMaterialRow& R = MaterialRow(T);
			if (R.Group != GroupA && R.Group != GroupB) continue;
			if (R.GatherLevel <= Level) { if (!Can.IsEmpty()) Can += TEXT(", "); Can += R.Name; }
			else if (R.GatherLevel < NextLv) { NextLv = R.GatherLevel; Next = R.Name; }
		}
		if (!Can.IsEmpty()) L.Add(FString::Printf(TEXT("Can gather: %s"), *Can));
		if (!Next.IsEmpty()) L.Add(FString::Printf(TEXT("Next: %s at level %d"), *Next, NextLv));
	}
}

inline TArray<FString> SkillTipLines(ESkillType T, int32 Level, int32 Into, int32 Need)
{
	using namespace JeebSkillTip;
	TArray<FString> L = { USkillComponent::SkillName(T), FString::Printf(TEXT("Level %d"), Level) };
	const int32 DamageBonus = Level / 5;   // (JeebItems::DamageRange: +1 damage per 5 levels of the weapon's skill)
	switch (T)
	{
	case ESkillType::Attack:
		L.Add(FString::Printf(TEXT("Melee damage: +%d"), DamageBonus));
		TierLines(L, Level, TEXT("weapons"));
		break;
	case ESkillType::Defense:
		L.Add(FString::Printf(TEXT("Health: %d"), 100 + (Level - 1) * 3));
		L.Add(FString::Printf(TEXT("Shield heals: %s per second"), *Secs(JeebItems::ShieldHeal(Level, 0))));
		TierLines(L, Level, TEXT("armour and shields"));
		break;
	case ESkillType::Archery:
		L.Add(FString::Printf(TEXT("Draw speed: %s seconds"), *Secs(AKnightCharacter::BowDrawSeconds(Level))));
		L.Add(FString::Printf(TEXT("Longbow draw: %s seconds"), *Secs(AKnightCharacter::LongbowDrawSeconds(Level))));
		L.Add(FString::Printf(TEXT("Arrow damage: +%d"), DamageBonus));
		TierLines(L, Level, TEXT("bows"));
		break;
	case ESkillType::Magic:
		L.Add(FString::Printf(TEXT("Magic damage: +%d"), DamageBonus));
		TierLines(L, Level, TEXT("wands and staffs"));
		break;
	case ESkillType::Mining:
		GatherLines(L, Level, JeebItems::MG_Ore, JeebItems::MG_Gem);
		TierLines(L, Level, TEXT("pickaxes"));
		break;
	case ESkillType::Woodcutting:
		GatherLines(L, Level, JeebItems::MG_Wood, JeebItems::MG_Wood);
		TierLines(L, Level, TEXT("axes"));
		break;
	case ESkillType::Fishing:
		GatherLines(L, Level, JeebItems::MG_Fish, JeebItems::MG_Fish);
		TierLines(L, Level, TEXT("fishing rods"));
		break;
	case ESkillType::Smithing:
	{
		using namespace JeebItems;
		int32 Best = -1;
		for (int32 t = 0; t < NumTiers; ++t) if (TierRow(t).SmithLevel <= Level) Best = t;
		if (Best >= 0) L.Add(FString::Printf(TEXT("Can smith: up to %s"), TierName(Best)));
		for (int32 t = 0; t < NumTiers; ++t)
			if (TierRow(t).SmithLevel > Level) { L.Add(FString::Printf(TEXT("Next: %s at level %d"), TierName(t), TierRow(t).SmithLevel)); break; }
		break;
	}
	case ESkillType::Climbing:
		L.Add(FString::Printf(TEXT("Climbing stamina: %s seconds"), *Secs(AKnightCharacter::ClimbSeconds(Level))));
		break;
	case ESkillType::Lockpicking:
		L.Add(FString::Printf(TEXT("Can pick locks up to level %d"), Level));
		break;
	default: break;
	}
	const float F = Need > 0 ? FMath::Clamp((float)Into / (float)Need, 0.f, 1.f) : 1.f;
	L.Add(FString(TipBarMark) + FString::SanitizeFloat(F));
	L.Add(FString::Printf(TEXT("%s / %s XP"), *Num(Into), *Num(Need)));
	return L;
}
