#include "SkillComponent.h"
#include "Messages.h"
#include "Engine/Engine.h"
#include "Inventory.h"
#include "Lore.h"

namespace
{
	// Level 1 costs Base XP; each later level costs 20% more than the previous one.
	void ComputeLevel(float Base, int32 XP, int32& Level, float& IntoLevel, float& Needed)
	{
		Level = 1;
		Needed = Base;
		IntoLevel = static_cast<float>(XP);
		while (IntoLevel >= Needed)
		{
			IntoLevel -= Needed;
			Needed *= 1.2f;
			++Level;
		}
	}
}

USkillComponent::USkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Slots.SetNum(JeebItems::NumSlots);
	Bank.SetNum(NumBankSlots);
	for (int32 i = 0; i < NumWornKinds; ++i) Worn[i] = 0;

	// Starting kit: a wooden sword, axe, pickaxe and fishing pole on the weapon bar, a wooden shield and a red cape
	Hotbar[0] = JeebItems::GearId(JeebItems::K_Sword, 0);
	Hotbar[1] = JeebItems::GearId(JeebItems::K_Axe, 0);
	Hotbar[2] = JeebItems::GearId(JeebItems::K_Pickaxe, 0);
	Hotbar[3] = JeebItems::GearId(JeebItems::K_Rod, 0);
	Worn[JeebItems::K_Shield] = JeebItems::GearId(JeebItems::K_Shield, 0);
	Worn[JeebItems::K_Cape] = JeebItems::GearId(JeebItems::K_Cape, 0);
	// The knight always wears a full suit of armour (wooden to start). Pieces can be swapped for better ones, never taken off.
	for (int32 K = JeebItems::K_Helmet; K <= JeebItems::K_Boots; ++K) Worn[K] = JeebItems::GearId(K, 0);
}

float USkillComponent::BaseXP(ESkillType Skill)
{
	switch (Skill)
	{
	case ESkillType::Mining:      return 20.f;
	case ESkillType::Woodcutting: return 40.f;
	case ESkillType::Smithing:    return 30.f;
	case ESkillType::Attack:
	case ESkillType::Defense:
	case ESkillType::Archery:
	case ESkillType::Magic:       return 30.f;
	default:                      return 25.f;
	}
}

FString USkillComponent::SkillName(ESkillType Skill)
{
	switch (Skill)
	{
	case ESkillType::Mining:      return TEXT("Mining");
	case ESkillType::Woodcutting: return TEXT("Woodcutting");
	case ESkillType::Smithing:    return TEXT("Smithing");
	case ESkillType::Attack:      return TEXT("Attack");
	case ESkillType::Defense:     return TEXT("Defense");
	case ESkillType::Archery:     return TEXT("Archery");
	case ESkillType::Magic:       return TEXT("Magic");
	case ESkillType::Lockpicking: return TEXT("Lockpicking");
	default:                      return TEXT("Fishing");
	}
}

int32 USkillComponent::GetLevel(ESkillType Skill) const
{
	int32 Level; float Into; float Needed;
	ComputeLevel(BaseXP(Skill), GetXP(Skill), Level, Into, Needed);
	return Level;
}

void USkillComponent::GetProgress(ESkillType Skill, int32& Level, int32& XPIntoLevel, int32& XPNeeded) const
{
	float Into; float Needed;
	ComputeLevel(BaseXP(Skill), GetXP(Skill), Level, Into, Needed);
	XPIntoLevel = FMath::FloorToInt(Into);
	XPNeeded = FMath::RoundToInt(Needed);
}

void USkillComponent::AddXP(ESkillType Skill, int32 Amount)
{
	const int32 OldLevel = GetLevel(Skill);
	XP[static_cast<int32>(Skill)] += Amount;
	const int32 NewLevel = GetLevel(Skill);

	// The HUD shows a progress bar for the skill you last trained, for 30 seconds
	const double Now = FPlatformTime::Seconds();
	LastSkill = Skill;
	LastSkillTime = Now;
	LastXPGain = Amount;
	if (NewLevel > OldLevel)
	{
		LevelUpTime = Now;
		JeebMsg::Post(5.f, FColor::Green, FString::Printf(TEXT("Level up! %s is now level %d."), *SkillName(Skill), NewLevel));
	}
}

bool USkillComponent::CanAdd(int32 Id, int32 Amount) const
{
	const int32 Stack = JeebItems::StackSize(Id);
	int32 Room = 0;
	for (const FInvSlot& S : Slots)
	{
		if (S.Id == 0) Room += Stack;
		else if (S.Id == Id) Room += Stack - S.Count;
	}
	return Room >= Amount;
}

bool USkillComponent::AddInvItem(int32 Id, int32 Amount)
{
	if (!CanAdd(Id, Amount)) return false;
	const int32 Stack = JeebItems::StackSize(Id);
	for (FInvSlot& S : Slots) // top up existing stacks first
	{
		if (Amount <= 0) break;
		if (S.Id == Id && S.Count < Stack)
		{
			const int32 N = FMath::Min(Stack - S.Count, Amount);
			S.Count += N; Amount -= N;
		}
	}
	for (FInvSlot& S : Slots) // then fill empty slots
	{
		if (Amount <= 0) break;
		if (S.Id == 0)
		{
			const int32 N = FMath::Min(Stack, Amount);
			S.Id = Id; S.Count = N; Amount -= N;
		}
	}
	return true;
}

bool USkillComponent::AddItem(EItemType Item, int32 Amount)
{
	return AddInvItem(JeebItems::MaterialId(Item), Amount);
}

int32 USkillComponent::SellAll(EItemType Item)
{
	const int32 Id = JeebItems::MaterialId(Item);
	int32 Total = 0;
	for (FInvSlot& S : Slots)
	{
		if (S.Id == Id) { Total += S.Count; S.Id = 0; S.Count = 0; }
	}
	const int32 Pay = Total * JeebItems::SellValue(Item);
	Ingots += Pay;
	return Pay;
}

bool USkillComponent::SpendIngots(int32 Amount)
{
	if (Ingots < Amount) return false;
	Ingots -= Amount;
	return true;
}

int32 USkillComponent::GetItemCount(EItemType Item) const
{
	return CountItem(JeebItems::MaterialId(Item));
}

int32 USkillComponent::CountItem(int32 Id) const
{
	int32 N = 0;
	for (const FInvSlot& S : Slots) if (S.Id == Id) N += S.Count;
	return N;
}

bool USkillComponent::RemoveItems(int32 Id, int32 Count)
{
	if (CountItem(Id) < Count) return false;
	// Take from the smallest stacks first so big stacks stay tidy
	while (Count > 0)
	{
		FInvSlot* Pick = nullptr;
		for (FInvSlot& S : Slots)
		{
			if (S.Id == Id && S.Count > 0 && (!Pick || S.Count < Pick->Count)) Pick = &S;
		}
		if (!Pick) return false;
		const int32 N = FMath::Min(Pick->Count, Count);
		Pick->Count -= N; Count -= N;
		if (Pick->Count <= 0) { Pick->Id = 0; Pick->Count = 0; }
	}
	return true;
}

int32 USkillComponent::CountWood() const
{
	return GetItemCount(EItemType::Logs) + GetItemCount(EItemType::PineLogs) + GetItemCount(EItemType::RedwoodLogs);
}

bool USkillComponent::TakeWood(int32 Count)
{
	if (CountWood() < Count) return false;
	const EItemType Order[3] = { EItemType::Logs, EItemType::PineLogs, EItemType::RedwoodLogs };
	for (EItemType T : Order)
	{
		const int32 Have = GetItemCount(T);
		const int32 N = FMath::Min(Have, Count);
		if (N > 0) { RemoveItems(JeebItems::MaterialId(T), N); Count -= N; }
		if (Count <= 0) break;
	}
	return true;
}

int32 USkillComponent::BestArrowTier() const
{
	for (int32 T = JeebItems::NumTiers - 1; T >= 0; --T)
		if (CountItem(JeebItems::ArrowId(T)) > 0) return T;
	return -1;
}

int32 USkillComponent::TakeBestArrow()
{
	const int32 T = BestArrowTier();
	if (T < 0) return -1;
	RemoveItems(JeebItems::ArrowId(T), 1);
	return T;
}

bool USkillComponent::EquipFromSlot(int32 Idx)
{
	if (!Slots.IsValidIndex(Idx)) return false;
	FInvSlot& S = Slots[Idx];
	if (!JeebItems::IsGear(S.Id)) return false;
	const int32 Id = S.Id;
	const int32 K = JeebItems::KindOf(Id);

	auto Swap = [&](int32& Target)
	{
		S.Id = Target;
		S.Count = Target ? 1 : 0;
		Target = Id;
	};

	if (JeebItems::IsWeaponKind(K))
	{
		int32 H = -1;
		for (int32 i = 0; i < 5; ++i) if (Hotbar[i] == 0) { H = i; break; }
		if (H < 0) H = Selected;
		Swap(Hotbar[H]);
		Selected = H;
		return true;
	}
	if (K < 0 || K >= NumWornKinds) return false;
	if (K == JeebItems::K_Ring)
	{
		if (Worn[K] == 0) Swap(Worn[K]);
		else if (RingB == 0) Swap(RingB);
		else Swap(Worn[K]);
		return true;
	}
	Swap(Worn[K]);
	return true;
}

bool USkillComponent::UnequipHotbar(int32 H)
{
	if (H < 0 || H >= 5 || Hotbar[H] == 0) return false;
	if (!AddInvItem(Hotbar[H], 1)) return false;
	Hotbar[H] = 0;
	return true;
}

bool USkillComponent::UnequipWorn(int32 Kind, bool bSecondRing)
{
	if (Kind < 0 || Kind >= NumWornKinds) return false;
	int32& Target = (bSecondRing && Kind == JeebItems::K_Ring) ? RingB : Worn[Kind];
	if (Target == 0) return false;
	if (JeebItems::IsArmorKind(Kind))
	{
		JeebMsg::Post(3.f, FColor::Orange, TEXT("Armour can't be taken off. Click a better piece in your pack to swap it."));
		return false;
	}
	if (!AddInvItem(Target, 1)) return false;
	Target = 0;
	return true;
}

bool USkillComponent::DiscoverLore(int32 Index)
{
	const TArray<JeebLore::FEntry>& E = JeebLore::Entries();
	if (!E.IsValidIndex(Index) || LoreFound.Contains(Index)) return false;
	LoreFound.Add(Index);
	if (GEngine)
	{
		JeebMsg::Post(5.f, FColor(255, 200, 90), FString::Printf(TEXT("New lore: %s  (press O, Lore tab)"), E[Index].Title));
	}
	return true;
}

namespace
{
	// Puts Count of Id into the slots (topping up stacks first). Returns how many did not fit.
	int32 PutInto(TArray<FInvSlot>& Arr, int32 Id, int32 Count)
	{
		const int32 Stack = JeebItems::StackSize(Id);
		for (FInvSlot& S : Arr)
		{
			if (Count <= 0) return 0;
			if (S.Id == Id && S.Count < Stack) { const int32 N = FMath::Min(Stack - S.Count, Count); S.Count += N; Count -= N; }
		}
		for (FInvSlot& S : Arr)
		{
			if (Count <= 0) return 0;
			if (S.Id == 0) { const int32 N = FMath::Min(Stack, Count); S.Id = Id; S.Count = N; Count -= N; }
		}
		return Count;
	}
}

bool USkillComponent::BankDeposit(int32 PackIdx)
{
	if (!Slots.IsValidIndex(PackIdx) || Slots[PackIdx].Id == 0) return false;
	FInvSlot& S = Slots[PackIdx];
	const int32 Left = PutInto(Bank, S.Id, S.Count);
	if (Left == S.Count) return false;
	S.Count = Left;
	if (S.Count <= 0) { S.Id = 0; S.Count = 0; }
	return true;
}

bool USkillComponent::BankWithdraw(int32 BankIdx)
{
	if (!Bank.IsValidIndex(BankIdx) || Bank[BankIdx].Id == 0) return false;
	FInvSlot& S = Bank[BankIdx];
	const int32 Left = PutInto(Slots, S.Id, S.Count);
	if (Left == S.Count) return false;
	S.Count = Left;
	if (S.Count <= 0) { S.Id = 0; S.Count = 0; }
	return true;
}

int32 USkillComponent::BankUsed() const
{
	int32 N = 0;
	for (const FInvSlot& S : Bank) if (S.Id != 0) ++N;
	return N;
}
