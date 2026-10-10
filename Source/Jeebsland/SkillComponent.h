#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SkillComponent.generated.h"

UENUM(BlueprintType)
enum class ESkillType : uint8
{
	Mining,
	Woodcutting,
	Smithing,
	Fishing,
	Attack,
	Defense,
	Archery,
	Magic,
	Lockpicking
};

UENUM(BlueprintType)
enum class EItemType : uint8
{
	CopperOre,
	Coal,
	Logs,
	Diamond,
	Bacon,
	PineLogs,
	RedwoodLogs,
	Perch,
	Trout,
		GoldenCarp,
	Hide,
	Bones
};

// One inventory slot: what is in it (see Inventory.h for ids) and how many
struct FInvSlot { int32 Id = 0; int32 Count = 0; };

// Tracks skill XP, the inventory and what you wear / hold. Lives on the character; later the server will own this.
//
// Levels: level 1 costs a base amount of XP (Mining 20, Woodcutting 40, Smithing 30, Fishing 25) and every level after
// costs 20% more than the one before it.
UCLASS()
class USkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkillComponent();

	static constexpr int32 NumSkills = 9;
	static constexpr int32 NumWornKinds = 24;   // indexed by JeebItems::EKind

	void AddXP(ESkillType Skill, int32 Amount);
	bool AddItem(EItemType Item, int32 Amount = 1);   // false (and nothing added) if it does not fit
	bool CanAdd(int32 Id, int32 Amount) const;
	bool AddInvItem(int32 Id, int32 Amount = 1);
	int32 SellAll(EItemType Item);                    // removes every unit, pays ingots, returns the payment
	int32 GetIngots() const { return Ingots; }
	bool SpendIngots(int32 Amount);
	void AddIngots(int32 Amount) { Ingots += Amount; }

	// Lore fragments heard so far (indices into JeebLore::Entries()). Returns true the first time.
	TSet<int32> LoreFound;
	bool DiscoverLore(int32 Index);
	int32 GetXPTotal(ESkillType Skill) const { return XP[static_cast<int32>(Skill)]; }

	// The bank: 300 slots, kept at the vault. Click a pack item to deposit it, click a bank item to take it out.
	static constexpr int32 NumBankSlots = 300;
	TArray<FInvSlot> Bank;
	bool BankDeposit(int32 PackIdx);     // the whole stack; false if the bank has no room
	bool BankWithdraw(int32 BankIdx);    // the whole stack (or as much as fits in the pack)
	int32 BankUsed() const;

	// The 16 inventory slots (drawn by the HUD, changed by the shops and equipping)
	TArray<FInvSlot> Slots;

	// The weapon bar (5 slots, keys 1-5) and what is worn, by kind. Rings use Worn[K_Ring] and RingB.
	int32 Hotbar[5] = { 0, 0, 0, 0, 0 };
	int32 Selected = 0;
	int32 Worn[NumWornKinds] = {};
	int32 RingB = 0;
	int32 SelectedItem() const { return Hotbar[Selected]; }

	bool EquipFromSlot(int32 InvIdx);                 // hold or wear the gear in that inventory slot (swaps with what is there)
	bool UnequipHotbar(int32 H);                      // back into the inventory
	bool UnequipWorn(int32 Kind, bool bSecondRing = false);

	// Counting and taking items by id
	int32 CountItem(int32 Id) const;
	bool RemoveItems(int32 Id, int32 Count);
	int32 CountWood() const;                          // any logs
	bool TakeWood(int32 Count);                       // takes ordinary logs first, then pine, then redwood
	int32 BestArrowTier() const;                      // -1 if you carry no arrows
	int32 TakeBestArrow();                            // uses one arrow of the best tier; -1 if none

	int32 GetItemCount(EItemType Item) const;
	int32 GetLevel(ESkillType Skill) const;
	// XPIntoLevel / XPNeeded describe progress through the current level
	void GetProgress(ESkillType Skill, int32& Level, int32& XPIntoLevel, int32& XPNeeded) const;
	static FString SkillName(ESkillType Skill);

private:
	int32 GetXP(ESkillType Skill) const { return XP[static_cast<int32>(Skill)]; }
	static float BaseXP(ESkillType Skill);

	int32 XP[NumSkills] = {};
	UPROPERTY(VisibleAnywhere, Category = "Inventory") int32 Ingots = 500; // testing money: lower or remove later
};
