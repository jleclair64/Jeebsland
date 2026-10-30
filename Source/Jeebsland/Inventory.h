#pragma once

#include "CoreMinimal.h"
#include "SkillComponent.h"

// ===================================================================================================
// Everything about items: ids, names, stack sizes, damage and defence numbers, prices, smithing recipes,
// rings and amulets. Header only; shared by the character, the shops and the HUD.
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
	// Which class may use a tier: 0 everyone, 1 knights (Onyx), 2 trolls (Emerald), 3 wizards (Diamond)
	inline int32 TierClass(int32 T) { return T == T_Onyx ? 1 : T == T_Emerald ? 2 : T == T_Diamond ? 3 : 0; }
	inline bool KnightCanUseTier(int32 T) { const int32 C = TierClass(T); return C == 0 || C == 1; }
	// The skill level needed to wear or wield a tier
	inline int32 EquipLevel(int32 T) { static const int32 L[NumTiers] = { 1, 5, 10, 20, 30, 40, 50, 50, 50 }; return L[FMath::Clamp(T, 0, NumTiers - 1)]; }
	constexpr int32 NumTiersKnight = 7;   // the tiers a knight can use (Tin to Onyx): what the smithy and the dictionary list
	constexpr int32 NumCapeColors = 7;

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
	// Potions: 950 green (speed), 951 red (healing), 952 purple (vigour: a bigger health bar for a while)
	constexpr int32 IdPotionBase = 950;
	constexpr int32 NumPotions = 4;
	enum EPotion : int32 { P_Speed = 0, P_Heal = 1, P_Vigour = 2, P_Radar = 3 };
	constexpr float RadarSeconds = 120.f;   // how long the radar potion shows enemies on the minimap
	inline int32 PotionId(int32 Type) { return IdPotionBase + Type; }
	inline bool IsPotion(int32 Id) { return Id >= IdPotionBase && Id < IdPotionBase + NumPotions; }
	inline int32 PotionType(int32 Id) { return Id - IdPotionBase; }
	inline const TCHAR* PotionName(int32 Type) { static const TCHAR* N[NumPotions] = { TEXT("Swiftness Potion"), TEXT("Healing Potion"), TEXT("Vigour Potion"), TEXT("Radar Potion") }; return N[FMath::Clamp(Type, 0, NumPotions - 1)]; }
	inline const TCHAR* PotionText(int32 Type) { static const TCHAR* N[NumPotions] = { TEXT("You run 50% faster for 8 seconds. Ready again after 1 minute."), TEXT("Heals you to full at once. Ready again after 2 minutes."), TEXT("Your health bar grows by 50% for 1 minute (the extra comes full). Ready again after 1 minute."), TEXT("Enemies and other players near you show as red dots on your minimap for 2 minutes. Ready again after 3 minutes.") }; return N[FMath::Clamp(Type, 0, NumPotions - 1)]; }
	inline float PotionCooldown(int32 Type) { static const float C[NumPotions] = { 60.f, 120.f, 60.f, 180.f }; return C[FMath::Clamp(Type, 0, NumPotions - 1)]; }
	inline int32 PotionPrice(int32 Type) { static const int32 P[NumPotions] = { 25, 40, 35, 60 }; return P[FMath::Clamp(Type, 0, NumPotions - 1)]; }
	inline int32 GearId(int32 Kind, int32 Variant) { return IdGearBase + Kind * 100 + Variant; }
	inline bool IsGear(int32 Id) { return Id >= IdGearBase; }
	inline int32 KindOf(int32 Id) { return (Id - IdGearBase) / 100; }
	inline int32 VariantOf(int32 Id) { return (Id - IdGearBase) % 10; }   // the tier (or cape colour, or which ring)
	// Shields come in three styles: 0 kite (the all-rounder), 1 round (a smaller guard that recovers twice as fast, and you move
	// quicker behind it), 2 tower (a guard half as big again, but slower to move behind)
	constexpr int32 NumShieldStyles = 3;
	inline int32 StyleOf(int32 Id) { return ((Id - IdGearBase) % 100) / 10; }
	inline int32 ShieldId(int32 Style, int32 Tier) { return IdGearBase + 11 * 100 + Style * 10 + Tier; }   // 11 = K_Shield
	inline const TCHAR* ShieldStyleName(int32 Style) { static const TCHAR* N[NumShieldStyles] = { TEXT("Kite Shield"), TEXT("Round Shield"), TEXT("Tower Shield") }; return N[FMath::Clamp(Style, 0, NumShieldStyles - 1)]; }
	inline float ShieldGuardMul(int32 Style) { static const float M[NumShieldStyles] = { 1.f, 0.75f, 1.5f }; return M[FMath::Clamp(Style, 0, NumShieldStyles - 1)]; }
	inline float ShieldMoveMul(int32 Style) { static const float M[NumShieldStyles] = { 1.f, 1.15f, 0.8f }; return M[FMath::Clamp(Style, 0, NumShieldStyles - 1)]; }
	inline float ShieldRecoverDelay(int32 Style) { static const float D[NumShieldStyles] = { 10.f, 5.f, 10.f }; return D[FMath::Clamp(Style, 0, NumShieldStyles - 1)]; }
	inline int32 TierOf(int32 Id) { return IsArrow(Id) ? ArrowTier(Id) : VariantOf(Id); }

	inline bool IsWeaponKind(int32 K) { return K >= K_Sword && K <= K_Rod; }
	inline bool IsArmorKind(int32 K) { return K >= K_Helmet && K <= K_Boots; }
	inline bool IsAccessoryKind(int32 K) { return K == K_Ring || K == K_Amulet || K == K_Crest; }

	// Two-handed weapons cannot be used with a shield
	inline bool AllowsShield(int32 Kind) { return !(Kind == K_Broadsword || Kind == K_BattleAxe || Kind == K_Katana || Kind == K_Staff); }

	inline int32 StackSize(int32 Id)
	{
		if (IsArrow(Id)) return 30;
		if (IsPotion(Id)) return 10;
		if (!IsMaterial(Id)) return 1;
		switch (MaterialType(Id))
		{
		case EItemType::CopperOre: return 10;
		case EItemType::Coal:      return 10;
		case EItemType::TinOre:    return 10;
		case EItemType::IronOre:   return 10;
		case EItemType::SilverOre: return 10;
		case EItemType::GoldOre:   return 10;
		case EItemType::Diamond:   return 5;
		case EItemType::Onyx:      return 5;
		case EItemType::Emerald:   return 5;
		case EItemType::Perch:     return 10;
		case EItemType::Trout:     return 10;
		case EItemType::GoldenCarp:return 5;
		default:                   return 5; // all wood, pork, hides and bones: 5 to a slot
		}
	}

	// Ingots paid by the store for one unit
	inline int32 SellValue(EItemType T)
	{
		switch (T)
		{
		case EItemType::CopperOre:   return 3;
		case EItemType::Coal:        return 5;
		case EItemType::Diamond:     return 60;
		case EItemType::TinOre:      return 2;
		case EItemType::IronOre:     return 6;
		case EItemType::SilverOre:   return 15;
		case EItemType::GoldOre:     return 25;
		case EItemType::Onyx:        return 60;
		case EItemType::Emerald:     return 60;
		case EItemType::Logs:        return 2;   // ordinary trees
		case EItemType::PineLogs:    return 4;
		case EItemType::RedwoodLogs: return 8;
		case EItemType::Bacon:       return 6;
		case EItemType::Perch:       return 4;
		case EItemType::Trout:       return 10;
				case EItemType::GoldenCarp:  return 35;
		case EItemType::Hide:        return 9;
		case EItemType::Bones:       return 4;
		}
		return 1;
	}
	inline const TCHAR* MaterialName(EItemType T)
	{
		static const TCHAR* N[] = { TEXT("Copper Ore"), TEXT("Coal"), TEXT("Logs"), TEXT("Diamond"), TEXT("Raw Pork"), TEXT("Pine Logs"), TEXT("Redwood Logs"), TEXT("Perch"), TEXT("Trout"), TEXT("Golden Carp"), TEXT("Beast Hide"), TEXT("Bones"),
			TEXT("Tin Ore"), TEXT("Iron Ore"), TEXT("Silver Ore"), TEXT("Gold Ore"), TEXT("Onyx"), TEXT("Emerald") };
		return N[FMath::Clamp(static_cast<int32>(T), 0, int32(UE_ARRAY_COUNT(N)) - 1)];
	}
	inline bool IsOreOrGem(EItemType T)
	{
		return T == EItemType::CopperOre || T == EItemType::Coal || T == EItemType::Diamond || T == EItemType::TinOre || T == EItemType::IronOre
			|| T == EItemType::SilverOre || T == EItemType::GoldOre || T == EItemType::Onyx || T == EItemType::Emerald;
	}
	inline bool IsGem(EItemType T) { return T == EItemType::Diamond || T == EItemType::Onyx || T == EItemType::Emerald; }
	// The colour of an ore's lumps or a gem's crystals (rocks, icons, dropped stacks)
	inline FLinearColor OreColor(EItemType T)
	{
		switch (T)
		{
		case EItemType::CopperOre: return FLinearColor(0.80f, 0.40f, 0.12f);
		case EItemType::Coal:      return FLinearColor(0.02f, 0.02f, 0.02f);
		case EItemType::TinOre:    return FLinearColor(0.62f, 0.64f, 0.62f);
		case EItemType::IronOre:   return FLinearColor(0.45f, 0.20f, 0.10f);
		case EItemType::SilverOre: return FLinearColor(0.86f, 0.88f, 0.92f);
		case EItemType::GoldOre:   return FLinearColor(0.98f, 0.74f, 0.20f);
		case EItemType::Diamond:   return FLinearColor(0.45f, 0.92f, 1.00f);
		case EItemType::Onyx:      return FLinearColor(0.05f, 0.04f, 0.06f);
		case EItemType::Emerald:   return FLinearColor(0.08f, 0.80f, 0.32f);
		default:                   return FLinearColor(0.5f, 0.5f, 0.5f);
		}
	}

	// ---------------------------------------------------------------- names
	inline const TCHAR* TierName(int32 T)
	{
		static const TCHAR* N[NumTiers] = { TEXT("Tin"), TEXT("Iron"), TEXT("Bronze"), TEXT("Steel"), TEXT("Silver"), TEXT("Gold"), TEXT("Onyx"), TEXT("Emerald"), TEXT("Diamond") };
		return N[FMath::Clamp(T, 0, NumTiers - 1)];
	}
	inline const TCHAR* KindName(int32 K)
	{
		static const TCHAR* N[K_Count] = { TEXT("Sword"), TEXT("Axe"), TEXT("Pickaxe"), TEXT("Spear"), TEXT("Broadsword"), TEXT("Battle Axe"), TEXT("Katana"),
			TEXT("Wand"), TEXT("Wizard Staff"), TEXT("Bow"), TEXT("Fishing Pole"), TEXT("Shield"),
			TEXT("Helmet"), TEXT("Chestplate"), TEXT("Shoulders"), TEXT("Gloves"), TEXT("Boots"), TEXT("Cape"), TEXT("Ring"), TEXT("Amulet"), TEXT("Helm Brush") };
		return N[FMath::Clamp(K, 0, K_Count - 1)];
	}
	inline const TCHAR* CapeColorName(int32 C)
	{
		static const TCHAR* N[NumCapeColors] = { TEXT("Red"), TEXT("Blue"), TEXT("Green"), TEXT("Purple"), TEXT("Gold"), TEXT("Black"), TEXT("White") };
		return N[FMath::Clamp(C, 0, NumCapeColors - 1)];
	}
	inline FLinearColor CapeColor(int32 C)
	{
		static const FLinearColor Col[NumCapeColors] = {
			{0.80f,0.07f,0.07f,1.f}, {0.12f,0.25f,0.70f,1.f}, {0.10f,0.45f,0.15f,1.f}, {0.35f,0.12f,0.50f,1.f},
			{0.90f,0.68f,0.12f,1.f}, {0.06f,0.06f,0.08f,1.f}, {0.90f,0.90f,0.88f,1.f} };
		return Col[FMath::Clamp(C, 0, NumCapeColors - 1)];
	}

	// Rings and amulets
	constexpr int32 NumRings = 5, NumAmulets = 5;
	inline const TCHAR* AccessoryName(int32 Kind, int32 V)
	{
		if (Kind == K_Crest) return TEXT("Red Helm Brush");
		static const TCHAR* R[NumRings] = { TEXT("Silver Ring"), TEXT("Gold Ring"), TEXT("Bronze Band"), TEXT("Sapphire Ring"), TEXT("Ruby Ring") };
		static const TCHAR* A[NumAmulets] = { TEXT("Hawk's Eye Amulet"), TEXT("Emerald Necklace"), TEXT("Chaos Pendant"), TEXT("Guardian Amulet"), TEXT("Vitality Amulet") };
		return Kind == K_Ring ? R[FMath::Clamp(V, 0, NumRings - 1)] : A[FMath::Clamp(V, 0, NumAmulets - 1)];
	}

	inline FString ItemName(int32 Id)
	{
		if (IsArrow(Id)) return FString::Printf(TEXT("%s Arrows"), TierName(ArrowTier(Id)));
		if (IsPotion(Id)) return PotionName(PotionType(Id));
		if (!IsGear(Id)) return FString();
		const int32 K = KindOf(Id), V = VariantOf(Id);
		if (K == K_Cape) return FString::Printf(TEXT("%s Cape"), CapeColorName(V));
		if (K == K_Shield) return FString::Printf(TEXT("%s %s"), TierName(V), ShieldStyleName(StyleOf(Id)));
		if (IsAccessoryKind(K)) return AccessoryName(K, V);
		return FString::Printf(TEXT("%s %s"), TierName(V), KindName(K));
	}

	// ---------------------------------------------------------------- tier colours
	// Roles follow EArmorRole: Plate, Under, Trim, Cloth, Accent, Leather, BootBody.
	inline FLinearColor Palette(int32 Tier, int32 Role)
	{
		static const FLinearColor T[NumTiers][7] = {
			// Tin: pale grey, dull
			{ {0.52f,0.54f,0.55f,1.f}, {0.16f,0.16f,0.17f,1.f}, {0.66f,0.67f,0.68f,1.f}, {0.80f,0.80f,0.78f,1.f}, {0.45f,0.30f,0.20f,1.f}, {0.26f,0.16f,0.08f,1.f}, {0.40f,0.41f,0.43f,1.f} },
			// Iron: dark grey
			{ {0.34f,0.35f,0.40f,1.f}, {0.10f,0.10f,0.12f,1.f}, {0.55f,0.57f,0.62f,1.f}, {0.78f,0.80f,0.86f,1.f}, {0.55f,0.08f,0.10f,1.f}, {0.20f,0.12f,0.07f,1.f}, {0.26f,0.27f,0.31f,1.f} },
			// Bronze: warm, with a shine
			{ {0.72f,0.45f,0.20f,1.f}, {0.22f,0.14f,0.07f,1.f}, {0.88f,0.74f,0.34f,1.f}, {0.90f,0.84f,0.70f,1.f}, {0.70f,0.20f,0.08f,1.f}, {0.28f,0.15f,0.07f,1.f}, {0.50f,0.30f,0.13f,1.f} },
			// Steel: bright
			{ {0.66f,0.69f,0.76f,1.f}, {0.20f,0.21f,0.25f,1.f}, {0.86f,0.68f,0.26f,1.f}, {0.93f,0.93f,0.89f,1.f}, {0.80f,0.07f,0.07f,1.f}, {0.28f,0.15f,0.07f,1.f}, {0.40f,0.42f,0.48f,1.f} },
			// Silver: shiny, near white
			{ {0.84f,0.86f,0.90f,1.f}, {0.22f,0.23f,0.27f,1.f}, {0.95f,0.96f,1.00f,1.f}, {0.95f,0.95f,0.95f,1.f}, {0.20f,0.35f,0.80f,1.f}, {0.30f,0.20f,0.12f,1.f}, {0.60f,0.62f,0.68f,1.f} },
			// Gold: golden
			{ {0.95f,0.72f,0.25f,1.f}, {0.25f,0.17f,0.06f,1.f}, {1.00f,0.88f,0.45f,1.f}, {0.95f,0.90f,0.75f,1.f}, {0.75f,0.10f,0.10f,1.f}, {0.32f,0.18f,0.07f,1.f}, {0.70f,0.50f,0.16f,1.f} },
			// Onyx (knights' legendary): glossy black with gold edges
			{ {0.06f,0.06f,0.07f,1.f}, {0.03f,0.03f,0.03f,1.f}, {0.95f,0.75f,0.30f,1.f}, {0.25f,0.25f,0.27f,1.f}, {0.70f,0.08f,0.08f,1.f}, {0.10f,0.07f,0.05f,1.f}, {0.05f,0.05f,0.06f,1.f} },
			// Emerald (trolls' legendary): deep green
			{ {0.08f,0.55f,0.28f,1.f}, {0.03f,0.15f,0.08f,1.f}, {0.55f,0.95f,0.60f,1.f}, {0.70f,0.92f,0.72f,1.f}, {0.90f,0.80f,0.20f,1.f}, {0.15f,0.20f,0.10f,1.f}, {0.06f,0.40f,0.20f,1.f} },
			// Diamond (wizards' legendary): icy blue
			{ {0.42f,0.86f,0.96f,1.f}, {0.10f,0.28f,0.38f,1.f}, {0.80f,0.96f,1.00f,1.f}, {0.88f,0.97f,1.00f,1.f}, {0.10f,0.50f,0.80f,1.f}, {0.18f,0.30f,0.38f,1.f}, {0.30f,0.70f,0.82f,1.f} },
		};
		return T[FMath::Clamp(Tier, 0, NumTiers - 1)][FMath::Clamp(Role, 0, 6)];
	}
	inline FLinearColor GemColor(int32 Tier)
	{
		static const FLinearColor G[NumTiers] = { {0.70f,0.72f,0.75f,1.f}, {0.55f,0.60f,0.70f,1.f}, {1.0f,0.55f,0.10f,1.f}, {0.90f,0.97f,1.0f,1.f}, {0.80f,0.85f,1.0f,1.f}, {1.0f,0.80f,0.25f,1.f}, {0.70f,0.15f,0.90f,1.f}, {0.10f,0.95f,0.40f,1.f}, {0.40f,0.95f,1.0f,1.f} };
		return G[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	inline bool TierIsMetal(int32 Tier) { return Tier >= 0; }   // (every tier is metal or gem since the redo; Wood is gone)

	// ---------------------------------------------------------------- weapon and armour numbers
	// Damage is a range: every hit is a random whole number from Min to Max.
	struct FWeaponBase { int32 Min, Max; float Range; float Cooldown; };
	// Numbers are for TIN, the lowest tier; better tiers add steps (TierSteps).
	inline FWeaponBase WeaponBase(int32 Kind)
	{
		switch (Kind)
		{
		case K_Sword:      return { 8, 12, 230.f, 0.55f };
		case K_Axe:        return { 9, 13, 220.f, 0.70f };
		case K_Pickaxe:    return { 6, 9, 210.f, 0.75f };
		case K_Spear:      return { 6, 9, 380.f, 0.65f };    // longer reach, lower damage
		case K_Broadsword: return { 15, 22, 260.f, 0.95f };
		case K_BattleAxe:  return { 17, 25, 250.f, 1.05f };
		case K_Katana:     return { 6, 9, 225.f, 0.38f };    // two blades, very fast
		case K_Wand:       return { 7, 11, 1800.f, 0.55f };  // magic bolt
		case K_Staff:      return { 11, 17, 2200.f, 1.00f }; // heavy magic bolt
		case K_Bow:        return { 5, 7, 4000.f, 0.80f };   // per arrow
		case K_Rod:        return { 2, 3, 200.f, 0.80f };
		}
		return { 1, 2, 200.f, 1.f };
	}
	inline float TierMult(int32 Tier)
	{
		static const float M[NumTiers] = { 0.6f, 0.85f, 1.0f, 1.35f, 1.7f, 2.1f, 2.6f, 2.6f, 2.6f };
		return M[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	// How many damage steps a tier adds (narrow gaps, so skill can beat a tier of gear; the three legendaries are equal)
	inline float TierSteps(int32 Tier)
	{
		static const float S[NumTiers] = { 0.f, 0.8f, 1.6f, 2.5f, 3.4f, 4.3f, 5.3f, 5.3f, 5.3f };
		return S[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	// Each step adds to both ends of the range: a quarter of the weapon's tin range
	// (sword: tin 8-12, iron 12-16, bronze 16-20, steel 20-24, silver 25-29, gold 30-34, legendary 35-39).
	inline int32 TierStep(int32 Kind)
	{
		const FWeaponBase B = WeaponBase(Kind);
		return FMath::Max(1, FMath::RoundToInt((B.Min + B.Max) * 0.25f));
	}
	// Plain damage range of a weapon (before rings, amulets and skill). Bows add the arrow tier fully and the bow tier by half.
	inline void WeaponDamage(int32 Kind, int32 Tier, int32& OutMin, int32& OutMax, int32 ArrowTierForBow = 0)
	{
		const FWeaponBase B = WeaponBase(Kind);
		const float Steps = (Kind == K_Bow) ? (TierSteps(ArrowTierForBow) + 0.5f * TierSteps(Tier)) : TierSteps(Tier);
		const int32 Add = FMath::RoundToInt(TierStep(Kind) * Steps);
		OutMin = B.Min + Add;
		OutMax = B.Max + Add;
	}
	// Which skill a weapon trains and is boosted by
	inline ESkillType CombatSkillFor(int32 Kind)
	{
		if (Kind == K_Bow) return ESkillType::Archery;
		if (Kind == K_Wand || Kind == K_Staff) return ESkillType::Magic;
		return ESkillType::Attack;
	}
	// Defence points of a shield (flat damage blocked) or armour piece (adds to the total)
	inline int32 DefenseOf(int32 Kind, int32 Tier)
	{
		static const int32 Base[K_Count] = { 0,0,0,0,0,0,0,0,0,0,0, 8, 3, 7, 4, 2, 3, 0, 0, 0 };
		return FMath::RoundToInt(Base[FMath::Clamp(Kind, 0, K_Count - 1)] * TierMult(Tier));
	}
	// A shield's guard (the damage it can stop while raised): its Defence, scaled by its style
	inline int32 ShieldGuard(int32 ShieldItemId)
	{
		if (!IsGear(ShieldItemId) || KindOf(ShieldItemId) != K_Shield) return 0;
		return FMath::RoundToInt(DefenseOf(K_Shield, VariantOf(ShieldItemId)) * ShieldGuardMul(StyleOf(ShieldItemId)));
	}
	static_assert(K_Shield == 11, "ShieldId assumes the shield is kind 11");
	// Boots: stamina comes back faster and drains slower
	inline float BootStaminaMult(int32 Tier)
	{
		static const float M[NumTiers] = { 1.0f, 1.08f, 1.15f, 1.25f, 1.4f, 1.6f, 1.9f, 1.9f, 1.9f };
		return M[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	// Tools make gathering faster: 1.0 for a tin tool, down to 0.6 for a legendary one
	inline float ToolTimeMult(int32 Tier) { return 1.f - 0.4f * TierSteps(Tier) / TierSteps(T_Onyx); }

	// ---------------------------------------------------------------- rings and amulets
	struct FMods
	{
		float AttackMul = 1.f, DefenseMul = 1.f;
		float CritChance = 0.f, CritBonus = 0.5f;   // chance, and extra damage on a crit
		int32 FloorDelta = 0, CeilDelta = 0;        // moves the lowest / highest damage
		bool bWild = false;                         // each hit is multiplied by a random 0.5 - 2.0
		float SpeedMul = 1.f;                       // running and walking speed
		int32 MaxHealthBonus = 0;
	};
	inline FMods AccessoryMods(int32 Kind, int32 V)
	{
		FMods M;
		if (Kind == K_Crest) { M.SpeedMul = 1.05f; return M; }   // Red Helm Brush: +5% speed
		if (Kind == K_Ring)
		{
			switch (V)
			{
			case 0: M.AttackMul = 1.15f; M.DefenseMul = 0.95f; break;  // Silver: +15% attack, -5% defence
			case 1: M.DefenseMul = 1.10f; M.AttackMul = 0.95f; break;  // Gold: +10% defence, -5% attack
			case 2: M.AttackMul = 1.05f; M.DefenseMul = 1.05f; break;  // Bronze band: a little of both
			case 3: M.DefenseMul = 1.20f; M.AttackMul = 0.90f; break;  // Sapphire: +20% defence, -10% attack
			case 4: M.AttackMul = 1.25f; M.DefenseMul = 0.85f; break;  // Ruby: +25% attack, -15% defence
			}
		}
		else if (Kind == K_Amulet)
		{
			switch (V)
			{
			case 0: M.CritChance = 0.20f; M.CritBonus = 0.5f; break;   // Hawk's Eye: 20% of hits do +50%
			case 1: M.bWild = true; break;                             // Emerald Necklace: x0.5 to x2.0 on every hit
			case 2: M.FloorDelta = -3; M.CeilDelta = 5; break;         // Chaos Pendant: lowest hit -3, highest +5
			case 3: M.DefenseMul = 1.15f; break;                       // Guardian: +15% defence
			case 4: M.MaxHealthBonus = 25; break;                      // Vitality: +25 health
			}
		}
		return M;
	}
	inline const TCHAR* AccessoryText(int32 Kind, int32 V)
	{
		if (Kind == K_Crest) return TEXT("+5% speed. A red horsehair brush along the crest of your helm");
		static const TCHAR* R[NumRings] = { TEXT("+15% attack, -5% defence"), TEXT("+10% defence, -5% attack"), TEXT("+5% attack, +5% defence"),
			TEXT("+20% defence, -10% attack"), TEXT("+25% attack, -15% defence") };
		static const TCHAR* A[NumAmulets] = { TEXT("20% of hits are critical: +50% damage"), TEXT("Every hit is multiplied by 0.5 to 2.0"),
			TEXT("Lowest damage -3, highest damage +5"), TEXT("+15% defence"), TEXT("+25 maximum health") };
		return Kind == K_Ring ? R[FMath::Clamp(V, 0, NumRings - 1)] : A[FMath::Clamp(V, 0, NumAmulets - 1)];
	}
	// Turn a plain damage range into the range with rings and amulets
	inline void ApplyAttackMods(const FMods& M, int32& Min, int32& Max)
	{
		Min = FMath::Max(1, FMath::RoundToInt(Min * M.AttackMul) + M.FloorDelta);
		Max = FMath::Max(Min, FMath::RoundToInt(Max * M.AttackMul) + M.CeilDelta);
	}

	// ---------------------------------------------------------------- prices (ingots)
	// The store sells tin, iron and bronze gear. Steel and better have to be smithed.
	inline int32 BaseBuyPrice(int32 Kind)
	{
		static const int32 P[K_Count] = { 40, 45, 35, 40, 90, 100, 110, 80, 100, 60, 30, 50, 40, 90, 55, 30, 45, 25, 0, 0 };
		return P[FMath::Clamp(Kind, 0, K_Count - 1)];
	}
	inline float TierPriceMult(int32 Tier)
	{
		static const float M[NumTiers] = { 0.4f, 0.75f, 1.0f, 2.2f, 4.0f, 7.0f, 15.f, 15.f, 15.f };
		return M[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	inline int32 AccessoryPrice(int32 Kind, int32 V)
	{
		if (Kind == K_Crest) return 120;
		static const int32 R[NumRings] = { 120, 100, 60, 130, 140 };
		static const int32 A[NumAmulets] = { 200, 180, 160, 150, 150 };
		return Kind == K_Ring ? R[FMath::Clamp(V, 0, NumRings - 1)] : A[FMath::Clamp(V, 0, NumAmulets - 1)];
	}
	constexpr int32 StoreMaxTier = T_Bronze;   // the store sells tin, iron and bronze
	inline int32 BuyPrice(int32 Kind, int32 Variant)
	{
		if (IsAccessoryKind(Kind)) return AccessoryPrice(Kind, Variant);
		if (Kind == K_Cape) return BaseBuyPrice(K_Cape) + (Variant == 4 ? 20 : 0);
		return FMath::Max(1, FMath::RoundToInt(BaseBuyPrice(Kind) * TierPriceMult(Variant)));
	}
	constexpr int32 ArrowBundle = 10;
	inline int32 ArrowBundlePrice(int32 Tier) { return FMath::Max(2, FMath::RoundToInt(8.f * TierPriceMult(Tier))); }

	// ---------------------------------------------------------------- smithing
	// How much raw material a piece takes ("mass"), and which materials each tier uses (2026-10-30):
	//   Tin: tin ore.  Iron: iron ore.  Bronze: copper + tin.  Steel: iron ore + coal.  Silver: silver ore.  Gold: gold ore.
	//   Onyx / Emerald / Diamond (the legendaries): their gem + gold ore, at a very high Smithing level.
	inline bool CanSmith(int32 Kind) { return IsWeaponKind(Kind) || Kind == K_Shield || IsArmorKind(Kind); }
	inline int32 Mass(int32 Kind)
	{
		static const int32 M[K_Count] = { 4, 5, 4, 4, 8, 9, 6, 3, 5, 4, 3, 6, 5, 10, 6, 3, 5, 0, 0, 0 };
		return M[FMath::Clamp(Kind, 0, K_Count - 1)];
	}
	// A recipe: up to two materials (Num 0 = unused), the Smithing level it needs and the XP it gives
	struct FRecipe { EItemType Mat[2] = { EItemType::TinOre, EItemType::TinOre }; int32 Num[2] = { 0, 0 }; int32 Level = 1; int32 XP = 0; };
	inline FRecipe SmithRecipe(int32 Kind, int32 Tier, bool bArrows = false)
	{
		FRecipe R;
		const int32 M = bArrows ? 2 : Mass(Kind);   // arrows: a bundle of 10 costs 2 mass
		const int32 H = (M + 1) / 2;
		auto Set = [&](EItemType A, int32 NA, EItemType B = EItemType::TinOre, int32 NB = 0) { R.Mat[0] = A; R.Num[0] = NA; R.Mat[1] = B; R.Num[1] = NB; };
		switch (Tier)
		{
		case T_Tin:     Set(EItemType::TinOre, M); break;
		case T_Iron:    Set(EItemType::IronOre, M); break;
		case T_Bronze:  Set(EItemType::CopperOre, M, EItemType::TinOre, H); break;
		case T_Steel:   Set(EItemType::IronOre, M, EItemType::Coal, H); break;
		case T_Silver:  Set(EItemType::SilverOre, M); break;
		case T_Gold:    Set(EItemType::GoldOre, M); break;
		case T_Onyx:    Set(EItemType::Onyx, H, EItemType::GoldOre, H); break;
		case T_Emerald: Set(EItemType::Emerald, H, EItemType::GoldOre, H); break;
		default:        Set(EItemType::Diamond, H, EItemType::GoldOre, H); break;
		}
		static const int32 TierLevel[NumTiers] = { 1, 5, 10, 18, 26, 34, 50, 50, 50 };
		static const int32 TierXP[NumTiers] = { 4, 8, 12, 20, 30, 45, 90, 90, 90 };
		int32 Extra = 0;
		if (Kind == K_Chest || Kind == K_Broadsword || Kind == K_BattleAxe || Kind == K_Katana) Extra = 2;
		else if (Kind == K_Wand || Kind == K_Staff || Kind == K_Bow || Kind == K_Shoulders) Extra = 1;
		R.Level = TierLevel[FMath::Clamp(Tier, 0, NumTiers - 1)] + (Tier == T_Tin ? 0 : Extra);
		R.XP = M * TierXP[FMath::Clamp(Tier, 0, NumTiers - 1)];
		return R;
	}

	// Levels needed to gather things
	inline int32 MiningLevelFor(EItemType T)
	{
		switch (T)
		{
		case EItemType::Coal:      return 6;
		case EItemType::IronOre:   return 8;
		case EItemType::SilverOre: return 20;
		case EItemType::GoldOre:   return 30;
		case EItemType::Onyx: case EItemType::Emerald: case EItemType::Diamond: return 45;
		default:                   return 1;   // tin and copper
		}
	}
	inline int32 WoodcuttingLevelFor(EItemType T) { return T == EItemType::RedwoodLogs ? 15 : (T == EItemType::PineLogs ? 6 : 1); }
	inline int32 FishingLevelFor(EItemType T) { return T == EItemType::GoldenCarp ? 12 : (T == EItemType::Trout ? 5 : 1); }
	inline int32 FishXP(EItemType T) { return T == EItemType::GoldenCarp ? 60 : (T == EItemType::Trout ? 20 : 8); }

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

	// ---------------------------------------------------------------- dictionary text
	inline const TCHAR* KindBlurb(int32 K)
	{
		switch (K)
		{
		case K_Sword:      return TEXT("The all-rounder: quick, reliable hits. Swing left to right, then right to left for a fast combo. A shield can be carried with it.");
		case K_Axe:        return TEXT("Hits harder than a sword and chops trees faster. Equip it (or let it auto-select) to chop. A shield can be carried with it.");
		case K_Pickaxe:    return TEXT("A mining tool first and a weapon only in a pinch. Better pickaxes mine faster and unlock harder ore.");
		case K_Spear:      return TEXT("Long reach. Hold the attack button to charge a lunge. A shield can be carried with it.");
		case K_Broadsword: return TEXT("A heavy two-handed blade: slow swings, big damage. Cannot be used with a shield.");
		case K_BattleAxe:  return TEXT("The biggest hitter and the slowest. Two-handed, no shield.");
		case K_Katana:     return TEXT("A pair of light curved blades, one in each hand. Very fast, modest hits. No shield.");
		case K_Wand:       return TEXT("Fires small magic bolts at range. Light and quick.");
		case K_Staff:      return TEXT("A wizard's staff: slow, heavy bolts from far away. Two-handed, no shield.");
		case K_Bow:        return TEXT("Fires arrows from a distance. Arrows run out: buy them at the store or smith them. Better arrows hit harder. A shield can be carried with it.");
		case K_Rod:        return TEXT("A fishing pole. Equip it and click towards the pond. Better poles bite faster and reach rarer fish.");
		case K_Shield:     return TEXT("Hold right-click to block blows from the front. A shield's guard stops damage up to its Defence; once spent, the guard is broken and stops only half of each blow until it recovers (10 s after the last blow, then 10 s to refill). Not usable with two-handed weapons.");
		case K_Helmet:     return TEXT("Protects the head. Defence adds to the total of all worn armour.");
		case K_Chest:      return TEXT("The heaviest armour piece and the biggest defence bonus.");
		case K_Shoulders:  return TEXT("Layered pauldrons for the shoulders.");
		case K_Gloves:     return TEXT("Armoured gauntlets for the hands.");
		case K_Boots:      return TEXT("Armoured boots. Better boots also give you more stamina: it lasts longer and returns faster.");
		case K_Cape:       return TEXT("A cloak in your choice of colour. Purely for style.");
		case K_Ring:       return TEXT("Rings shift damage and defence. Three miscellaneous slots hold any mix of rings and amulets. Every effect is on top of the weapon's own damage range.");
		case K_Amulet:     return TEXT("Wears in one of your three miscellaneous slots. Some change the damage rules in strange ways.");
		}
		return TEXT("");
	}
	inline const TCHAR* TierBlurb(int32 T)
	{
		switch (T)
		{
		case T_Tin:     return TEXT("TIN  - from tin ore. Dull and soft: the starting rank. Level 1.");
		case T_Iron:    return TEXT("IRON  - from iron ore. Dark and tough. Level 5.");
		case T_Bronze:  return TEXT("BRONZE  - copper with a little tin. Warm and shining. Level 10.");
		case T_Steel:   return TEXT("STEEL  - iron ore forged with coal. Bright and strong, smithed only. Level 20.");
		case T_Silver:  return TEXT("SILVER  - from silver ore. Gleaming, smithed only. Level 30.");
		case T_Gold:    return TEXT("GOLD  - from gold ore. Golden and rare, smithed only. Level 40.");
		case T_Onyx:    return TEXT("ONYX  - the knights' legendary: onyx and gold, glossy black. Level 50, Smithing 50.");
		case T_Emerald: return TEXT("EMERALD  - the trolls' legendary: emerald and gold. Trolls only.");
		default:        return TEXT("DIAMOND  - the wizards' legendary: diamond and gold. Wizards only.");
		}
	}
	// Short material text for a recipe, e.g. "6 Copper"
	inline FString RecipeText(const FRecipe& R)
	{
		FString S;
		auto Add = [&](int32 N, const TCHAR* Name) { if (N > 0) { if (!S.IsEmpty()) S += TEXT(", "); S += FString::Printf(TEXT("%d %s"), N, Name); } };
		for (int32 i = 0; i < 2; ++i) Add(R.Num[i], MaterialName(R.Mat[i]));
		return S;
	}
}
