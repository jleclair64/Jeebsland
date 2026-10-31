#pragma once

#include "CoreMinimal.h"
#include "JeebslandCharacter.h"
#include "SkillComponent.h"
#include "KnightDesign.h"
#include "Inventory.h"
#include "HitBoxes.h"
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
	PickArrow,      // Target: an arrow lying in the world (E)
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
	// Test pictures (-JeebShot -ShotMove=...): the knight faces FaceYaw (not the camera) and does the move with a sword
	void ShotPlayMove(const FString& Move, float FaceYaw);
	static float SwingSpeedScale();   // testing: sword swings slowed by DefaultGame.ini [Jeebsland.Testing] SwingSpeed (1 = normal)
	bool bShotFace = false; float ShotFaceYaw = 0.f;
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
	void DealDamage(UHealthComponent* Target, int32 Dmg, bool bCrit, ESkillType Skill, bool bHitBoxes = false);
	// A raised shield's wedge (HitBoxes.h), for the box view; false while the shield is down
	bool GetShieldWedge(JeebHit::FShieldWedge& Out) const;
	// A clean hit moves you (server; HitBoxes.h FReact): hitstun with a push, or the low sweep's launch. Everyone is told.
	// Kept out while you can't be hit (getting up). Hit during your kick, you are knocked down instead (attack beats kick: Hurt).
	void TakeReact(AActor* From, const JeebHit::FReact& R);
	// Your sword is coming (a jab, smash or sweep before the end of its hit frames): a kick into you loses
	bool BladeComing() const;
	bool IsKicking() const { return bAttacking && AtkStyle == EAtkStyle::Kick; }
	// The raised shield's own mesh parts (arrows stick in them), in the same order on every machine
	void GetShieldParts(TArray<UPrimitiveComponent*>& Out) const;

	// ---------------- The bow (2026-10-31, Breath of the Wild style; KnightCharacterBow.cpp) ----------------
	// Hold the attack button to draw: the knight turns side-on, the bow up in his left fist, the string pulled back to his face,
	// walking at half speed while the camera closes in over his right shoulder and a reticle shows where the arrow goes. The
	// draw fills in BowDrawTime, stays full for BowHoldMax (trembling), then looses by itself; let go any time to shoot. The
	// fuller the draw, the faster (so further) and harder the arrow flies. Switching weapons, rolling, a kick, being hit or
	// knocked down lets the string down without shooting. The arrow slot (next to the shield slot) picks the arrows (F).
	bool IsDrawingBow() const { return Bow == EBow::Drawing; }
	float BowDrawFrac() const { return BowCharge; }
	float BowAimAmount() const { return BowAimAlpha; }
	int32 ArrowSlotId() const;   // the arrows the next shot uses (0: none left)
	void CycleArrows();          // F: the next kind of arrows you carry

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
	enum class EAtkStyle : uint8 { Slash, Thrust, Cast, Kick, Sweep, Dash };
	void StartAttack(EAtkStyle Style, int32 Dir, float Charge);
	// The kick (wheel up; KnightCharacterCombat.cpp): the right leg, the whole body turning to his left like Ken's forward smash.
	// Rock-paper-scissors: the sword is stopped by a raised shield, the kick goes through it and knocks the shielding knight
	// down (KnockDownBy). It has end lag: once the foot is out he can't move or act until the leg is back down.
	void StartKick();
	static constexpr float KickDur = 0.65f, KickActiveFrom = 0.18f, KickActiveTo = 0.5f, KickDamage = 0.6f;   // (out in about 0.12 s)
	TArray<TWeakObjectPtr<class AKnightCharacter>> KickedKnights;   // (a move hits each other knight once: MoveLab)
	// A kick that lands (owner, 2026-10-31): the rest of it plays KickLandedSpeed times faster, and the low sweep can cut it short at
	// once (kick, then sweep him as he falls: the combo). A wheel-down pressed during the kick comes out the moment it lands.
	bool bKickLanded = false;
	static constexpr float KickLandedSpeed = 2.2f;
	float QueuedSweep = -10.f;
	float KickFade = 0.f, KickFadeT = 0.f;   // a kick cut short by the sweep: its leg pose fades out quickly from where it was (PoseKick)
	void KickLanded();                       // (server) this kick hit someone: everyone is told
	void ApplyKickLanded();
	UFUNCTION(NetMulticast, Reliable) void MulticastKickLanded();
	// The low sweep (wheel down; sword only), like Link's down tilt in Smash Ultimate: he drops into a squat and sweeps the blade
	// along the ground in front, right to left. It reaches a knight lying on the ground (after a kick: read which way he rolls
	// and sweep there) and launches whoever it hits up into the air and a little back (bent by his DI); he lands on his back
	// again. Rooted until it is over.
	void StartSweep();
	static constexpr float SweepDur = 0.55f, SweepActiveFrom = 0.24f, SweepActiveTo = 0.6f, SweepDamage = 0.8f;   // (out in about 0.13 s: was 0.2)
	// The dash attack (owner, 2026-10-31): sprinting forward and clicking with a sword, a lunge with the blade stabbed straight out
	// at full reach like a spear. Out at DashActiveFrom s, hits until DashActiveTo s; damage, hitstun and end lag of a smash
	// attack charged DashCharge (the end lag: DashLag()). Rooted, sliding on with the lunge, until it is over.
	static constexpr float DashActiveFrom = 0.11f, DashActiveTo = 0.28f, DashCharge = 0.2f, DashLunge = 1100.f;
	float DashLag() const;
	float DashU() const;   // where the dash is in its key poses (0 - 1): the thrust in the first 0.3 s, the recovery over the lag
	bool IsDashing() const { return bAttacking && AtkStyle == EAtkStyle::Dash; }
	// Hitstun (2026-10-31 balance pass): a clean hit stops whatever you are doing and you can't move, swing, block, roll or
	// jump for this long. Jab hit 1 is long enough for hit 2 to follow for sure; hit 2 pushes you clear (the string ends there).
	static constexpr float StunJab1 = 0.45f, StunJab2 = 0.32f, StunSmash = 0.5f, StunSmashFull = 0.8f;
	float StunLeft = 0.f;
	void ApplyReact(float Stun, const FVector& Vel, bool bLaunch);   // (every machine)
	UFUNCTION(NetMulticast, Reliable) void MulticastReact(float Stun, FVector Vel, bool bLaunch);
	// DI (Smash's directional influence): the direction you hold (camera-relative, flat), noted every frame you hold one, even
	// while you can't move; the server is sent it and bends a launch by it (JeebHit::LaunchVelocity)
	FVector DIDir = FVector::ZeroVector;
	float DITime = -10.f, DISent = -10.f;
	void NoteDI(const FVector& Dir);
	FVector HeldDI() const;   // (server) what you are holding now, or zero
	UFUNCTION(Server, Unreliable) void ServerDI(FVector_NetQuantizeNormal Dir);
	// Knocked down (a kick on his raised shield): he falls on his back; once down, A or D rolls that way, W, S, Space or a click
	// gets straight up (he gets up by himself after KnockAuto s). Getting up and rolling away can't be hit for a moment.
	bool bKnockedDown = false;
	uint8 KnockPhase = 0;          // 0 falling and lying, 1 getting up
	float KnockT = 0.f, KnockAlpha = 0.f;
	static constexpr float KnockFall = 0.35f, KnockAuto = 1.5f, KnockRise = 0.45f;   // (KnockAuto counts from landing)
	void KnockDownBy(AActor* From);   // (server)
	void GetUp(uint8 Opt);            // 0 straight up, 1 roll to his left, 2 roll to his right (the player's machine, or the server)
	void ApplyGetUp(uint8 Opt);
	void TickKnock(float Dt);
	UFUNCTION(Server, Reliable) void ServerGetUp(uint8 Opt);
	UFUNCTION(NetMulticast, Reliable) void MulticastKnock(bool bDown, uint8 Opt);
	bool MoveLocked() const;          // a smash attack's swipe and end lag, a rebound, a kick, lying knocked down: no moving or turning
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
	void ScrollUp() { StartKick(); }   // (the wheel up kicks)
	void ScrollDown() { StartSweep(); }   // (the wheel down sweeps low)
	void NextWeapon() { CycleHotbar(1); }   // R: the next weapon on the bar, left to right (round to the first again)
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
	UPROPERTY() UInputAction* NextWeaponAction = nullptr;
	UPROPERTY() UInputAction* ArrowsAction = nullptr;   // F: the next kind of arrows
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
	// The wind trail behind a sword cut (BotW style): a soft pale ribbon between the middle of the blade and its tip, drawn
	// from the last moments of the blade's path (KnightCharacterFx.cpp, TickSwordTrail; material M_SwordTrail)
	UPROPERTY() class UProceduralMeshComponent* SwordTrail = nullptr;
	struct FTrailSample { FVector In, Out; float Time = 0.f, Strength = 0.f; };
	TArray<FTrailSample> TrailSamples;
	FVector TrailPrevTip = FVector::ZeroVector; bool bTrailPrev = false;
	void TickSwordTrail(float Dt);
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
	void PoseKick(struct FKnightPose& P);   // the kick and lying knocked down, over the legs and body
	float KickGuard() const;                // 0 - 1: how far the fists are up in the kick's guard
	void TickDust(float Dt, bool bFalling, bool bSpecial, float S, float C);
	TWeakObjectPtr<class ALootDrop> LootTarget;
	UPROPERTY() UInputAction* PotionAction = nullptr;
	int32 PanelTab = 0;
	UPROPERTY() UInputAction* PanelTabAction = nullptr;
	bool bBankOpen = false;
	float PickCooldownUntil = 0.f;
		mutable TWeakObjectPtr<ACottage> FoundCottage;
	mutable TWeakObjectPtr<ALootDrop> FoundDrop;
	mutable TWeakObjectPtr<class AProjectile> FoundArrow;   // an arrow lying in reach (E picks it up)
	bool bShopOpen = false;
	bool bSmithOpen = false;
	bool bDialogueOpen = false;
	TWeakObjectPtr<ANpc> TradeNpc;
	TArray<FString> DialogueLines;
	int32 DialogueIdx = 0;
	enum class EInteract : uint8 { None, Bench, Npc, Horse, Door, Loot, Drop, Arrow };
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
	UPROPERTY() USceneComponent* WristR = nullptr;   // the sword hand turns here (the held weapon hangs off it)
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
	float CapeYawPrev = 0.f;   // the chest's heading last frame (the cloth is carried round when he turns)
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
	bool bStrafeMode = true;    // strafe mode (wheel click or X; on from the start, owner 2026-10-31): always face where the camera looks, sidestep and back up instead of turning
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
	int32 AttackDir = 1;            // +1 right-to-left, -1 left-to-right (the follow-up swing); (the sword's jab uses the same two)
	static constexpr int32 JabFirst = 1, JabSecond = -1;   // the sword's two-hit jab (forehand, backhand; they loop)
	float SwingTwistNow = 0.f;      // the chest turning into a sword swing
	float SwingBodyWant = 0.f;      // the chest turn the jab's key poses ask for
	float SwingLeanWant = 0.f, SwingLeanNow = 0.f;  // the chest loads back, then leans into the sword cut
	float SwingLoadWant = 0.f, SwingLoadNow = 0.f;  // weight shifts from the rear-side leg through the follow-through
	float SwingFrom[8] = {};        // where the arm was when a sword swing began (pitch, yaw, roll, elbow, forearm, chest turn, blade heading, blade tilt)
	float BladeYawNow = 0.f, BladePitchNow = 0.f, BladeLeadSign = -1.f;   // a sword slash: where the blade points (knight's frame, degrees) and which way it travels
	float BladeCantNow = 0.f;       // the blade's cant about its own length (degrees; 0 = edge level)
	float CantFrom = 0.f;           // the cant a sword swing starts from (the previous hit's, seen with this hit's leading edge), or below -900: none
	float BladeAlpha = 0.f;         // how much the wrist is steering the blade (fades in and out of a slash)
	float WristBendNow = 0.f, ForearmTwistNow = 0.f;   // how far the wrist is bent and the forearm twisted for the blade (degrees; for checking a move)
	void LogSwordTip();             // test pictures: the sword tip and edge, frame by frame, in the knight's frame (log: JEEBTIP)
	// Smash-style hit boxes and hurt boxes (HitBoxes.h; KnightCharacterCombat.cpp, TickHitboxes)
	TArray<JeebHit::FCapsule> HitLive;                         // the sword's hit boxes this frame (swept from the last frame); empty outside the active frames
	FVector HitPrev[4]; bool bHitPrevValid = false;            // where each hit box was the frame before
	TArray<TWeakObjectPtr<class UHealthComponent>> HitThisSwing;   // a move hits each target once
	UPROPERTY() class UProceduralMeshComponent* BoxView = nullptr;   // the boxes, drawn see-through (M_HitBox)
	void TickHitboxes(float Dt);
	void DrawBoxes();
public:
	void GatherHurt(TArray<JeebHit::FCapsule>& Out) const;    // this knight's hurt boxes, from his joints as posed this frame
	const TArray<JeebHit::FCapsule>& LiveHitboxes() const { return HitLive; }
private:
	float JabBodyNow = 0.f;         // how far into the knees and the lean a sword jab has taken him (0 - 1)
	float AttackYaw = 0.f;          // the heading a swing started with (a sword hit keeps it)
	float ArmRollR = 0.f, ForearmNow = 0.f;   // the sword arm raised out to the side, and the forearm's turn (degrees)
	int32 QueuedJabs = 0;           // the attack buffer (Smash style): 1 if the attack button was pressed during this swing; extra presses don't stack
	int32 LastSwingDir = 0;
	// The sword's smash attack (Smash Ultimate style; KnightCharacterCombat.cpp, TickSmash). A press starts hit 1 as usual; held
	// past SmashHoldDelay (so a slow click is still a jab) hit 1 stops at its pull-back and charges at half walking speed: full
	// in SmashChargeTime, then it stays full for SmashHoldMax and goes by itself. Let go any time: hit 1's swipe, 1x to 2.5x
	// by the charge, then a fixed end lag (he stops at the end of the swipe; no moving, blocking, rolling or jumping).
	// Rebound is not part of a smash: any sword hit stopped by a raised shield bounces off (StartRebound).
	enum class ESmash : uint8 { None, Deciding, Charging, Swing, EndLag, Rebound };
	static constexpr float SmashHoldDelay = 0.25f, SmashChargeTime = 1.f, SmashHoldMax = 2.f, SmashEndPause = 0.4f;
	// Hitting a shield (owner, 2026-10-31): the blade bounces back (ReboundBack s) and he staggers back a step, open and unable
	// to move, swing, block, roll or jump for a fixed time: a jab hit ReboundJab s, a smash attack ReboundSmash s (longer than
	// its own end lag). A press in the last moment comes out right after, as after a smash.
	static constexpr float ReboundJab = 0.3f, ReboundSmash = 0.6f, ReboundBack = 0.15f;   // (balance pass 2026-10-31: was 0.45 / 0.8)
	float ReboundDur = 0.f;
	void StartRebound();                 // (server) a hit box of this swing struck a raised shield
	void BeginRebound(float Dur);        // (every machine) the bounce itself
	UFUNCTION(NetMulticast, Reliable) void MulticastRebound(float Dur);
	static constexpr float SmashApexT = 0.25f, SmashPauseT = 0.62f;   // where hit 1 waits while charging (its pull-back) and where the end lag pauses
	ESmash Smash = ESmash::None;
	float SmashT = 0.f;            // time in this phase
	float SmashCharge = 0.f;       // 0 - 1: how charged (while charging, then what the swipe was let go with)
	float SmashCoilNow = 0.f;      // the extra coil of the charge in the pose (eased out into the swipe)
	bool bSmashSwing = false;      // this swing is a smash (its own hit boxes and damage)
	bool bAttackDown = false;      // the attack button is held (the player's own machine)
	float ShotSmashHold = -1.f;    // test pictures: charge this long, then let go
	float ShotSweepAt = -1.f;      // test pictures (kicksweep): the low sweep at this time
	float ShotBowAt = -1.f;        // test pictures (bow60): let the string go at this time
	float ShotStrafe = 0.f;        // test pictures (-ShotStrafe=1): walk sideways to his right (-1 left) while shooting
	bool ShotSideOn() const;       // test pictures taken from the side: the bow aims straight ahead and the camera stays put
	void SetSmash(ESmash P, float Charge = 0.f, bool bTell = true);
	bool SmashInputOk(ESmash P) const;   // a phase change the player's machine asked for fits what this machine is doing
	void TickSmash(float Dt);
	float SmashTimeLeft() const;   // how long until the smash's end lag is over
	UFUNCTION(Server, Reliable) void ServerSmash(uint8 Phase, float Charge);
	UFUNCTION(NetMulticast, Unreliable) void MulticastSmash(uint8 Phase, float Charge);
	bool SmashLocked() const { return Smash == ESmash::Swing || Smash == ESmash::EndLag || Smash == ESmash::Rebound; }   // no moving
	float AtkCharge = 0.f;
	float LastSwingEnd = -10.f, QueuedClick = -10.f;
	bool bCharging = false;
	float ChargeT = 0.f;
	int32 PendingArrowTier = 0;
	// The bow (see IsDrawingBow above). The player's machine decides the draw and the shot from the button and tells the server,
	// which checks them (it keeps its own draw timer: never more charge than it has seen) and fires the arrow; everyone else is
	// shown the draw.
	enum class EBow : uint8 { None, Drawing, Recover };
	EBow Bow = EBow::None;
	float BowT = 0.f;              // time in this phase
	float BowCharge = 0.f;         // 0 - 1: how far drawn
	float BowAimAlpha = 0.f;       // the aiming pose and camera, eased in and out
	float BowEaseAlpha = 0.f;      // the bow in his hand (the left forearm turns so it lies forward and back at his side)
	float BowCamAlpha = 0.f;
	int32 ArrowPick = 0;           // the arrows picked in the arrow slot (when you still have some)
	static constexpr float BowDrawTime = 1.f, BowHoldMax = 2.f, BowRecover = 0.3f;   // (like the smash attack's charge)
	// By the draw (none, half, full; straight lines between): a level shot lands about 3 m out barely drawn, 12 m half drawn,
	// 44 m full (owner, 2026-10-31: 10 ft, 40 ft, as before); the speed and how fast it drops together set that
	static constexpr float BowSpeedMin = 1000.f, BowSpeedMid = 2400.f, BowSpeedMax = 4800.f;   // cm/s
	static constexpr float BowSagMin = 2900.f, BowSagMid = 1000.f, BowSagMax = 300.f;          // cm/s/s it drops
	static constexpr float BowDamageMin = 0.12f, BowDamageMax = 1.3f;   // times the bow's damage, by the draw to the power 1.5 (half drawn: x0.54)
	static constexpr float BowCamLength = 220.f;                                      // the camera's distance while aiming (540 normally)
	static constexpr float BowAnchorUp = 6.f, BowAnchorSide = 16.f;   // the full draw's anchor: this far above his neck joint and out to his right (beside the helm's cheek)
	void StartDraw();
	void ReleaseDraw();
	void CancelDraw();
	void SetBow(EBow P);
	void TickBow(float Dt);
	void PoseBow(float Dt);        // (end of PoseArms) the aiming arms, the side-on chest and the turned head
	FVector ArrowStart() const;    // where arrows leave from (the same on every machine)
	FVector BowAimDir(FVector* OutTarget = nullptr) const;     // the player's machine: from ArrowStart through what is under the reticle; elsewhere where he looks
	void FireArrow(const FVector& Dir, float Charge, int32 ArrowId);   // (server)
	UFUNCTION(Server, Reliable) void ServerBowDraw();
	UFUNCTION(Server, Reliable) void ServerBowShoot(FVector_NetQuantizeNormal Dir, float Charge, int32 ArrowId);
	UFUNCTION(Server, Reliable) void ServerBowCancel();
	UFUNCTION(NetMulticast, Reliable) void MulticastBow(uint8 Phase);
	UPROPERTY() USceneComponent* BowHold = nullptr;              // the bow in the left fist, built at its grip (RebuildHeld)
	UPROPERTY() UProceduralMeshComponent* BowString = nullptr;   // its straight string, hidden while drawn (BowBits take over)
	UPROPERTY() TArray<UStaticMeshComponent*> BowBits;           // the drawn string's two halves, the nocked arrow and its head
	UPROPERTY() TArray<UStaticMeshComponent*> QuiverParts;       // the quiver of arrows on his back while the bow is in hand
	void UpdateBowBits();
	void UpdateQuiver();
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
