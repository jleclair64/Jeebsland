#pragma once

#include "CoreMinimal.h"
#include "SkillComponent.h"

// ===================================================================================================
// Everything about items: ids, names, stack sizes, damage and defence numbers, prices, smithing recipes,
// rings and amulets. Header only; shared by the character, the shops and the HUD.
//
// TIERS (weakest to strongest): Wood (trees), Bronze (copper), Iron (coal), Steel (coal + copper), Diamond.
// ===================================================================================================
namespace JeebItems
{
	constexpr int32 NumSlots = 16;      // inventory slots
	constexpr int32 NumHotbar = 5;      // weapons and tools you carry ready
	constexpr int32 NumTiers = 5;
	constexpr int32 NumCapeColors = 7;

	// What kind of thing a piece of gear is
	enum EKind : int32
	{
		K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Broadsword, K_BattleAxe, K_Katana, K_Wand, K_Staff, K_Bow, K_Rod,   // the weapon bar
		K_Shield,
		K_Helmet, K_Chest, K_Shoulders, K_Gloves, K_Boots,
		K_Cape,
		K_Ring, K_Amulet,
		K_Count
	};

	// Ids: 0 = empty; 1..99 = materials and fish (EItemType + 1); 900..904 = arrows by tier; 1000 + kind * 10 + variant = gear.
	// The variant is the tier (0-4) for weapons and armour, the colour for capes, the particular piece for rings and amulets.
	constexpr int32 IdArrowBase = 900;
	constexpr int32 IdGearBase = 1000;

	inline int32 MaterialId(EItemType T) { return static_cast<int32>(T) + 1; }
	inline bool IsMaterial(int32 Id) { return Id >= 1 && Id < 100; }
	inline EItemType MaterialType(int32 Id) { return static_cast<EItemType>(Id - 1); }
	inline int32 ArrowId(int32 Tier) { return IdArrowBase + Tier; }
	inline bool IsArrow(int32 Id) { return Id >= IdArrowBase && Id < IdArrowBase + NumTiers; }
	inline int32 ArrowTier(int32 Id) { return Id - IdArrowBase; }
	inline int32 GearId(int32 Kind, int32 Variant) { return IdGearBase + Kind * 10 + Variant; }
	inline bool IsGear(int32 Id) { return Id >= IdGearBase; }
	inline int32 KindOf(int32 Id) { return (Id - IdGearBase) / 10; }
	inline int32 VariantOf(int32 Id) { return (Id - IdGearBase) % 10; }
	inline int32 TierOf(int32 Id) { return IsArrow(Id) ? ArrowTier(Id) : VariantOf(Id); }

	inline bool IsWeaponKind(int32 K) { return K >= K_Sword && K <= K_Rod; }
	inline bool IsArmorKind(int32 K) { return K >= K_Helmet && K <= K_Boots; }
	inline bool IsAccessoryKind(int32 K) { return K == K_Ring || K == K_Amulet; }

	// Two-handed weapons cannot be used with a shield
	inline bool AllowsShield(int32 Kind) { return !(Kind == K_Broadsword || Kind == K_BattleAxe || Kind == K_Katana || Kind == K_Staff); }

	inline int32 StackSize(int32 Id)
	{
		if (IsArrow(Id)) return 30;
		if (!IsMaterial(Id)) return 1;
		switch (MaterialType(Id))
		{
		case EItemType::CopperOre: return 10;
		case EItemType::Coal:      return 10;
		case EItemType::Diamond:   return 5;
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
		case EItemType::Diamond:     return 50;
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
		static const TCHAR* N[] = { TEXT("Copper Ore"), TEXT("Coal"), TEXT("Logs"), TEXT("Diamond"), TEXT("Raw Pork"), TEXT("Pine Logs"), TEXT("Redwood Logs"), TEXT("Perch"), TEXT("Trout"), TEXT("Golden Carp"), TEXT("Beast Hide"), TEXT("Bones") };
		return N[FMath::Clamp(static_cast<int32>(T), 0, 11)];
	}

	// ---------------------------------------------------------------- names
	inline const TCHAR* TierName(int32 T)
	{
		static const TCHAR* N[NumTiers] = { TEXT("Wood"), TEXT("Bronze"), TEXT("Iron"), TEXT("Steel"), TEXT("Diamond") };
		return N[FMath::Clamp(T, 0, NumTiers - 1)];
	}
	inline const TCHAR* KindName(int32 K)
	{
		static const TCHAR* N[K_Count] = { TEXT("Sword"), TEXT("Axe"), TEXT("Pickaxe"), TEXT("Spear"), TEXT("Broadsword"), TEXT("Battle Axe"), TEXT("Katana"),
			TEXT("Wand"), TEXT("Wizard Staff"), TEXT("Bow"), TEXT("Fishing Pole"), TEXT("Shield"),
			TEXT("Helmet"), TEXT("Chestplate"), TEXT("Shoulders"), TEXT("Gloves"), TEXT("Boots"), TEXT("Cape"), TEXT("Ring"), TEXT("Amulet") };
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
		static const TCHAR* R[NumRings] = { TEXT("Silver Ring"), TEXT("Gold Ring"), TEXT("Bronze Band"), TEXT("Sapphire Ring"), TEXT("Ruby Ring") };
		static const TCHAR* A[NumAmulets] = { TEXT("Hawk's Eye Amulet"), TEXT("Emerald Necklace"), TEXT("Chaos Pendant"), TEXT("Guardian Amulet"), TEXT("Vitality Amulet") };
		return Kind == K_Ring ? R[FMath::Clamp(V, 0, NumRings - 1)] : A[FMath::Clamp(V, 0, NumAmulets - 1)];
	}

	inline FString ItemName(int32 Id)
	{
		if (IsArrow(Id)) return FString::Printf(TEXT("%s Arrows"), TierName(ArrowTier(Id)));
		if (!IsGear(Id)) return FString();
		const int32 K = KindOf(Id), V = VariantOf(Id);
		if (K == K_Cape) return FString::Printf(TEXT("%s Cape"), CapeColorName(V));
		if (IsAccessoryKind(K)) return AccessoryName(K, V);
		return FString::Printf(TEXT("%s %s"), TierName(V), KindName(K));
	}

	// ---------------------------------------------------------------- tier colours
	// Roles follow EArmorRole: Plate, Under, Trim, Cloth, Accent, Leather, BootBody.
	inline FLinearColor Palette(int32 Tier, int32 Role)
	{
		static const FLinearColor T[NumTiers][7] = {
			// Wood: planks and leather
			{ {0.50f,0.34f,0.17f,1.f}, {0.18f,0.12f,0.07f,1.f}, {0.33f,0.20f,0.09f,1.f}, {0.62f,0.52f,0.36f,1.f}, {0.62f,0.25f,0.10f,1.f}, {0.26f,0.15f,0.07f,1.f}, {0.36f,0.23f,0.11f,1.f} },
			// Bronze (from copper)
			{ {0.72f,0.45f,0.20f,1.f}, {0.22f,0.14f,0.07f,1.f}, {0.88f,0.74f,0.34f,1.f}, {0.90f,0.84f,0.70f,1.f}, {0.70f,0.20f,0.08f,1.f}, {0.28f,0.15f,0.07f,1.f}, {0.50f,0.30f,0.13f,1.f} },
			// Iron (from coal): dark grey
			{ {0.34f,0.35f,0.40f,1.f}, {0.10f,0.10f,0.12f,1.f}, {0.55f,0.57f,0.62f,1.f}, {0.78f,0.80f,0.86f,1.f}, {0.55f,0.08f,0.10f,1.f}, {0.20f,0.12f,0.07f,1.f}, {0.26f,0.27f,0.31f,1.f} },
			// Steel: bright
			{ {0.66f,0.69f,0.76f,1.f}, {0.20f,0.21f,0.25f,1.f}, {0.86f,0.68f,0.26f,1.f}, {0.93f,0.93f,0.89f,1.f}, {0.80f,0.07f,0.07f,1.f}, {0.28f,0.15f,0.07f,1.f}, {0.40f,0.42f,0.48f,1.f} },
			// Diamond: icy blue
			{ {0.42f,0.86f,0.96f,1.f}, {0.10f,0.28f,0.38f,1.f}, {0.80f,0.96f,1.00f,1.f}, {0.88f,0.97f,1.00f,1.f}, {0.10f,0.50f,0.80f,1.f}, {0.18f,0.30f,0.38f,1.f}, {0.30f,0.70f,0.82f,1.f} },
		};
		return T[FMath::Clamp(Tier, 0, NumTiers - 1)][FMath::Clamp(Role, 0, 6)];
	}
	inline FLinearColor GemColor(int32 Tier)
	{
		static const FLinearColor G[NumTiers] = { {0.25f,0.95f,0.35f,1.f}, {1.0f,0.55f,0.10f,1.f}, {0.45f,0.60f,1.0f,1.f}, {0.90f,0.97f,1.0f,1.f}, {0.40f,0.95f,1.0f,1.f} };
		return G[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	inline bool TierIsMetal(int32 Tier) { return Tier >= 1; }

	// ---------------------------------------------------------------- weapon and armour numbers
	// Damage is a range: every hit is a random whole number from Min to Max.
	struct FWeaponBase { int32 Min, Max; float Range; float Cooldown; };
	// Numbers are for BRONZE; other tiers scale by TierMult.
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
		static const float M[NumTiers] = { 0.6f, 1.0f, 1.35f, 1.8f, 2.4f };
		return M[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	// Each tier above wood adds a flat step to both ends of the range: half the weapon's average damage
	// (sword: wood 8-12, bronze 13-17, iron 18-22, steel 23-27, diamond 28-32).
	inline int32 TierStep(int32 Kind)
	{
		const FWeaponBase B = WeaponBase(Kind);
		return FMath::Max(1, FMath::RoundToInt((B.Min + B.Max) * 0.25f));
	}
	// Plain damage range of a weapon (before rings, amulets and skill). Bows add the arrow tier fully and the bow tier by half.
	inline void WeaponDamage(int32 Kind, int32 Tier, int32& OutMin, int32& OutMax, int32 ArrowTierForBow = 0)
	{
		const FWeaponBase B = WeaponBase(Kind);
		const float Steps = (Kind == K_Bow) ? (ArrowTierForBow + 0.5f * Tier) : (float)Tier;
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
	// Boots: stamina comes back faster and drains slower
	inline float BootStaminaMult(int32 Tier)
	{
		static const float M[NumTiers] = { 1.0f, 1.1f, 1.25f, 1.5f, 2.0f };
		return M[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	// Tools make gathering faster: 1.0 for a wooden tool, down to 0.45 for diamond
	inline float ToolTimeMult(int32 Tier) { return 1.f - 0.14f * Tier; }

	// ---------------------------------------------------------------- rings and amulets
	struct FMods
	{
		float AttackMul = 1.f, DefenseMul = 1.f;
		float CritChance = 0.f, CritBonus = 0.5f;   // chance, and extra damage on a crit
		int32 FloorDelta = 0, CeilDelta = 0;        // moves the lowest / highest damage
		bool bWild = false;                         // each hit is multiplied by a random 0.5 - 2.0
		int32 MaxHealthBonus = 0;
	};
	inline FMods AccessoryMods(int32 Kind, int32 V)
	{
		FMods M;
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
	// The store sells wood, bronze and iron gear. Steel and diamond gear have to be smithed.
	inline int32 BaseBuyPrice(int32 Kind)
	{
		static const int32 P[K_Count] = { 40, 45, 35, 40, 90, 100, 110, 80, 100, 60, 30, 50, 40, 90, 55, 30, 45, 25, 0, 0 };
		return P[FMath::Clamp(Kind, 0, K_Count - 1)];
	}
	inline float TierPriceMult(int32 Tier)
	{
		static const float M[NumTiers] = { 0.4f, 1.0f, 2.2f, 4.5f, 10.f };
		return M[FMath::Clamp(Tier, 0, NumTiers - 1)];
	}
	inline int32 AccessoryPrice(int32 Kind, int32 V)
	{
		static const int32 R[NumRings] = { 120, 100, 60, 130, 140 };
		static const int32 A[NumAmulets] = { 200, 180, 160, 150, 150 };
		return Kind == K_Ring ? R[FMath::Clamp(V, 0, NumRings - 1)] : A[FMath::Clamp(V, 0, NumAmulets - 1)];
	}
	constexpr int32 StoreMaxTier = 2;   // store sells wood, bronze and iron
	inline int32 BuyPrice(int32 Kind, int32 Variant)
	{
		if (IsAccessoryKind(Kind)) return AccessoryPrice(Kind, Variant);
		if (Kind == K_Cape) return BaseBuyPrice(K_Cape) + (Variant == 4 ? 20 : 0);
		return FMath::Max(1, FMath::RoundToInt(BaseBuyPrice(Kind) * TierPriceMult(Variant)));
	}
	constexpr int32 ArrowBundle = 10;
	inline int32 ArrowBundlePrice(int32 Tier) { return FMath::Max(2, FMath::RoundToInt(8.f * TierPriceMult(Tier))); }

	// ---------------------------------------------------------------- smithing
	// How much raw material a piece takes ("mass"), and which materials the tier uses:
	//   Wood: logs.  Bronze: copper.  Iron: coal.  Steel: coal + copper.  Diamond: diamonds + coal.
	inline bool CanSmith(int32 Kind) { return IsWeaponKind(Kind) || Kind == K_Shield || IsArmorKind(Kind); }
	inline int32 Mass(int32 Kind)
	{
		static const int32 M[K_Count] = { 4, 5, 4, 4, 8, 9, 6, 3, 5, 4, 3, 6, 5, 10, 6, 3, 5, 0, 0, 0 };
		return M[FMath::Clamp(Kind, 0, K_Count - 1)];
	}
	struct FRecipe { int32 Logs = 0, Copper = 0, Coal = 0, Diamonds = 0; int32 Level = 1; int32 XP = 0; };
	inline FRecipe SmithRecipe(int32 Kind, int32 Tier, bool bArrows = false)
	{
		FRecipe R;
		const int32 M = bArrows ? 2 : Mass(Kind);   // arrows: a bundle of 10 costs 2 mass
		switch (Tier)
		{
		case 0: R.Logs = M; break;
		case 1: R.Copper = M; break;
		case 2: R.Coal = M; break;
		case 3: R.Coal = M; R.Copper = (M + 1) / 2; break;
		default: R.Diamonds = (M + 1) / 2; R.Coal = (M + 1) / 2; break;
		}
		static const int32 TierLevel[NumTiers] = { 1, 4, 10, 18, 28 };
		static const int32 TierXP[NumTiers] = { 4, 8, 16, 32, 64 };
		int32 Extra = 0;
		if (Kind == K_Chest || Kind == K_Broadsword || Kind == K_BattleAxe || Kind == K_Katana) Extra = 2;
		else if (Kind == K_Wand || Kind == K_Staff || Kind == K_Bow || Kind == K_Shoulders) Extra = 1;
		R.Level = TierLevel[FMath::Clamp(Tier, 0, NumTiers - 1)] + (Tier == 0 ? 0 : Extra);
		R.XP = M * TierXP[FMath::Clamp(Tier, 0, NumTiers - 1)];
		return R;
	}

	// Levels needed to gather things
	inline int32 MiningLevelFor(EItemType T) { return T == EItemType::Diamond ? 15 : (T == EItemType::Coal ? 6 : 1); }
	inline int32 WoodcuttingLevelFor(EItemType T) { return T == EItemType::RedwoodLogs ? 15 : (T == EItemType::PineLogs ? 6 : 1); }
	inline int32 FishingLevelFor(EItemType T) { return T == EItemType::GoldenCarp ? 12 : (T == EItemType::Trout ? 5 : 1); }
	inline int32 FishXP(EItemType T) { return T == EItemType::GoldenCarp ? 60 : (T == EItemType::Trout ? 20 : 8); }

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
		if (bShield && S->Worn[K_Shield]) D += DefenseOf(K_Shield, VariantOf(S->Worn[K_Shield]));
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
		case 0: return TEXT("WOOD  - from logs. Weak and cheap: the starting rank.");
		case 1: return TEXT("BRONZE  - from copper ore. A solid first upgrade.");
		case 2: return TEXT("IRON  - from coal. Dark and tough.");
		case 3: return TEXT("STEEL  - coal forged with copper. Bright, strong, smithed only.");
		default: return TEXT("DIAMOND  - from diamonds and coal. The best there is, smithed only.");
		}
	}
	// Short material text for a recipe, e.g. "6 Copper"
	inline FString RecipeText(const FRecipe& R)
	{
		FString S;
		auto Add = [&](int32 N, const TCHAR* Name) { if (N > 0) { if (!S.IsEmpty()) S += TEXT(", "); S += FString::Printf(TEXT("%d %s"), N, Name); } };
		Add(R.Logs, TEXT("Logs"));
		Add(R.Copper, TEXT("Copper"));
		Add(R.Coal, TEXT("Coal"));
		Add(R.Diamonds, TEXT("Diamond"));
		return S;
	}
}
