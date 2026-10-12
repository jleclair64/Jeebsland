#include "SkillComponent.h"
#include "Messages.h"
#include "Engine/Engine.h"
#include "Inventory.h"
#include "Lore.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"

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
	SetIsReplicatedByDefault(true);
	Slots.SetNum(JeebItems::NumSlots);
	Bank.SetNum(NumBankSlots);
	for (int32 i = 0; i < NumWornKinds; ++i) Worn[i] = 0;
	Known[static_cast<int32>(ESkillType::Attack)] = true;
	Known[static_cast<int32>(ESkillType::Defense)] = true;

	ResetToStartingKit();
}

// The kit you begin with (and wake up with after dying): a wooden sword, axe, pickaxe and fishing pole on the weapon bar, a wooden
// shield, a black cape and a full suit of bronze armour (pieces can be swapped for others, never taken off)
void USkillComponent::ResetToStartingKit()
{
	if (!HasAuth()) return;   // only the server changes the pack
	for (int32 i = 0; i < 5; ++i) Hotbar[i] = 0;
	for (int32 i = 0; i < NumWornKinds; ++i) Worn[i] = 0;
	RingB = 0;
	SetSelected(0);
	Hotbar[0] = JeebItems::GearId(JeebItems::K_Sword, 0);
	Hotbar[1] = JeebItems::GearId(JeebItems::K_Axe, 0);
	Hotbar[2] = JeebItems::GearId(JeebItems::K_Pickaxe, 0);
	Hotbar[3] = JeebItems::GearId(JeebItems::K_Rod, 0);
	Worn[JeebItems::K_Shield] = JeebItems::GearId(JeebItems::K_Shield, 0);
	Worn[JeebItems::K_Cape] = JeebItems::GearId(JeebItems::K_Cape, 5);   // black
	for (int32 K = JeebItems::K_Helmet; K <= JeebItems::K_Boots; ++K) Worn[K] = JeebItems::GearId(K, 1);   // bronze: real metal plate from the start
}

// Dying: everything you own leaves you, sorted by where it was (0 pack, 1 weapon bar, 2 worn), plus all your ingots
void USkillComponent::TakeEverything(TArray<FInvSlot>& Out, TArray<uint8>& OutWhere, int32& OutIngots)
{
	if (!HasAuth()) return;   // only the server changes the pack
	auto Put = [&](int32 Id, int32 Count, uint8 W) { if (Id != 0 && Count > 0) { FInvSlot D; D.Id = Id; D.Count = Count; Out.Add(D); OutWhere.Add(W); } };
	for (FInvSlot& S : Slots) { Put(S.Id, S.Count, 0); S.Id = 0; S.Count = 0; }
	for (int32 i = 0; i < 5; ++i) { Put(Hotbar[i], 1, 1); Hotbar[i] = 0; }
	for (int32 i = 0; i < NumWornKinds; ++i) { Put(Worn[i], 1, 2); Worn[i] = 0; }
	Put(RingB, 1, 2); RingB = 0;
	Put(PotionSlot.Id, PotionSlot.Count, 0); PotionSlot = FInvSlot();
	OutIngots = Ingots;
	Ingots = 0;
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
	if (!HasAuth()) return;   // only the server changes the pack
	const int32 OldLevel = GetLevel(Skill);
	XP[static_cast<int32>(Skill)] += Amount;
	const int32 NewLevel = GetLevel(Skill);
	if (!Known[static_cast<int32>(Skill)])
	{
		Known[static_cast<int32>(Skill)] = true;
		Say(5.f, FColor(255, 215, 120), FString::Printf(TEXT("New skill discovered: %s!"), *SkillName(Skill)));
	}

	if (IsRemoteOwned()) ClientXP(static_cast<uint8>(Skill), Amount, NewLevel > OldLevel);
	else ShowXP(Skill, Amount, NewLevel > OldLevel);
}

// The HUD shows a progress bar for the skill you last trained, for 30 seconds (but not for Attack and Defence: those level quietly)
void USkillComponent::ShowXP(ESkillType Skill, int32 Amount, bool bLevelUp)
{
	const double Now = FPlatformTime::Seconds();
	const bool bQuiet = (Skill == ESkillType::Attack || Skill == ESkillType::Defense);
	if (!bQuiet)
	{
		LastSkill = Skill;
		LastSkillTime = Now;
		LastXPGain = Amount;
	}
	if (bLevelUp)
	{
		if (!bQuiet) LevelUpTime = Now;
		JeebMsg::Post(5.f, FColor::Green, FString::Printf(TEXT("Level up! %s is now level %d."), *SkillName(Skill), GetLevel(Skill)));
	}
}

void USkillComponent::ClientXP_Implementation(uint8 Skill, int32 Amount, bool bLevelUp)
{
	// (the XP itself arrives by replication; it may land a moment after this, so the level is read when the pop-up draws)
	if (Skill < NumSkills) ShowXP(static_cast<ESkillType>(Skill), Amount, bLevelUp);
}

void USkillComponent::ClientSay_Implementation(const FString& Text, FColor Color, float Seconds)
{
	JeebMsg::Post(Seconds, Color, Text);
}

bool USkillComponent::HasAuth() const
{
	const AActor* O = GetOwner();
	return !O || O->HasAuthority();
}

bool USkillComponent::IsRemoteOwned() const
{
	const APawn* P = Cast<APawn>(GetOwner());
	return P && P->HasAuthority() && P->GetNetMode() != NM_Standalone && !P->IsLocallyControlled();
}

void USkillComponent::Say(float Seconds, const FColor& Color, const FString& Text)
{
	if (IsRemoteOwned()) ClientSay(Text, Color, Seconds);
	else if (!GetOwner() || GetOwner()->GetNetMode() != NM_DedicatedServer) JeebMsg::Post(Seconds, Color, Text);
}

void USkillComponent::SetSelected(int32 H)
{
	Selected = FMath::Clamp(H, 0, 4);
	if (HasAuth()) RepSelected = Selected;
}

void USkillComponent::OnRep_Selected()
{
	// the server moved the hand (a tool picked for gathering, a death): follow it, unless the player has just chosen a slot
	if (FPlatformTime::Seconds() - LocalSelectTime < 1.0) return;
	if (Selected != RepSelected) { Selected = RepSelected; OnGearChanged.Broadcast(); }
}

void USkillComponent::OnRep_Gear()
{
	OnGearChanged.Broadcast();   // the owner's knight redraws its armour and the weapon in hand
}

void USkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// all of it is private to the player who owns the knight (others see the knight's look through AKnightCharacter::RepLook)
	DOREPLIFETIME_CONDITION(USkillComponent, Slots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, Bank, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, Hotbar, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, RepSelected, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, Worn, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, RingB, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, PotionSlot, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, XP, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, Known, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, Ingots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, LoreFound, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USkillComponent, LastGambleHour, COND_OwnerOnly);
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
	if (!HasAuth()) return false;   // only the server changes the pack
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
	if (!HasAuth()) return 0;   // only the server changes the pack
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
	if (!HasAuth()) return false;   // only the server changes the pack
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
	if (!HasAuth()) return false;   // only the server changes the pack
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
	if (!HasAuth()) return false;   // only the server changes the pack
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
	if (!HasAuth()) return -1;   // only the server changes the pack
	const int32 T = BestArrowTier();
	if (T < 0) return -1;
	RemoveItems(JeebItems::ArrowId(T), 1);
	return T;
}

bool USkillComponent::EquipFromSlot(int32 Idx)
{
	if (!HasAuth()) return false;   // only the server changes the pack
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
		SetSelected(H);
		return true;
	}
	if (K < 0 || K >= NumWornKinds) return false;
	if (JeebItems::IsAccessoryKind(K))
	{
		// three miscellaneous slots hold any mix of rings and amulets: the first free one, or swap with the first
		int32* Misc[3] = { &Worn[JeebItems::K_Ring], &RingB, &Worn[JeebItems::K_Amulet] };
		for (int32* M : Misc) if (*M == 0) { Swap(*M); return true; }
		Swap(*Misc[0]);
		return true;
	}
	Swap(Worn[K]);
	return true;
}

bool USkillComponent::UnequipHotbar(int32 H)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	if (H < 0 || H >= 5 || Hotbar[H] == 0) return false;
	if (!AddInvItem(Hotbar[H], 1)) return false;
	Hotbar[H] = 0;
	return true;
}

bool USkillComponent::UnequipWorn(int32 Kind, bool bSecondRing)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	if (Kind < 0 || Kind >= NumWornKinds) return false;
	int32& Target = (bSecondRing && Kind == JeebItems::K_Ring) ? RingB : Worn[Kind];
	if (Target == 0) return false;
	if (JeebItems::IsArmorKind(Kind))
	{
		Say(3.f, FColor::Orange, TEXT("Armour can't be taken off. Click a better piece in your pack to swap it."));
		return false;
	}
	if (!AddInvItem(Target, 1)) return false;
	Target = 0;
	return true;
}

bool USkillComponent::DiscoverLore(int32 Index)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	const TArray<JeebLore::FEntry>& E = JeebLore::Entries();
	if (!E.IsValidIndex(Index) || LoreFound.Contains(Index)) return false;
	LoreFound.Add(Index);
	Say(5.f, FColor(255, 200, 90), FString::Printf(TEXT("New lore: %s  (press O, Lore tab)"), E[Index].Title));
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
	if (!HasAuth()) return false;   // only the server changes the pack
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
	if (!HasAuth()) return false;   // only the server changes the pack
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

bool USkillComponent::SwapInvHotbar(int32 InvIdx, int32 H)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	if (!Slots.IsValidIndex(InvIdx) || H < 0 || H >= 5) return false;
	FInvSlot& S = Slots[InvIdx];
	if (S.Id != 0 && !(JeebItems::IsGear(S.Id) && JeebItems::IsWeaponKind(JeebItems::KindOf(S.Id)))) return false;   // only weapons go on the bar
	if (S.Id == 0 && Hotbar[H] == 0) return false;
	const int32 FromBar = Hotbar[H];
	Hotbar[H] = S.Id;
	S.Id = FromBar;
	S.Count = FromBar ? 1 : 0;
	return true;
}

bool USkillComponent::SwapInv(int32 A, int32 B)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	if (!Slots.IsValidIndex(A) || !Slots.IsValidIndex(B) || A == B) return false;
	Swap(Slots[A], Slots[B]);
	return true;
}

bool USkillComponent::SwapHotbar(int32 A, int32 B)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	if (A < 0 || B < 0 || A >= 5 || B >= 5 || A == B) return false;
	Swap(Hotbar[A], Hotbar[B]);
	return true;
}

void USkillComponent::FillPracticePack()
{
	if (!HasAuth()) return;   // only the server changes the pack
	FRandomStream R(5150);
	using namespace JeebItems;
	static const int32 Kinds[] = { K_Sword, K_Axe, K_Pickaxe, K_Spear, K_Broadsword, K_BattleAxe, K_Katana, K_Wand, K_Staff, K_Bow,
		K_Shield, K_Helmet, K_Chest, K_Shoulders, K_Gloves, K_Boots, K_Cape, K_Ring, K_Amulet };
	// three of each potion: swiftness and vigour in the pack, healing in the potion slot
	Slots[0].Id = JeebItems::PotionId(JeebItems::P_Speed); Slots[0].Count = 3;
	Slots[1].Id = JeebItems::PotionId(JeebItems::P_Vigour); Slots[1].Count = 3;
	PotionSlot.Id = JeebItems::PotionId(JeebItems::P_Heal); PotionSlot.Count = 3;
	// and one of each new shield style to try (iron)
	Slots[2].Id = JeebItems::ShieldId(1, 2); Slots[2].Count = 1;
	Slots[3].Id = JeebItems::ShieldId(2, 2); Slots[3].Count = 1;
	for (int32 i = 0; i < Slots.Num() - 3; ++i)   // the last three pack slots stay empty
	{
		if (Slots[i].Id != 0) continue;
		const int32 K = Kinds[R.RandRange(0, UE_ARRAY_COUNT(Kinds) - 1)];
		Slots[i].Id = GearId(K, R.RandRange(0, 4));
		Slots[i].Count = 1;
	}
}

int32* USkillComponent::WornSlotPtr(int32 Kind, bool bSecond)
{
	if (Kind == JeebItems::K_Ring) return bSecond ? &RingB : &Worn[JeebItems::K_Ring];
	if (Kind < 0 || Kind >= NumWornKinds) return nullptr;
	return &Worn[Kind];
}

bool USkillComponent::SwapInvWorn(int32 InvIdx, int32 Kind, bool bSecond)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	if (!Slots.IsValidIndex(InvIdx)) return false;
	FInvSlot& S = Slots[InvIdx];
	if (S.Id == 0 || !JeebItems::IsGear(S.Id)) return false;
	const int32 IK = JeebItems::KindOf(S.Id);
	const bool bMiscSlot = (Kind == JeebItems::K_Ring || Kind == JeebItems::K_Amulet);
	if (bMiscSlot ? !JeebItems::IsAccessoryKind(IK) : IK != Kind) return false;   // the item has to belong in that slot
	int32* Target = WornSlotPtr(Kind, bSecond);
	if (!Target) return false;
	const int32 Old = *Target;
	*Target = S.Id;
	S.Id = Old;
	S.Count = Old ? 1 : 0;
	return true;
}

bool USkillComponent::SwapInvPotion(int32 InvIdx)
{
	if (!HasAuth()) return false;   // only the server changes the pack
	if (!Slots.IsValidIndex(InvIdx)) return false;
	FInvSlot& S = Slots[InvIdx];
	if (S.Id != 0 && !JeebItems::IsPotion(S.Id)) return false;   // only potions go in the potion slot
	if (S.Id == 0 && PotionSlot.Id == 0) return false;
	Swap(S, PotionSlot);
	return true;
}

void USkillComponent::RestoreSkills(const TArray<int32>& InXP, const TArray<uint8>& InKnown, int32 InIngots)
{
	if (!HasAuth()) return;   // only the server changes the pack
	for (int32 i = 0; i < NumSkills; ++i)
	{
		XP[i] = InXP.IsValidIndex(i) ? FMath::Max(0, InXP[i]) : 0;
		Known[i] = (InKnown.IsValidIndex(i) && InKnown[i] != 0) || i == static_cast<int32>(ESkillType::Attack) || i == static_cast<int32>(ESkillType::Defense);
	}
	Ingots = InIngots;
}
