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
	Bones,
	// (2026-10-30, the tier redo: new ores and gems; always add at the END, saves store these as numbers)
	TinOre,
	IronOre,
	SilverOre,
	GoldOre,
	Onyx,
	Emerald
};

// Sizes of the skill and worn-gear arrays (outside the class so the replication code generator can see them)
constexpr int32 JeebNumSkills = 9;
constexpr int32 JeebNumWornKinds = 24;

// One inventory slot: what is in it (see Inventory.h for ids) and how many
USTRUCT()
struct FInvSlot
{
	GENERATED_BODY()
	FInvSlot() {}
	FInvSlot(int32 InId, int32 InCount) : Id(InId), Count(InCount) {}
	UPROPERTY() int32 Id = 0;
	UPROPERTY() int32 Count = 0;
	bool operator==(const FInvSlot& O) const { return Id == O.Id && Count == O.Count; }
};

// Tracks skill XP, the inventory and what you wear / hold. Lives on the character.
// ONLINE: the server owns all of it. Only the server changes it (every changing function below does nothing on a client);
// it replicates to the owning player only. Players ask for changes through AKnightCharacter::Inv (a request the server checks).
//
// Levels: level 1 costs a base amount of XP (Mining 20, Woodcutting 40, Smithing 30, Fishing 25) and every level after
// costs 20% more than the one before it.
UCLASS()
class USkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkillComponent();

	static constexpr int32 NumSkills = JeebNumSkills;
	static constexpr int32 NumWornKinds = JeebNumWornKinds;   // indexed by JeebItems::EKind

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// True where changes are allowed: the server, or a single-player game
	bool HasAuth() const;
	// A message for this player: shown here, or sent down to their machine when the server is running it for someone else
	void Say(float Seconds, const FColor& Color, const FString& Text);

	void AddXP(ESkillType Skill, int32 Amount);

	// The skill trained most recently (for the HUD's pop-up progress bar); times are FPlatformTime::Seconds()
	ESkillType LastSkill = ESkillType::Mining;
	double LastSkillTime = -1000.0;
	double LevelUpTime = -1000.0;
	int32 LastXPGain = 0;

	// The store's mystery satchel can be opened once per real-world hour (hours since 1970)
	UPROPERTY(Replicated) int64 LastGambleHour = -1;
	bool AddItem(EItemType Item, int32 Amount = 1);   // false (and nothing added) if it does not fit
	bool CanAdd(int32 Id, int32 Amount) const;
	bool AddInvItem(int32 Id, int32 Amount = 1);
	int32 SellAll(EItemType Item);                    // removes every unit, pays ingots, returns the payment
	// Skills are discovered by doing: only Attack and Defence are known at the start; the rest appear in the skills window the
	// first time you earn XP in them.
	UPROPERTY(Replicated) uint8 Known[JeebNumSkills] = {};
	bool IsKnown(ESkillType S) const { return Known[static_cast<int32>(S)] != 0; }
	int32 GetIngots() const { return Ingots; }
	bool SpendIngots(int32 Amount);
	void AddIngots(int32 Amount) { if (HasAuth()) Ingots += Amount; }

	// Lore fragments heard so far (indices into JeebLore::Entries()). Returns true the first time.
	UPROPERTY(Replicated) TArray<int32> LoreFound;
	bool DiscoverLore(int32 Index);

	// Map fog: the ground this character has seen, as a FogN x FogN grid over the whole world (JeebWorld bounds; about 12 m
	// cells). The server marks it as the knight walks (Explore); it is saved with the character and never lost on death.
	static constexpr int32 FogN = 192;
	static constexpr float FogRevealRadius = 4500.f;   // how far around you the fog clears
	UPROPERTY(Replicated) TArray<uint8> Explored;
	void Explore(const FVector& At);
	bool IsExplored(float X, float Y) const;
	int32 GetXPTotal(ESkillType Skill) const { return XP[static_cast<int32>(Skill)]; }

	// The bank: 300 slots, kept at the vault. Click a pack item to deposit it, click a bank item to take it out.
	static constexpr int32 NumBankSlots = 300;
	UPROPERTY(Replicated) TArray<FInvSlot> Bank;
	bool BankDeposit(int32 PackIdx);     // the whole stack; false if the bank has no room
	bool BankWithdraw(int32 BankIdx);    // the whole stack (or as much as fits in the pack)
	bool BankDepositHot(int32 H);        // a weapon straight from the weapon bar
	int32 BankUsed() const;

	// The 16 inventory slots (drawn by the HUD, changed by the shops and equipping)
	UPROPERTY(Replicated) TArray<FInvSlot> Slots;

	// The weapon bar (5 slots, keys 1-5) and what is worn, by kind. Rings use Worn[K_Ring] and RingB.
	UPROPERTY(ReplicatedUsing = OnRep_Gear) int32 Hotbar[5] = { 0, 0, 0, 0, 0 };
	// Which weapon-bar slot is in hand. The player's own machine changes it straight away (keys 1-5) and tells the server;
	// the server's choice comes back in RepSelected (and wins unless the player has just picked one themselves).
	int32 Selected = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Selected) int32 RepSelected = 0;
	double LocalSelectTime = -100.0;
	void SetSelected(int32 H);   // server: also sends it down
	// Fires on the owning player's machine when worn gear, the weapon bar or the hand changes (the knight redraws itself)
	DECLARE_MULTICAST_DELEGATE(FOnGearChanged);
	FOnGearChanged OnGearChanged;
	UPROPERTY(ReplicatedUsing = OnRep_Gear) int32 Worn[JeebNumWornKinds] = {};
	UPROPERTY(ReplicatedUsing = OnRep_Gear) int32 RingB = 0;
	int32 SelectedItem() const { return Hotbar[Selected]; }

	bool EquipFromSlot(int32 InvIdx);                 // hold or wear the gear in that inventory slot (swaps with what is there)
	bool UnequipHotbar(int32 H);                      // back into the inventory
	// Drag and drop between the pack and the weapon bar (and within each). Only weapons and tools can go on the bar.
	bool SwapInvHotbar(int32 InvIdx, int32 H);   // the bar slot and the pack slot trade places (either may be empty)
	bool SwapInv(int32 A, int32 B);
	bool SwapHotbar(int32 A, int32 B);
	// Armour and other worn gear: the pack item and the worn slot trade places if the item belongs there
	int32* WornSlotPtr(int32 Kind, bool bSecond);   // the int that holds that worn slot (the three misc slots are Worn[K_Ring], RingB and Worn[K_Amulet])
	bool SwapInvWorn(int32 InvIdx, int32 Kind, bool bSecond);
	void FillPracticePack();                     // stocks the pack with random weapons and armour (for trying out swaps)
	// Dying: everything you own (pack, weapon bar, worn gear, potions, ingots) leaves you, sorted by where it was
	// (0 pack, 1 weapon bar, 2 worn); then you start again with the default kit. (Skills and levels are kept.)
	void TakeEverything(TArray<FInvSlot>& Out, TArray<uint8>& OutWhere, int32& OutIngots);
	void ResetToStartingKit();
	static constexpr int32 StartSteelArrows = 99, StartFireArrows = 99;   // (the arrows in the starting kit)
	void RefreshCape();   // the worn cape is always the one for your PvP rank
	// Loading a save: puts back XP, which skills are known and the ingots (the rest is public)
	void RestoreSkills(const TArray<int32>& InXP, const TArray<uint8>& InKnown, int32 InIngots);
	// The potion slot (beside the weapon bar): one stack of potions ready to drink with Q
	UPROPERTY(Replicated) FInvSlot PotionSlot;
	bool SwapInvPotion(int32 InvIdx);   // a pack slot and the potion slot trade places (only potions go in it)
	bool UnequipWorn(int32 Kind, bool bSecondRing = false);

	// Counting and taking items by id
	int32 CountItem(int32 Id) const;
	bool RemoveItems(int32 Id, int32 Count);
	int32 CountWood() const;                          // any logs
	bool TakeWood(int32 Count);                       // takes ordinary logs first, then pine, then redwood
	int32 BestArrowTier() const;                      // -1 if you carry no arrows
	int32 TakeBestArrow();                            // uses one arrow of the best tier; -1 if none

	int32 GetItemCount(EItemType Item) const;
	static constexpr int32 MaxLevel = 99;
	static int32 XPForLevel(ESkillType Skill, int32 Level);   // total XP needed to reach a level
	void SetAllMaxLevel();   // testing only: every skill at 99 (see MaxLevelAccounts in DefaultGame.ini)
	int32 GetLevel(ESkillType Skill) const;
	// XPIntoLevel / XPNeeded describe progress through the current level
	void GetProgress(ESkillType Skill, int32& Level, int32& XPIntoLevel, int32& XPNeeded) const;
	static FString SkillName(ESkillType Skill);

	// Down to the owning player: a message, and the XP pop-up (the server earns the XP; their HUD shows it)
	UFUNCTION(Client, Reliable) void ClientSay(const FString& Text, FColor Color, float Seconds);
	UFUNCTION(Client, Reliable) void ClientXP(uint8 Skill, int32 Amount, bool bLevelUp);

private:
	int32 GetXP(ESkillType Skill) const { return XP[static_cast<int32>(Skill)]; }
	static float BaseXP(ESkillType Skill);
	bool IsRemoteOwned() const;   // the server is running this for a player on another machine
	void ShowXP(ESkillType Skill, int32 Amount, bool bLevelUp);
	UFUNCTION() void OnRep_Gear();
	UFUNCTION() void OnRep_Selected();

	UPROPERTY(Replicated) int32 XP[JeebNumSkills] = {};
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Inventory") int32 Ingots = 500; // testing money: lower or remove later
};
