#pragma once

#include "CoreMinimal.h"
#include "SkillComponent.h"
#include "JeebItemData.h"

// ===================================================================================================
// Everything about items: ids, names, stack sizes, damage and defence numbers, prices, smithing recipes,
// rings and amulets. Shared by the character, the shops and the HUD. The NUMBERS AND TEXT live in the spreadsheets in
// Content/Data (see JeebItemData.h); this file holds the ids and the rules that use them.
//
// TIERS (weakest to strongest; redone 2026-10-30): Tin, Iron, Bronze, Steel, Silver, Gold, then one LEGENDARY tier per
// class, all equally strong: Onyx (knights), Emerald (trolls), Diamond (wizards). Only knights exist so far, so Emerald and
// Diamond gear can be found and sold but not worn yet. Gear needs a skill level to equip (EquipLevel). Save version 2
// moved old ids (Wood, Bronze, Iron, Steel, Diamond) onto these (UJeebSave::Migrate).
// ===================================================================================================
namespace JeebItems
{
	constexpr int32 NumSlots = 24;      // inventory slots (the side panel is 5 x 5: ingots in a fixed top-left cell, then these)
	constexpr int32 NumHotbar = 5;      // weapons and tools you carry ready
	constexpr int32 NumTiers = 9;
	enum ETier : int32 { T_Tin, T_Iron, T_Bronze, T_Steel, T_Silver, T_Gold, T_Onyx, T_Emerald, T_Diamond };
	constexpr int32 MaxPotions = 50;    // potion ids 950..999
	constexpr int32 MaxVariants = 10;   // rings, amulets and capes: the last digit of the id
	inline const FTierRow& TierRow(int32 T) { return Tables().Tiers[FMath::Clamp(T, 0, NumTiers - 1)]; }
	// Which class may use a tier: 0 everyone, 1 knights (Onyx), 2 trolls (Emerald), 3 wizards (Diamond)
	inline int32 TierClass(int32 T) { return TierRow(T).Class; }
	inline bool KnightCanUseTier(int32 T) { const int32 C = TierClass(T); return C == 0 || C == 1; }
	// The skill level needed to wear or wield a tier
	inline int32 EquipLevel(int32 T) { return TierRow(T).EquipLevel; }
	constexpr int32 NumTiersKnight = 7;   // the tiers a knight can use (Tin to Onyx): what the smithy and the dictionary list

	// What kind of thing a piece of gear is
	enum EKind : int32
	{
		K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Broadsword, K_BattleAxe, K_Katana, K_Wand, K_Staff, K_Bow, K_Rod,   // the weapon bar
		K_Shield,
		K_Helmet, K_Chest, K_Shoulders, K_Gloves, K_Boots,
		K_Cape,
		K_Ring, K_Amulet,
		K_Crest,          // a brush along the helm's crest: worn in a miscellaneous slot like a ring or amulet
		K_Count
	};

	// Ids: 0 = empty; 1..99 = materials and fish (EItemType + 1); 900..904 = arrows by tier; 950..953 potions;
	// 1000 + kind * 100 + variant = gear (shields also carry a style: 1000 + kind * 100 + style * 10 + tier).
	// The variant is the tier (0-8, see ETier) for weapons and armour, the colour for capes, the particular piece for rings and amulets.
	constexpr int32 IdArrowBase = 900;
	constexpr int32 IdGearBase = 1000;

	inline int32 MaterialId(EItemType T) { return static_cast<int32>(T) + 1; }
	inline bool IsMaterial(int32 Id) { return Id >= 1 && Id < 100; }
	inline EItemType MaterialType(int32 Id) { return static_cast<EItemType>(Id - 1); }
	inline int32 ArrowId(int32 Tier) { return IdArrowBase + Tier; }
	inline bool IsArrow(int32 Id) { return Id >= IdArrowBase && Id < IdArrowBase + NumTiers; }
	inline int32 ArrowTier(int32 Id) { return Id - IdArrowBase; }
	// Potions: 950 + the row in Potions.csv. The first four rows are swiftness, healing, vigour and radar (the starting pack
	// and the practice pack name them by these numbers).
	constexpr int32 IdPotionBase = 950;
	inline int32 NumPotions() { return Tables().Potions.Num(); }
	enum EPotion : int32 { P_Speed = 0, P_Heal = 1, P_Vigour = 2, P_Radar = 3 };
	inline int32 PotionId(int32 Type) { return IdPotionBase + Type; }
	inline bool IsPotion(int32 Id) { return Id >= IdPotionBase && Id < IdPotionBase + NumPotions(); }
	// Any id in the potion range, even one the table no longer has (saves keep them, so a short table never loses potions)
	inline bool IsPotionRange(int32 Id) { return Id >= IdPotionBase && Id < IdPotionBase + MaxPotions; }
	inline int32 PotionType(int32 Id) { return Id - IdPotionBase; }
	inline const FPotionRow& PotionRow(int32 Type) { static const FPotionRow None; const TArray<FPotionRow>& P = Tables().Potions; return P.IsValidIndex(Type) ? P[Type] : None; }
	inline const TCHAR* PotionName(int32 Type) { return *PotionRow(Type).Name; }
	inline const TCHAR* PotionText(int32 Type) { return *PotionRow(Type).Text; }
	inline float PotionCooldown(int32 Type) { return PotionRow(Type).Cooldown; }
	inline int32 PotionPrice(int32 Type) { return PotionRow(Type).Price; }
	inline int32 GearId(int32 Kind, int32 Variant) { return IdGearBase + Kind * 100 + Variant; }
	inline bool IsGear(int32 Id) { return Id >= IdGearBase; }
	inline int32 KindOf(int32 Id) { return (Id - IdGearBase) / 100; }
	inline int32 VariantOf(int32 Id) { return (Id - IdGearBase) % 10; }   // the tier (or cape colour, or which ring)
	// Shields come in three styles: 0 kite (the all-rounder), 1 round (a smaller guard that recovers twice as fast, and you move
	// quicker behind it), 2 tower (a guard half as big again, but slower to move behind)
	constexpr int32 NumShieldStyles = 3;
	inline int32 StyleOf(int32 Id) { return ((Id - IdGearBase) % 100) / 10; }
	inline int32 ShieldId(int32 Style, int32 Tier) { return IdGearBase + 11 * 100 + Style * 10 + Tier; }   // 11 = K_Shield
	inline const FShieldStyleRow& ShieldStyleRow(int32 Style) { return Tables().ShieldStyles[FMath::Clamp(Style, 0, NumShieldStyles - 1)]; }
	inline const TCHAR* ShieldStyleName(int32 Style) { return *ShieldStyleRow(Style).Name; }
	inline float ShieldGuardMul(int32 Style) { return ShieldStyleRow(Style).GuardMul; }
	inline float ShieldMoveMul(int32 Style) { return ShieldStyleRow(Style).MoveMul; }
	inline float ShieldRecoverDelay(int32 Style) { return ShieldStyleRow(Style).RecoverDelay; }
	inline int32 TierOf(int32 Id) { return IsArrow(Id) ? ArrowTier(Id) : VariantOf(Id); }

	inline bool IsWeaponKind(int32 K) { return K >= K_Sword && K <= K_Rod; }
	inline bool IsArmorKind(int32 K) { return K >= K_Helmet && K <= K_Boots; }
	inline bool IsAccessoryKind(int32 K) { return K == K_Ring || K == K_Amulet || K == K_Crest; }

	inline const FKindRow& KindRow(int32 K) { return Tables().Kinds[FMath::Clamp(K, 0, K_Count - 1)]; }
	// Two-handed weapons cannot be used with a shield
	inline bool AllowsShield(int32 K) { return !KindRow(K).bTwoHanded; }

	inline const FMaterialRow& MaterialRow(EItemType T) { const TArray<FMaterialRow>& M = Tables().Materials; return M[FMath::Clamp(static_cast<int32>(T), 0, M.Num() - 1)]; }
	inline int32 StackSize(int32 Id)
	{
		if (IsArrow(Id)) return 30;
		if (IsPotionRange(Id)) return IsPotion(Id) ? PotionRow(PotionType(Id)).StackSize : 10;
		if (!IsMaterial(Id)) return 1;
		return MaterialRow(MaterialType(Id)).StackSize;
	}

	// Ingots paid by the store for one unit
	inline int32 SellValue(EItemType T) { return MaterialRow(T).SellValue; }
	inline const TCHAR* MaterialName(EItemType T) { return *MaterialRow(T).Name; }
	inline bool IsOreOrGem(EItemType T) { const int32 G = MaterialRow(T).Group; return G == MG_Ore || G == MG_Gem; }
	inline bool IsGem(EItemType T) { return MaterialRow(T).Group == MG_Gem; }
	// The colour of an ore's lumps or a gem's crystals (rocks, icons, dropped stacks)
	inline FLinearColor OreColor(EItemType T) { return MaterialRow(T).Color; }

	// ---------------------------------------------------------------- names
	inline const TCHAR* TierName(int32 T) { return *TierRow(T).Name; }
	inline const TCHAR* KindName(int32 K) { return *KindRow(K).Name; }
	// The cape shows the PvP rank (2026-10-30): its variant is the rank, which uses the tier ladder (Tin ... Gold, then the
	// class's legendary at the top), and its colour is that tier's CapeColor. It is locked to the character: the server sets
	// it from the rank (USkillComponent::RefreshCape) and it is never taken off, dropped, sold or bought.
	inline FLinearColor CapeColor(int32 Rank) { return TierRow(Rank).CapeColor; }
	inline int32 CapeRank(const USkillComponent* S) { return T_Tin; }   // the player's PvP rank: PvP is not built yet, so every knight is Tin

	// Rings and amulets (and the helm brush)
	inline int32 NumRings() { return Tables().Rings.Num(); }
	inline int32 NumAmulets() { return Tables().Amulets.Num(); }
	inline const FAccessoryRow& AccessoryRow(int32 K, int32 V)
	{
		static const FAccessoryRow None;
		const FItemTables& T = Tables();
		if (K == K_Crest) return T.Crest;
		const TArray<FAccessoryRow>& A = (K == K_Ring) ? T.Rings : T.Amulets;
		return A.IsValidIndex(V) ? A[V] : None;
	}
	inline const TCHAR* AccessoryName(int32 K, int32 V) { return *AccessoryRow(K, V).Name; }

	inline FString ItemName(int32 Id)
	{
		if (IsArrow(Id)) return FString::Printf(TEXT("%s Arrows"), TierName(ArrowTier(Id)));
		if (IsPotion(Id)) return PotionName(PotionType(Id));
		if (!IsGear(Id)) return FString();
		const int32 K = KindOf(Id), V = VariantOf(Id);
		if (K == K_Cape) return FString::Printf(TEXT("%s Cape"), TierName(V));
		if (K == K_Shield) return FString::Printf(TEXT("%s %s"), TierName(V), ShieldStyleName(StyleOf(Id)));
		if (IsAccessoryKind(K)) return AccessoryName(K, V);
		return FString::Printf(TEXT("%s %s"), TierName(V), KindName(K));
	}

	// ---------------------------------------------------------------- tier colours (Tiers.csv)
	// Roles follow EArmorRole: Plate, Under, Trim, Cloth, Accent, Leather, BootBody.
	inline FLinearColor Palette(int32 T, int32 Role) { return TierRow(T).Palette[FMath::Clamp(Role, 0, 6)]; }
	inline FLinearColor GemColor(int32 T) { return TierRow(T).Gem; }
	inline bool TierIsMetal(int32 T) { return T >= 0; }   // (every tier is metal or gem since the redo; Wood is gone)

	// ---------------------------------------------------------------- weapon and armour numbers (Kinds.csv, Tiers.csv)
	// Damage is a range: every hit is a random whole number from Min to Max.
	struct FWeaponBase { int32 Min, Max; float Range; float Cooldown; };
	// Numbers are for TIN, the lowest tier; better tiers add steps (TierSteps).
	inline FWeaponBase WeaponBase(int32 K) { const FKindRow& R = KindRow(K); return { R.DamageMin, R.DamageMax, R.Range, R.Cooldown }; }
	inline float TierMult(int32 T) { return TierRow(T).DefenceMult; }
	// How many damage steps a tier adds (narrow gaps, so skill can beat a tier of gear; the three legendaries are equal)
	inline float TierSteps(int32 T) { return TierRow(T).DamageSteps; }
	// Each step adds to both ends of the range: a quarter of the weapon's tin range
	// (sword: tin 8-12, iron 12-16, bronze 16-20, steel 20-24, silver 25-29, gold 30-34, legendary 35-39).
	inline int32 TierStep(int32 K)
	{
		const FWeaponBase B = WeaponBase(K);
		return FMath::Max(1, FMath::RoundToInt((B.Min + B.Max) * 0.25f));
	}
	// Plain damage range of a weapon (before rings, amulets and skill). Bows add the arrow tier fully and the bow tier by half.
	inline void WeaponDamage(int32 K, int32 T, int32& OutMin, int32& OutMax, int32 ArrowTierForBow = 0)
	{
		const FWeaponBase B = WeaponBase(K);
		const float Steps = (K == K_Bow) ? (TierSteps(ArrowTierForBow) + 0.5f * TierSteps(T)) : TierSteps(T);
		const int32 Add = FMath::RoundToInt(TierStep(K) * Steps);
		OutMin = B.Min + Add;
		OutMax = B.Max + Add;
	}
	// Which skill a weapon trains and is boosted by
	inline ESkillType CombatSkillFor(int32 K)
	{
		if (K == K_Bow) return ESkillType::Archery;
		if (K == K_Wand || K == K_Staff) return ESkillType::Magic;
		return ESkillType::Attack;
	}
	// Defence points of a shield (flat damage blocked) or armour piece (adds to the total)
	inline int32 DefenseOf(int32 K, int32 T) { return FMath::RoundToInt(KindRow(K).Defence * TierMult(T)); }
	// A shield's guard (the damage it can stop while raised): its Defence, scaled by its style
	inline int32 ShieldGuard(int32 ShieldItemId)
	{
		if (!IsGear(ShieldItemId) || KindOf(ShieldItemId) != K_Shield) return 0;
		return FMath::RoundToInt(DefenseOf(K_Shield, VariantOf(ShieldItemId)) * ShieldGuardMul(StyleOf(ShieldItemId)));
	}
	static_assert(K_Shield == 11, "ShieldId assumes the shield is kind 11");
	// The guard pool a raised shield blocks with (2026-10-31 balance pass): three times its Defence, so a shield takes about three
	// sword jabs before it breaks (its Defence alone was one jab). Its Defence (armour totals, badges) is unchanged.
	constexpr float GuardPoolMul = 3.f;
	inline float GuardPool(int32 ShieldItemId) { return float(ShieldGuard(ShieldItemId)) * GuardPoolMul; }
	// Boots: stamina comes back faster and drains slower
	inline float BootStaminaMult(int32 T) { return TierRow(T).BootStamina; }
	// Tools make gathering faster: 1.0 for a tin tool, down to 0.6 for a legendary one
	inline float ToolTimeMult(int32 T) { const float Top = TierSteps(T_Onyx); return Top > 0.f ? 1.f - 0.4f * TierSteps(T) / Top : 1.f; }

	// ---------------------------------------------------------------- rings and amulets (Accessories.csv)
	inline FMods AccessoryMods(int32 K, int32 V) { return AccessoryRow(K, V).Mods; }
	inline const TCHAR* AccessoryText(int32 K, int32 V) { return *AccessoryRow(K, V).Text; }
	// Turn a plain damage range into the range with rings and amulets
	inline void ApplyAttackMods(const FMods& M, int32& Min, int32& Max)
	{
		Min = FMath::Max(1, FMath::RoundToInt(Min * M.AttackMul) + M.FloorDelta);
		Max = FMath::Max(Min, FMath::RoundToInt(Max * M.AttackMul) + M.CeilDelta);
	}

	// ---------------------------------------------------------------- prices (ingots)
	// The store sells tin, iron and bronze gear. Steel and better have to be smithed.
	inline int32 BaseBuyPrice(int32 K) { return KindRow(K).BuyPrice; }
	inline float TierPriceMult(int32 T) { return TierRow(T).PriceMult; }
	inline int32 AccessoryPrice(int32 K, int32 V) { return AccessoryRow(K, V).Price; }
	constexpr int32 StoreMaxTier = T_Bronze;   // the store sells tin, iron and bronze
	inline int32 BuyPrice(int32 K, int32 Variant)
	{
		if (IsAccessoryKind(K)) return AccessoryPrice(K, Variant);
		if (K == K_Cape) return 0;   // (never sold: it shows your PvP rank)
		return FMath::Max(1, FMath::RoundToInt(BaseBuyPrice(K) * TierPriceMult(Variant)));
	}
	constexpr int32 ArrowBundle = 10;
	inline int32 ArrowBundlePrice(int32 T) { return FMath::Max(2, FMath::RoundToInt(8.f * TierPriceMult(T))); }

	// ---------------------------------------------------------------- smithing (Kinds.csv Mass and SmithExtraLevel, Tiers.csv)
	// How much raw material a piece takes ("mass"), and which materials each tier uses (2026-10-30):
	//   Tin: tin ore.  Iron: iron ore.  Bronze: copper + tin.  Steel: iron ore + coal.  Silver: silver ore.  Gold: gold ore.
	//   Onyx / Emerald / Diamond (the legendaries): their gem + gold ore, at a very high Smithing level.
	inline bool CanSmith(int32 K) { return IsWeaponKind(K) || K == K_Shield || IsArmorKind(K); }
	inline int32 Mass(int32 K) { return KindRow(K).Mass; }
	// A recipe: up to two materials (Num 0 = unused), the Smithing level it needs and the XP it gives
	struct FRecipe { EItemType Mat[2] = { EItemType::TinOre, EItemType::TinOre }; int32 Num[2] = { 0, 0 }; int32 Level = 1; int32 XP = 0; };
	inline FRecipe SmithRecipe(int32 K, int32 T, bool bArrows = false)
	{
		FRecipe R;
		const FTierRow& TR = TierRow(T);
		const int32 M = bArrows ? 2 : Mass(K);   // arrows: a bundle of 10 costs 2 mass
		for (int32 i = 0; i < 2; ++i)
		{
			R.Mat[i] = TR.Mat[i];
			R.Num[i] = TR.Share[i] > 0.f ? FMath::Max(1, FMath::CeilToInt(M * TR.Share[i] - 0.001f)) : 0;
		}
		R.Level = TR.SmithLevel + ((T == T_Tin || bArrows) ? 0 : KindRow(K).SmithExtraLevel);
		R.XP = M * TR.SmithXP;
		return R;
	}

	// Levels needed to gather things (Materials.csv GatherLevel), and Fishing XP per catch
	inline int32 MiningLevelFor(EItemType T) { return MaterialRow(T).GatherLevel; }
	inline int32 WoodcuttingLevelFor(EItemType T) { return MaterialRow(T).GatherLevel; }
	inline int32 FishingLevelFor(EItemType T) { return MaterialRow(T).GatherLevel; }
	inline int32 FishXP(EItemType T) { return MaterialRow(T).FishXP; }

	// ---------------------------------------------------------------- equipping: level gates (2026-10-30)
	// Weapons need their combat skill (Attack; Archery for bows; Magic for wands and staffs); the gathering tools need their
	// gathering skill (axe Woodcutting, pickaxe Mining, fishing pole Fishing); armour and shields need Defence.
	inline ESkillType EquipSkill(int32 Kind)
	{
		switch (Kind)
		{
		case K_Axe:     return ESkillType::Woodcutting;
		case K_Pickaxe: return ESkillType::Mining;
		case K_Rod:     return ESkillType::Fishing;
		case K_Bow:     return ESkillType::Archery;
		case K_Wand: case K_Staff: return ESkillType::Magic;
		default:        return IsWeaponKind(Kind) ? ESkillType::Attack : ESkillType::Defense;
		}
	}
	inline const TCHAR* SkillNameOf(ESkillType S)
	{
		static const TCHAR* N[] = { TEXT("Mining"), TEXT("Woodcutting"), TEXT("Smithing"), TEXT("Fishing"), TEXT("Attack"), TEXT("Defence"), TEXT("Archery"), TEXT("Magic"), TEXT("Lockpicking") };
		return N[FMath::Clamp(static_cast<int32>(S), 0, int32(UE_ARRAY_COUNT(N)) - 1)];
	}
	// Can this player wear or wield this item? If not, OutWhy says why. (Capes, rings, amulets and the brush have no gate.)
	inline bool CanEquip(const USkillComponent* S, int32 Id, FString* OutWhy = nullptr)
	{
		if (!IsGear(Id)) return true;
		const int32 K = KindOf(Id);
		if (!IsWeaponKind(K) && !IsArmorKind(K) && K != K_Shield) return true;
		const int32 T = VariantOf(Id);
		if (!KnightCanUseTier(T))
		{
			if (OutWhy) *OutWhy = FString::Printf(TEXT("%s gear is made for %s. A knight can't use it."), TierName(T), TierClass(T) == 2 ? TEXT("trolls") : TEXT("wizards"));
			return false;
		}
		const ESkillType Sk = EquipSkill(K);
		const int32 Need = EquipLevel(T);
		if (S && S->GetLevel(Sk) < Need)
		{
			if (OutWhy) *OutWhy = FString::Printf(TEXT("You need %s level %d to use %s."), SkillNameOf(Sk), Need, *ItemName(Id));
			return false;
		}
		return true;
	}
	// ---------------------------------------------------------------- selling gear back
	inline int32 GearSellPrice(int32 Id)
	{
		if (IsArrow(Id)) return 1;
		if (!IsGear(Id)) return 0;
		return FMath::Max(1, BuyPrice(KindOf(Id), VariantOf(Id)) / 2);
	}

	// ---------------------------------------------------------------- what you carry adds up
	inline FMods CombinedMods(const USkillComponent* S)
	{
		FMods M;
		if (!S) return M;
		const int32 Ids[3] = { S->Worn[K_Ring], S->RingB, S->Worn[K_Amulet] };
		for (int32 Id : Ids)
		{
			if (Id == 0) continue;
			const FMods A = AccessoryMods(KindOf(Id), VariantOf(Id));
			M.AttackMul *= A.AttackMul;
			M.DefenseMul *= A.DefenseMul;
			M.CritChance += A.CritChance;
			M.FloorDelta += A.FloorDelta;
			M.CeilDelta += A.CeilDelta;
			M.bWild = M.bWild || A.bWild;
			M.MaxHealthBonus += A.MaxHealthBonus;
			M.SpeedMul *= A.SpeedMul;
		}
		return M;
	}
	// Armour total (shield counted only when bShield). Rings and amulets scale it.
	inline int32 TotalDefense(const USkillComponent* S, bool bShield)
	{
		if (!S) return 0;
		int32 D = 0;
		for (int32 K = K_Helmet; K <= K_Boots; ++K)
			if (S->Worn[K]) D += DefenseOf(K, VariantOf(S->Worn[K]));
		if (bShield && S->Worn[K_Shield]) D += ShieldGuard(S->Worn[K_Shield]);
		return FMath::RoundToInt(D * CombinedMods(S).DefenseMul);
	}
	// Full damage range: weapon, then rings and amulets, then + (skill level / 5) on both ends. Kind -1 = bare fists.
	inline void DamageRange(const USkillComponent* S, int32 Kind, int32 Tier, int32 ArrowTier, int32& Min, int32& Max)
	{
		if (Kind < 0) { Min = 1; Max = 3; }
		else WeaponDamage(Kind, Tier, Min, Max, ArrowTier);
		ApplyAttackMods(CombinedMods(S), Min, Max);
		const int32 Bonus = S ? S->GetLevel(CombatSkillFor(Kind < 0 ? K_Sword : Kind)) / 5 : 0;
		Min += Bonus;
		Max += Bonus;
	}
	// One actual hit: a random whole number in the range, x0.5-2 with the Emerald Necklace, then a chance of +50% (crit)
	inline int32 RollDamage(const USkillComponent* S, int32 Kind, int32 Tier, int32 ArrowTier, bool& bOutCrit, float Mul = 1.f)
	{
		int32 Min, Max;
		DamageRange(S, Kind, Tier, ArrowTier, Min, Max);
		const FMods M = CombinedMods(S);
		float D = (float)FMath::RandRange(Min, Max);
		if (M.bWild) D *= FMath::FRandRange(0.5f, 2.f);
		bOutCrit = FMath::FRand() < M.CritChance;
		if (bOutCrit) D *= (1.f + M.CritBonus);
		return FMath::Max(1, FMath::RoundToInt(D * Mul));
	}
	// Damage range of the weapon in a hotbar slot, including rings and amulets
	inline bool HotbarDamage(const USkillComponent* S, int32 H, int32& Min, int32& Max)
	{
		Min = Max = 0;
		if (!S || H < 0 || H > 4 || S->Hotbar[H] == 0) return false;
		const int32 Id = S->Hotbar[H];
		DamageRange(S, KindOf(Id), VariantOf(Id), FMath::Max(0, S->BestArrowTier()), Min, Max);
		return true;
	}
	inline float StaminaMult(const USkillComponent* S)
	{
		return (S && S->Worn[K_Boots]) ? BootStaminaMult(VariantOf(S->Worn[K_Boots])) : 1.f;
	}

	// ---------------------------------------------------------------- dictionary text (Kinds.csv and Tiers.csv Blurb)
	inline const TCHAR* KindBlurb(int32 K) { return *KindRow(K).Blurb; }
	inline const TCHAR* TierBlurb(int32 T) { return *TierRow(T).Blurb; }
	// Short material text for a recipe, e.g. "6 Copper"
	inline FString RecipeText(const FRecipe& R)
	{
		FString S;
		auto Add = [&](int32 N, const TCHAR* Name) { if (N > 0) { if (!S.IsEmpty()) S += TEXT(", "); S += FString::Printf(TEXT("%d %s"), N, Name); } };
		for (int32 i = 0; i < 2; ++i) Add(R.Num[i], MaterialName(R.Mat[i]));
		return S;
	}
}
