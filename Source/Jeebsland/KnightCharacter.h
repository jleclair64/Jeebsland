#pragma once

#include "CoreMinimal.h"
#include "JeebslandCharacter.h"
#include "SkillComponent.h"
#include "KnightDesign.h"
#include "Inventory.h"
#include "KnightCharacter.generated.h"

class UStaticMeshComponent;
class UInputMappingContext;
class UInputAction;
class USkillComponent;
class AResourceNode;
class UInputModifierScalar;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class ANpc;
class AHorse;
class UHealthComponent;
class ACottage;
class ALootDrop;

// The knight is built from slots (helmet, body, shoulders, boots, cape, gloves, shield); what is worn in a slot sets its
// tier colours and extra parts. The shapes themselves are modelled in KnightDesign.h; weapons in WeaponDesign.h.
struct FArmorPart
{
	UPrimitiveComponent* Comp = nullptr;
	int32 Slot = -1;                 // KnightDesign::ESlot
	EArmorRole Role = EArmorRole::Plate;
	int32 MinTier = 0, MaxTier = 9;  // shown only on these tiers
	FLinearColor Fixed = FLinearColor::White;
	bool bFixedMetal = false;
	bool bBare = false;              // skin / hair: shown only while the slot is empty
	bool bFixedColor = false;        // always its Fixed colour, matte (black eye openings, the red brush)
	int32 Style = 0;                 // shields: which shield style the piece belongs to
	float Shade = 1.f;
	bool bModel = false;             // a piece of the Meshy model (KnightModel.h): painted by M_Knight, tinted for the tier
};

// Everything a player can do to their pack, gear and money. ONLINE the player's machine only asks (AKnightCharacter::Inv sends a
// request); the server checks it, carries it out, and the result comes back by replication. Single player: done straight away.
// A, B, C, Target and At mean different things per request (below).
UENUM()
enum class EInvOp : uint8
{
	Equip,          // A pack slot: hold or wear it
	UnequipHot,     // A bar slot back into the pack
	UnequipWorn,    // A kind, B 1 = the second misc slot
	SwapInvHot,     // A pack slot, B bar slot
	SwapInv,        // A, B pack slots
	SwapHot,        // A, B bar slots
	SwapInvWorn,    // A pack slot, B kind, C 1 = the second misc slot
	SwapInvPotion,  // A pack slot
	Select,         // A the bar slot to hold
	DropPack,       // A pack slot, B how many
	DropIngots,     // A how many
	DropHot,        // A bar slot
	BankDeposit,    // A pack slot (-1: everything)
	BankWithdraw,   // A bank slot
	SellAll,        // A the EItemType
	SellSlot,       // A pack slot, B the id the player saw there
	Buy,            // A ware index, B the id the player saw
	Gamble,
	Smith,          // A kind (-1 arrows), B tier
	Drink,
	LootTakeAll,    // Target: the drop
	LootTakeOne,    // Target, A entry (-1: its ingots)
	LootEquip,      // Target, A entry, B bar slot (or -1), C worn kind (+100 for the second misc slot)
	HutLoot,        // At: the cottage
	PickLock,       // At: the cottage
	Gather,         // At: the rock, tree or fishing spot
	Lore,           // A lore entry (heard from a villager)
	BankDepositHot, // A bar slot: that weapon into the bank
	ToggleDoor,     // At: the cottage (open or shut its door)
};

// Knight built from basic shapes. Also creates its own keyboard/mouse
// controls in code, so no input assets are needed.
UCLASS()
class AKnightCharacter : public AJeebslandCharacter
{
	GENERATED_BODY()

public:
	AKnightCharacter();

	virtual void Tick(float DeltaSeconds) override;
	// On a horse, steering is like a car: W goes forward along the horse, A and D turn it (it cannot slide sideways), S only brakes.
	virtual void DoMove(float Right, float Forward) override;
	virtual void Jump() override; // also stands you up from a bench, and ends a slide
	// Online: what the knight stands on is never used as its "movement base" unless it is a shared (replicated) actor. The
	// ground, Blackrock, the towers and forts are built separately on every machine, so the server can't name them to the
	// player's machine: a correction that names one is thrown away there, and the two copies of the knight drift apart
	// until the knight jumps (then it snaps to wherever the server had it).
	using ACharacter::SetBase;
	virtual void SetBase(FMovementBaseInterfaceData* Data, const FName BoneName = NAME_None, bool bNotifyActor = true) override;

	// For the HUD
	float GetStaminaFraction() const { return Stamina / MaxStamina; }
	bool IsExhausted() const { return bExhausted; }

	// Mouse/stick look passes through here, so the sensitivity setting scales it directly
	virtual void AddControllerYawInput(float Val) override;
	virtual void AddControllerPitchInput(float Val) override;

	// Called by resource nodes: start/stop the working animation
	void BeginGathering(AResourceNode* Node, ESkillType Skill);
	void EndGathering();

	// Screens: Tab = side-panel cursor (or close a screen), K = skills board, E at the store / smithy = trade, O = settings
	bool IsUIOpen() const { return bSettingsOpen || bSkillsOpen || bBankOpen || bInventoryOpen || bShopOpen || bSmithOpen || bDialogueOpen; }
	bool IsInventoryOpen() const { return bInventoryOpen; }
	bool IsSkillsOpen() const { return bSkillsOpen; }
	bool IsBankOpen() const { return bBankOpen; }
	void OpenBank();
	void CloseBank();
	void ToggleSkills();
	bool IsShopOpen() const { return bShopOpen; }
	bool IsSmithOpen() const { return bSmithOpen; }
	bool IsDialogueOpen() const { return bDialogueOpen; }
	void ToggleInventory();
	void ShowPackTab(bool bSkills);   // switch the open pack window between its Inventory and Skills tabs
	// The side panel (lower left): Tab frees the cursor to use it, Caps Lock steps through its tabs (Inventory, Apparel, Skills)
	bool IsPanelCursor() const { return bPanelCursor; }
	int32 GetPanelTab() const { return PanelTab; }
	void SetPanelCursor(bool bOn) { if (bPanelCursor != bOn) { bPanelCursor = bOn; RefreshUIMode(); } }
	void SetPanelTab(int32 T) { PanelTab = FMath::Clamp(T, 0, 2); }
	void CyclePanelTab() { PanelTab = (PanelTab + 1) % 3; }
	// Dropping things on the ground (they land in a bag in front of you; press E to pick them up again; it fades after 4 minutes)
	void DropFromPack(int32 SlotIdx, int32 Count);
	void DropIngots(int32 Count);
	void DropFromHotbar(int32 H);
	void DropBag(const TArray<FInvSlot>& Items, int32 Ingots);
	// Potions: Q drinks the one in the potion slot, if that kind is ready (each kind has its own cooldown)
	void DrinkPotion();
	float GetPotionCooldownLeft(int32 Type) const;
	// Radar potion, for the minimap: seconds left (0 = off) and the red dots to draw (world X, Y)
	float GetRadarLeft() const;
	const TArray<FVector2D>& GetRadarDots() const { return RadarDots; }
	// A sack on the ground opened with E: its windows show beside yours until you walk away or put the cursor away
	class ALootDrop* GetLootTarget() const { return LootTarget.Get(); }
	void OpenLoot(class ALootDrop* Drop);
	void CloseLoot() { LootTarget = nullptr; }
	// Dying, of anything: everything you own goes into a sack where you fell, and you wake in Blackrock in the starting kit
	void Die();
	// Settings: show the side panel all the time (on), or only while the cursor is out with Tab (off)
	bool GetPanelAlways() const { return bPanelAlways; }
	void SetPanelAlways(bool bOn);
	bool bPitchLimitsSet = false;
	void OpenShop();
	void CloseShop();
	void OpenSmith();
	void CloseSmith();
	// Conversation with a villager: one line per click, then the trade screen (if they have one)
	void StartDialogue(ANpc* Npc);
	void AdvanceDialogue();
	void CloseDialogue();
	bool GetDialogue(FString& OutName, FString& OutLine, bool& bOutLast) const;

	// Ask for a change to the pack, gear or money (see EInvOp). Online it goes to the server; single player it happens now.
	void Inv(EInvOp Op, int32 A = 0, int32 B = 0, int32 C = 0, AActor* Target = nullptr, FVector At = FVector::ZeroVector);
	// Down from the server to this player: a line for the open shop screen, gathering starting or stopping (the server runs
	// the gathering; this machine only plays the animation), a potion drunk, a lock picked
	UFUNCTION(Client, Reliable) void ClientShopNote(const FString& Text, float Seconds);
	UFUNCTION(Client, Reliable) void ClientGather(FVector_NetQuantize At, uint8 Skill, bool bStart);
	UFUNCTION(Client, Reliable) void ClientDrank(uint8 Type);
	UFUNCTION(Client, Reliable) void ClientUnlockHut(FVector_NetQuantize At);
	void ShopNote(const FString& Text, float Seconds = 3.f);   // shown at the bottom of the shop or smithy screen

	void EquipFromInventory(int32 Idx);          // hold or wear the gear in that slot (swaps with what you have on)
	void UnequipHotbar(int32 H);
	void UnequipWorn(int32 Kind, bool bSecondRing = false);
	void SelectHotbar(int32 H);
	void RefreshEquipment();                     // redraws armour, shield and the weapon in your hand
	bool ShieldActive() const;                   // a shield is worn and the weapon in hand allows one
	int32 SelectedKind() const;                  // kind of the weapon in hand, or -1
	// Gathering: puts the right tool in hand (and tells you if you have none). OutTimeMult speeds up better tools.
	bool PrepareTool(ESkillType Skill, float& OutTimeMult);
	float GetStaminaMult() const;
	// Riding: T calls the horse, E beside it mounts. The bar counts 100 down while sprinting, then refills before the next sprint.
	bool IsMounted() const { return bMounted; }
	float GetHorseEnergy() const { return HorseEnergy; }   // 0 - 100
	float GetHorseHealth() const { return HorseHealth; }
	float GetHorseMaxHealth() const { return HorseMaxHealth; }
	// The horse is hurt separately from the knight (its own hit boxes come later). Heals only after 10 s unhurt.
	void HurtHorse(float Amount);
	bool IsHorseTired() const { return bHorseTired; }
	// Shield guard, for the HUD
	float GetGuardFraction() const { return GuardMax > 0.f ? GuardPool / GuardMax : 0.f; }
	bool IsGuardBroken() const { return bGuardBroken; }
	bool IsBlocking() const { return bBlocking; }
	USkillComponent* GetSkills() const { return Skills; }
	// Text for the "Press E ..." prompt, or false if nothing is in reach
	bool GetInteractPrompt(FString& OutText) const;
	// Where the floating E prompt hangs: just in front of and above the thing you can use. bOutE is false for things that are not an E action (a locked door you cannot pick, a place to fish).
	bool GetInteractAnchor(FVector& Out, bool& bOutE) const;

	// Combat: health, damage dealt and taken
	float GetHealth() const { return Health; }
	int32 GetMaxHealth() const { return MaxHealth; }
	// Something hits you for Raw points; armour, Defence level and a raised shield soak some of it
	// bEnvironmental: a hazard (poison smoke, fire): armour and shields do not stop it, and it trains no Defence.
	void Hurt(int32 Raw, AActor* From, bool bEnvironmental = false);
	// You (or your arrow / bolt) hit something: applies the damage and awards skill XP
	void DealDamage(UHealthComponent* Target, int32 Dmg, bool bCrit, ESkillType Skill);

	// The Escape screen (map, settings ...): Escape or O
	// Save now (the button on the map): online the server writes it (at most every few seconds)
	void SaveNow();
	UFUNCTION(Server, Reliable) void ServerSaveNow();
	double LastSaveAsk = -100.0;
	void ToggleSettings();
	bool IsSettingsOpen() const { return bSettingsOpen; }
	float GetLookSensitivity() const { return LookSensitivity; }
	void SetLookSensitivity(float Value);

	// Graphics options (also in the settings screen)
	// Graphics: 0 Lowest (for weak PCs: no clouds, simple shadows, FXAA), 1 Low (the start). No Lumen level any more.
	int32 GetGraphicsLevel() const { return GraphicsLevel; }
	void SetGraphicsLevel(int32 Level);
	float GetRenderScale() const { return RenderScale; }
	void SetRenderScale(float Value);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void Autosave();   // writes the player to disk (every minute, and when the game ends)
	bool UsesLocalSave() const;   // single player only: online, the server will own characters

	// Online: what this knight wears and holds (Worn[0..23], then RingB, then the held item), so other players see it.
	// The server works it out from the pack it owns (SendLook, every tick, cheap) and passes it on to everyone else.
	UPROPERTY(ReplicatedUsing = OnRep_Look) TArray<int32> RepLook;
	UFUNCTION() void OnRep_Look();
	void SendLook();

	// Requests (see Inv). DoInv runs on the server (or in single player) and checks everything before doing it.
	UFUNCTION(Server, Reliable) void ServerInv(EInvOp Op, int32 A, int32 B, int32 C, AActor* Target, FVector_NetQuantize At);
	void DoInv(EInvOp Op, int32 A, int32 B, int32 C, AActor* Target, const FVector& At);
	bool NearTrader(uint8 Role) const;   // a villager of that ENpcRole is close enough to trade with

	// Online combat and moves (KnightCharacterNet.cpp). The server decides every hit; swings, guarding, crouching, rolls and
	// slides are copied to the server (so it moves the knight the same way) and shown to everyone else.
	UFUNCTION(Server, Reliable) void ServerAttack(uint8 Style, int8 Dir, float Charge);
	UFUNCTION(NetMulticast, Unreliable) void MulticastAttack(uint8 Style, int8 Dir, float Charge);
	UFUNCTION(Server, Reliable) void ServerState(uint8 Flags);   // 1 sprinting, 2 crouching, 4 shield button held
	UFUNCTION(Server, Reliable) void ServerRoll(FVector_NetQuantizeNormal Dir);
	UFUNCTION(Server, Reliable) void ServerSlide(FVector_NetQuantizeNormal Dir, float Speed);
	UFUNCTION(Client, Reliable) void ClientDied(FVector_NetQuantize Wake, float Yaw);
	void SendState();
	uint8 SentState = 0;
	double LastServerAttack = -10.0;
	// What other players' machines need to animate this knight: 1 crouched, 2 guarding, 4 rolling, 8 sliding
	UPROPERTY(ReplicatedUsing = OnRep_MoveFlags) uint8 RepMoveFlags = 0;
	UFUNCTION() void OnRep_MoveFlags();
	void BeginSlide(const FVector& Dir, float Speed);
	// Climbing online: the player's machine says when it gets on and off a ladder; meanwhile the server trusts its position
	UFUNCTION(Server, Reliable) void ServerClimb(bool bOn);
	FTimerHandle ClimbTrustTimer;
	float ClimbGroundedFor = 0.f;
	bool bUIModeSet = false;
	void ApplyPotion(int32 Type);       // the potion's effect (both on the server and on the player's machine)
	void ApplyLook(const TArray<int32>& Look);   // another player's knight: dress our copy of it
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	FTimerHandle AutosaveTimer;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void StartSprint();
	void StopSprint();
	void Interact();            // left click: gather if something is in front, otherwise swing the sword
	void StartCrouch();         // hold Ctrl
	void StopCrouch();
	void InteractReleased();    // letting go of left click: a charged spear thrust
	enum class EAtkStyle : uint8 { Slash, Thrust, Cast };
	void StartAttack(EAtkStyle Style, int32 Dir, float Charge);
	void AttackLands();         // the moment a swing, thrust or cast lands
	void TryStartClimb();       // walking into a ladder
	void UpdateClimb(float Dt);
	void ExitClimb(bool bAtTop, bool bAtBottom);
	void StartBlock();          // right click (hold)
	void StopBlock();
	void StartSlide();          // C while sprinting
	void StartRoll();           // V or Alt
	void DodgeLeft() { TapKey(0); }     // A, D, W and S each tapped twice in a row (nothing else pressed between): a roll that way
	void DodgeRight() { TapKey(1); }
	void DodgeFwd() { TapKey(2); }
	void DodgeBack() { TapKey(3); }
	void TapKey(int32 Key);   // 0 A, 1 D, 2 W, 3 S
	int32 LastTapKey = -1; double LastTapTime = -10.0;
	FVector PendingRollDir = FVector::ZeroVector;
	void OnInteractKey();       // E: sit, trade, talk, mount, open doors ...
	void UseDoor(ACottage* Hut);   // open, close, or try to pick the lock
	void CallHorse();           // T
	void Mount(AHorse* H);
	void Dismount();
	void SitOnSeat(int32 SeatIdx);
	void StandUp();
	void OnInventoryKey();      // Tab: closes an open screen, otherwise frees or hides the cursor for the side panel
	void OnSkillsKey() { ToggleSkills(); }
	void RefreshUIMode();
	void OnSettingsKey() { ToggleSettings(); }
	void Select1() { SelectHotbar(0); }          // keys 1-5 pick the weapon bar slot
	void Select2() { SelectHotbar(1); }
	void Select3() { SelectHotbar(2); }
	void Select4() { SelectHotbar(3); }
		void Select5() { SelectHotbar(4); }
	void ScrollUp() { CycleHotbar(-1); }
	void ScrollDown() { CycleHotbar(1); }
	void CycleHotbar(int32 Dir);
	void ApplyArmorSlot(int32 Slot);
	void BuildKnight();   // creates every armour part from KnightDesign.h
	void RebuildHeld();   // the weapon(s) in your hands
	UProceduralMeshComponent* MakeMeshComp(USceneComponent* Attach, int32 PivotId, const KM::FMD& Md);
	void ApplyPartMaterial(FArmorPart& P, const FLinearColor& Color, bool bMetal);

private:
	UPROPERTY() UInputMappingContext* KnightContext = nullptr;
	UPROPERTY() UInputAction* SprintAction = nullptr;
	UPROPERTY() UInputAction* InteractAction = nullptr;
	UPROPERTY() USkillComponent* Skills = nullptr;
	UPROPERTY() UInputAction* SettingsAction = nullptr;
	UPROPERTY() UInputAction* CrouchAction = nullptr;
	UPROPERTY() UInputAction* BlockAction = nullptr;
	UPROPERTY() UInputAction* SlideAction = nullptr;
	UPROPERTY() UInputAction* RollAction = nullptr;
	UPROPERTY() UInputAction* StrafeAction = nullptr;   // mouse wheel click (or X): strafe mode on/off
	UPROPERTY() UInputAction* DodgeLAction = nullptr;
	UPROPERTY() UInputAction* DodgeRAction = nullptr;
	UPROPERTY() UInputAction* DodgeFAction = nullptr;
	UPROPERTY() UInputAction* DodgeBAction = nullptr;
	UPROPERTY() UInputAction* SitAction = nullptr;
	UPROPERTY() UInputAction* InventoryAction = nullptr;
	UPROPERTY() UInputAction* CallHorseAction = nullptr;
		UPROPERTY() UInputAction* SkillsAction = nullptr;
	UPROPERTY() UInputAction* ScrollUpAction = nullptr;
	UPROPERTY() UInputAction* ScrollDownAction = nullptr;
	UPROPERTY() UInputAction* HotbarActions[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
	TArray<FArmorPart> ArmorParts;
	UPROPERTY() TArray<UProceduralMeshComponent*> HeldComps;
	float HeldElbow = 25.f;                    // how far the sword arm is bent while holding the weapon
	bool bShieldShown = false;
	// Shield and weapon go on the back (shield over the sword, both under the cape) while climbing, riding or sitting
	bool bStowed = false;
	void PlaceShield(bool bOnBack);
	// The shield is strapped along the left forearm (at the elbow and the hand) and hangs at the side; raising it blends to
	// the guard. Called every frame after the arms are posed.
	void HoldShield();
	// A sword held at ease: the blade turned in the hand so its tip rests in front, between the feet. Blends out for attacks.
	TArray<UProceduralMeshComponent*> EaseComps;
	float EaseAlpha = 0.f;
	int32 EaseKind = -1;
	float ShieldHoldA = 1.f;
	void HoldWeaponAtEase(float Dt, bool bBusy);
	// Stepping in the shield-up stance: the way the legs step (forward/back, and sideways), eased
	float StepFw = 1.f, StepSd = 0.f;
	// Shield guard: a raised shield stops up to its Defence in damage; once spent the guard is broken and stops only half of
	// each blow until it has fully recovered (it recovers like health: 10 s after the last blocked hit, then a full bar in 10 s)
	// (online: the server's numbers, sent to this knight's player)
	float NextExploreTime = 0.f;   // the server clears the map fog round the knight a couple of times a second
	UPROPERTY(Replicated) float GuardPool = 0.f;
	UPROPERTY(Replicated) float GuardMax = 0.f;
	float LastGuardHitTime = -100.f;
	UPROPERTY(Replicated) bool bGuardBroken = false;
	UPROPERTY() UInstancedStaticMeshComponent* DustISM = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* ClodISM = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* BladeISM = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* LeafAISM = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* LeafBISM = nullptr;
	UPROPERTY() UInputModifierScalar* LookScalar = nullptr;
	bool bSettingsOpen = false;
	bool bInventoryOpen = false;
	bool bSkillsOpen = false;
	bool bPanelCursor = false;
	bool bPanelAlways = true;
	TArray<float> PotionReadyAt;   // per potion row (Potions.csv)
	float SpeedUntil = -1.f, VigourUntil = -1.f, RadarUntil = -1.f;
	float SpeedPotionMul = 1.5f, VigourMul = 1.5f;   // how strong the last speed / vigour potion was
	// Radar potion: the server finds who is near (enemies and other players) twice a second and sends the spots to the drinker only
	UPROPERTY(Replicated) TArray<FVector2D> RadarDots;
	double LastRadarScan = -10.0;
	void UpdateRadar();

	// The steps of Tick, in order (see AKnightCharacter::Tick for which file each lives in)
	void TickHousekeeping();
	void TickGatherReach();
	bool TickRollSlide(float Dt, bool bFalling, float& OutSpecialSpeed);
	void TickAttack(float Dt);
	void TickHealth(float Dt);
	bool TickStamina(float Dt, float Speed, bool bFalling, bool bSpecial);
	void TickGear(float Dt);
	bool TickGroundSpeed(float Dt, float Speed, bool bFalling, bool bSpecial, float SpecialSpeed, bool bWantSprint);
	void TickPose(float Dt, const FVector& Vel, float Speed, bool bFalling, bool bFaceCam, float& OutS, float& OutC);
	void PosePrep(struct FKnightPose& P);
	void PoseLegs(struct FKnightPose& P);
	void PoseBody(struct FKnightPose& P);
	void PoseCape(struct FKnightPose& P);
	void PoseArms(struct FKnightPose& P);
	void TickDust(float Dt, bool bFalling, bool bSpecial, float S, float C);
	TWeakObjectPtr<class ALootDrop> LootTarget;
	UPROPERTY() UInputAction* PotionAction = nullptr;
	int32 PanelTab = 0;
	UPROPERTY() UInputAction* PanelTabAction = nullptr;
	bool bBankOpen = false;
	float PickCooldownUntil = 0.f;
		mutable TWeakObjectPtr<ACottage> FoundCottage;
	mutable TWeakObjectPtr<ALootDrop> FoundDrop;
	bool bShopOpen = false;
	bool bSmithOpen = false;
	bool bDialogueOpen = false;
	TWeakObjectPtr<ANpc> TradeNpc;
	TArray<FString> DialogueLines;
	int32 DialogueIdx = 0;
	enum class EInteract : uint8 { None, Bench, Npc, Horse, Door, Loot, Drop };
	EInteract FindInteractable(int32& OutIndex, ANpc*& OutNpc, AHorse*& OutHorse) const;
	TWeakObjectPtr<AHorse> MyHorse;
	bool bMounted = false;
	float MountAlpha = 0.f;
	float HorseEnergy = 100.f;
	float HorseHealth = 100.f, HorseMaxHealth = 100.f;
	float HorseSpeedNow = 0.f;   // the horse's current top speed while ridden (ramps up when sprinting)
	bool bSavedOrientMount = true;
	double LastBackTime = -10.0;          // last time S was held while riding (backing the horse up)
	float StanceFwd = 0.f, StanceRight = 0.f;   // the last movement input while the shield is up
	bool bBackOrient = false, bBackSavedOrient = true;
	float LastHorseHurtTime = -100.f;
	bool bHorseTired = false;
	bool bFishing = false;
	int32 GraphicsLevel = 1;      // see GetGraphicsLevel
	bool bGraphicsApplied = false;
	float RenderScale = 0.6f;     // 0.35 - 1.0 of the screen resolution
	void ApplyGraphics();
	float LookSensitivity = 0.5f; // 1.0 = the original (fast) speed
	FVector GatherStart = FVector::ZeroVector;
	float ElbowRCur = 0.f;

	float WalkSpeed = 400.f;
	float CrouchSpeed = 240.f; // 40% slower than walking
	float SprintSpeed = 750.f;


	// The skeleton: every body part hangs off one of these pivots
	UPROPERTY() USceneComponent* BodyRoot = nullptr;    // bounces
	UPROPERTY() USceneComponent* UpperPivot = nullptr;  // chest: leans and twists
	UPROPERTY() USceneComponent* HeadPivot = nullptr;
	UPROPERTY() USceneComponent* PlumePivot = nullptr;
	UPROPERTY() USceneComponent* CapeUpper = nullptr;
	UPROPERTY() USceneComponent* CapeLower = nullptr;
	UPROPERTY() USceneComponent* HipL = nullptr;
	UPROPERTY() USceneComponent* HipR = nullptr;
	UPROPERTY() USceneComponent* KneeL = nullptr;
	UPROPERTY() USceneComponent* KneeR = nullptr;
	UPROPERTY() USceneComponent* AnkleL = nullptr;   // the feet turn here to stay flat on the ground
	UPROPERTY() USceneComponent* AnkleR = nullptr;
	UPROPERTY() USceneComponent* ArmPivot = nullptr;    // right shoulder (sword arm); mining and chopping drive this
	UPROPERTY() USceneComponent* ArmPivotL = nullptr;   // left shoulder (shield arm)
	UPROPERTY() USceneComponent* ElbowR = nullptr;
	UPROPERTY() USceneComponent* ElbowL = nullptr;

	// The cape: a small cloth simulation (a grid of points pinned across the shoulders), redrawn every frame
	UPROPERTY() UProceduralMeshComponent* CapeCloth = nullptr;
	UPROPERTY() class UMaterialInstanceDynamic* CapeMid = nullptr;
	static constexpr int32 CapeNX = 7, CapeNY = 10;
	static constexpr float StanceHip = 12.f;   // relaxed stance: the thighs lean this far forward and the knees bend twice as much
	static constexpr float RestLean = 0.f;    // at ease the chest leans this far forward
	TArray<FVector> CapeP, CapePrev;
	bool bCapeInit = false;
	FVector CapeRootPrev = FVector::ZeroVector; bool bCapeRootSet = false;
	TArray<FVector> CapeNrmPrev;
	bool bCapeMeshMade = false;
	float CapeAccum = 0.f;
	float CapeTime = 0.f;
	void TickCape(float Dt);
	FVector CapePin(int32 i) const;

	// Animation state
	float GaitPhase = 0.f;
	float MoveBlend = 0.f;   // 0 = standing, 1 = moving
	float RunAlpha = 0.f;    // 0 = walking, 1 = full sprint
	float AirAlpha = 0.f;    // 0 = on the ground, 1 = in the air
	float LandTimer = 0.f;
	float LandImpact = 0.f;
	float LandDuration = 0.35f;
	float LastFallVz = 0.f;
	float FlutterTime = 0.f;
	float IdleTime = 0.f;
	float PrevYaw = 0.f;
	float YawRate = 0.f;
	float SmoothBodyZ = 0.f;
	float SmoothChestZ = 0.f;
	float AirSpeedMul = 1.f;
	bool bSprinting = false;
	FVector UpperBase = FVector::ZeroVector;
	bool bWasFalling = false;

	// Moves and stances
	bool bCrouched = false;
	bool bBlocking = false;
	float StrafeSprint = 1.f;   // strafe mode: how much of the sprint applies, eased (it fades in as you turn toward straight ahead)
	float LegYaw = 0.f;   // facing the camera (strafe mode, shield stance): how far the hips and legs turn toward the way you move (Fortnite style)
	bool bStrafeMode = false;   // strafe mode (wheel click or X): always face where the camera looks, sidestep and back up instead of turning
	void ToggleStrafe();
	bool bBlockHeld = false;   // the block button is down (the shield drops during a swing and comes back after it if still held)
	bool bSliding = false;
	bool bRolling = false;
	bool bSitting = false;
	bool bAttacking = false;
	bool bExhausted = false;
	bool bClimbing = false;
	bool bAttackHit = true;
	bool bClimbIgnoreUp = false;
	float ClimbAlpha = 0.f, ClimbPhase = 0.f;
	FVector2D ClimbPos = FVector2D::ZeroVector, ClimbOut = FVector2D(1.f, 0.f);
		float ClimbGround = 0.f, ClimbDeckTop = 0.f;
	float ClimbT = 0.f;
	bool bSavedOrient = true;
	float CrouchAlpha = 0.f, BlockAlpha = 0.f, SlideAlpha = 0.f, SitAlpha = 0.f, TuckAlpha = 0.f;
	float Stamina = 10000.f, MaxStamina = 10000.f, StaminaDelay = 0.f;
	float SlideT = 0.f, SlideSpeed = 0.f, SlideCooldown = 0.f;
	FVector SlideDir = FVector::ForwardVector;
	float RollT = 0.f, RollDur = 0.6f, RollCooldown = 0.f;
	FVector RollDir = FVector::ForwardVector;
	float AttackT = 0.f, AttackDur = 0.7f;
	EAtkStyle AtkStyle = EAtkStyle::Slash;
	EAtkStyle LastStyle = EAtkStyle::Cast;
	int32 AttackDir = 1;            // +1 right-to-left, -1 left-to-right (the follow-up swing)
	int32 LastSwingDir = 0;
	float AtkCharge = 0.f;
	float LastSwingEnd = -10.f, QueuedClick = -10.f;
	bool bCharging = false;
	float ChargeT = 0.f;
	int32 PendingArrowTier = 0;
	// (online: the server's numbers, sent to everyone)
	UPROPERTY(Replicated) float Health = 100.f;
	UPROPERTY(Replicated) int32 MaxHealth = 100;
	float LastHurtTime = -100.f, InvulnUntil = 0.f;
	FVector SpawnLoc = FVector::ZeroVector;
	float PrevC = 1.f;
	float SlideDustTimer = 0.f;
	FVector LeftBase = FVector::ZeroVector;
	FVector SeatForward = FVector::ForwardVector;

	// Dust puffs kicked up by feet, landings, rolls and slides
	struct FDust { FVector Pos = FVector::ZeroVector; float Age = 99.f; float Life = 0.5f; float Size = 30.f; };
	TArray<FDust> Dust;
	int32 DustNext = 0;
	void SpawnDust(const FVector& Pos, float Size);
	// What the feet kick up, by what they are on: dirt clods (and a faint puff) on a path, a few grass blades in the grass,
	// nothing on stone, wood or indoors. Small pieces with a little physics: they fly, tumble, land and fade.
	struct FBit { FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector; FQuat Rot = FQuat::Identity; FVector Spin = FVector::ZeroVector; FVector Size = FVector(1.f); float Age = 99.f, Life = 1.f, Floor = 0.f; };
	TArray<FBit> Clods, Blades, LeavesA, LeavesB;   // (leaves under the trees: orange and deep red)
	int32 ClodNext = 0, BladeNext = 0, LeafNext = 0;
	int32 GroundUnderfoot() const;   // 0 none (stone, wood, indoors, water), 1 a dirt path, 2 grass
	void Kick(const FVector& Foot, float Strength);
	static void TickBits(TArray<FBit>& Bits, UInstancedStaticMeshComponent* ISM, float Dt, float Gravity, float Drag);

	TWeakObjectPtr<AResourceNode> ActiveNode;
	bool bSwinging = false;
	float SwingTime = 0.f;
	float SwingPeriod = 1.f;
	float WindupAngle = 150.f;
	float StrikeAngle = 30.f;
	float ArmPitch = 0.f;
	float ArmYaw = 0.f;
	bool bSideways = false; // woodcutting sweeps the arm sideways instead of overhead

	TArray<TPair<UStaticMeshComponent*, FLinearColor>> Parts;
};
