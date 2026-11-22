#pragma once

#include "CoreMinimal.h"
#include "JeebslandCharacter.h"
#include "SkillComponent.h"
#include "KnightDesign.h"
#include "Inventory.h"
#include "HitBoxes.h"
#include "JeebTuning.h"
namespace JeebAnim { struct FInput; }
class FKnightAnimator;
#include "KnightCharacter.generated.h"

class UStaticMeshComponent;
class UInputMappingContext;
class UInputAction;
class USkillComponent;
class UQuestComponent;
class UTravelComponent;
class AResourceNode;
class UInputModifierScalar;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class ANpc;
class AHorse;
class UHealthComponent;
class ACottage;
class ALootDrop;

// One part he carries that is modelled in code (his shield's pieces): KnightDesign.h; weapons in WeaponDesign.h. His body and armour are one
// skinned suit (FKnightAnimator).
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
	class UStaticMeshComponent* Model = nullptr;   // shields: the library model of its style, on its plate (JeebProps::AddShieldModel); the code-built parts are then hidden
	float Shade = 1.f;
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
	TakeFromWall,   // A: a peg of the MoveLab gear wall (E; test builds only)
	ChooseArrows,   // A: the arrow id to shoot next
	HouseDoor,      // At: an open house's door hinge (AHouseInterior::DoorKey): open or shut it (E)
};

// Knight built from basic shapes. Also creates its own keyboard/mouse
// controls in code, so no input assets are needed.
UCLASS()
class AKnightCharacter : public AJeebslandCharacter
{
	GENERATED_BODY()

public:
	AKnightCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	// On a horse, steering is like a car: W goes forward along the horse, A and D turn it (it cannot slide sideways), S only brakes.
	virtual void DoMove(float Right, float Forward) override;
	virtual void Jump() override; // also stands you up from a bench, and ends a slide
	virtual void StopJumping() override;                     // (the engine's jump; ours only clears bWantsRise)
	virtual void CheckJumpInput(float DeltaTime) override;   // (offers a new press to climb, water and mantle first)
	// Online: what the knight stands on is never used as its "movement base" unless it is a shared (replicated) actor. The
	// ground, the old village site, the towers and forts are built separately on every machine, so the server can't name them to the
	// player's machine: a correction that names one is thrown away there, and the two copies of the knight drift apart
	// until the knight jumps (then it snaps to wherever the server had it).
	using ACharacter::SetBase;
	virtual void SetBase(FMovementBaseInterfaceData* Data, const FName BoneName = NAME_None, bool bNotifyActor = true) override;

	// The playable troll (2026-11-19, KnightCharacterTroll.cpp): a class mode of the knight. ATrollCharacter sets it in its constructor,
	// so every machine knows it from the class alone. Everything else is the knight's code, with the troll's changes inside it.
	bool IsTroll() const { return bIsTroll; }
	// The playable wizard (2026-11-21, KnightCharacterWizard.cpp): a class mode of the knight too, on the knight's own skeleton and moves. AWizardCharacter sets it.
	// His guard is a blue orb all round him in place of a shield; his roll is a short teleport the server places; he wears the wizard's hat, robe, boots and book.
	bool IsWizard() const { return bIsWizard; }
	bool WizardDashKind(int32 K) const { return bIsWizard && (K == 7 || K == 8); }   // (K_Wand, K_Staff: his dash attack is the knight's lunge with these too)
	int32 WizardGuardItem() const;   // (what his orb is as strong as: a kite shield of his spell book's tier, tin with none)
	void StartBlink(const FVector& Dir);
	void DoBlink(const FVector& Dir);
	bool BlinkSpot(const FVector& Dir, FVector& Out) const;
	void BlinkFx(const FVector& From, const FVector& To);
	void TickWizard(float Dt);
	void SwapWizardForTest(bool bWizard);   // (single player, test builds: jeeb.PlayWizard 1|0)
	UFUNCTION(Server, Reliable) void ServerBlink(FVector_NetQuantizeNormal Dir);
	UFUNCTION(NetMulticast, Reliable) void MultiBlink(FVector_NetQuantize From, FVector_NetQuantize To);
	UPROPERTY() class UStaticMeshComponent* WizardOrb = nullptr;
	UPROPERTY() class UStaticMeshComponent* WizardBookHeld = nullptr;   // the spell book in his left fist (UpdateWizardBook)
	void UpdateWizardBook();
	UPROPERTY() class UMaterialInstanceDynamic* WizardOrbMid = nullptr;
	struct FBlinkPuff { TWeakObjectPtr<class UStaticMeshComponent> Comp; TWeakObjectPtr<class UMaterialInstanceDynamic> Mid; float T = 0.f; bool bIn = false; };
	TArray<FBlinkPuff> BlinkPuffs;
	float BlinkHide = 0.f;   // (seconds his body stays unseen after a teleport: the vanish)
	float WalkNow() const;     // WalkSpeed, or the troll's (Tuning.ini [Troll])
	float SprintNow() const;
	// The troll's charged jump: hold Space, let go. Client asks (ServerLeap), server checks and launches; the cooldown is the server's.
	float LeapChargeShare() const;   // 0..1 while Space is held (this machine's own troll)
	float LeapCooldownShare() const { return LeapCooldown > 0.f ? LeapCooldown / FMath::Max(0.1f, JeebTune::V().TrollLeapCooldown) : 0.f; }
	float LeapFillUsed = 1.f;   // (the bar's fill the last leap took)
	float LeapFill() const { return 1.f - LeapCooldownShare(); }   // (2026-11-22) the charged-jump bar: 1 full; a leap goes this share as far and empties it
	bool IsLeapCharging() const { return LeapChargeFrom >= 0.0; }
	// The troll's rush (owner 2026-11-22): sprinting builds up to 3x the knight's sprint over 10 s; after 5 s a rush bar drains (Stamina
	// skill); empty, he eases down to his walking speed. At top speed he bowls over players and creatures (server). Tuning.ini [Troll].
	bool IsRushing() const { return bRushing; }   // (replicated from the server: he is at rush top speed; arrows and others may read it)
	float RushBarShare() const { return RushBar; }
	bool IsRushTired() const { return bRushTired; }
	static float RushSeconds(int32 StaminaLevel);   // how long a full rush bar lasts once it drains, at this Stamina level
	UFUNCTION(Server, Reliable) void ServerLeapCharge(bool bStart);
	UFUNCTION(Server, Reliable) void ServerLeap(float Charge, FVector_NetQuantizeNormal Dir);
	UFUNCTION(Server, Reliable) void ServerHop(FVector_NetQuantizeNormal Dir);
	UFUNCTION(Client, Reliable) void ClientLeapRefused(float CooldownLeft);
	void SwapClassForTest(bool bTroll);   // (single player, test builds: jeeb.PlayAs troll|knight saves, then comes back as the other in the same spot)
	// (2026-11-22) Play as another class, online too (test kit's KNIGHT / TROLL / WIZARD buttons): the owning machine calls
	// RequestClass (lets go of this pawn's input, asks AKnightPlayerController::ServerTestSwapClass); the server runs SwapToClass.
	int32 ClassIndex() const { return bIsWizard ? 2 : bIsTroll ? 1 : 0; }
	void RequestClass(int32 Class);
	void SwapToClass(int32 Class);
	void ReleaseInputForSwap();
	void SwapTestStep();   // (-SwapTest: cycles the classes, logs JEEBSWAP check)

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
	// Dying, of anything: everything you own goes into a sack where you fell, and you wake in the old village site in the starting kit
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
	// Drinking takes time (owner, 2026-11-05): the server starts it and ends it, every machine plays it. The potion is used and works only at the end.
	UFUNCTION(NetMulticast, Reliable) void MulticastDrink(uint8 Type, bool bStart);
	bool bDrinking = false;
	float DrinkT = 0.f;
	double LastDrinkStart = -100.0;
	bool bDrinkPredicted = false;   // the player's machine has started a drink itself and is waiting for the server's echo of it
	UFUNCTION(Client, Reliable) void ClientDrinkRefused();
	uint8 DrinkType = 0;
	bool CanDrinkNow() const;
	void TickDrink(float Dt);
	// Emotes (2026-11-18, KnightCharacterEmote.cpp; JeebEmotes.h): RequestEmote asks the server (ServerEmote), which checks him and
	// replicates the emote in RepEmote (id in the low byte, a count above it so the same emote again restarts it); every machine plays it
	// and ends it itself the moment he moves, jumps, attacks or is hit, and the server's word ends it everywhere.
	void RequestEmote(int32 Id);           // (0 stops the one playing)
	uint8 EmoteNow() const { return EmoteId; }
	bool CanEmote(FString* Why = nullptr) const;
	bool EmoteStows() const;               // weapon and shield on his back for this one (and through the blend back to standing after it)
	float EmoteStowLeft = 0.f;             // (s) after a stowing emote ends, how long the gear stays on his back: the body's blend back to standing (KnightAnimator 0.45 s, 0.7 s up from the sit, 0.3 s walking off)
	UFUNCTION(Server, Reliable) void ServerEmote(uint8 Id);
	UPROPERTY(ReplicatedUsing = OnRep_Emote) uint16 RepEmote = 0;
	UFUNCTION() void OnRep_Emote();
	uint8 EmoteId = 0;
	float EmoteT = 0.f, EmoteFor = 0.f;   // seconds into it; how long it lasts (0 until ended)
	double LastEmoteAsk = -10.0, LastServerEmote = -10.0;
	void StartEmote(uint8 Id);
	void StopEmote(const TCHAR* Why);
	void TickEmote(float Dt);
	bool EmoteCancelled(FString& Why) const;
	void TickEmoteTests(float Dt);         // (test pictures and online tests: -ShotMove=emote<name>|emoteall, -EmoteTest=)
	UPROPERTY(Transient) TObjectPtr<USceneComponent> DrinkGroup;         // the open hand round the bottle (the skeleton has no fingers: the fist folds away while he drinks)
	UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> DrinkHand;
	float AimCamZ = 0.f; bool bAimCamZSet = false;                       // where the aiming camera's height has got to (TickBow)
	static constexpr float AimCamFollow = 10.f, AimCamMax = 30.f;        // how quickly it follows a jump, and the most it trails by (cm)
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BottleMesh;    // the potion in his hand while he drinks
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BeltFlask;     // and on his belt while the potion slot is filled (owner 2026-11-05)
	void UpdateBeltFlask();
	int32 LookPotion = 0;                                                // other players' knights: what is in their potion slot (RepLook)
	int32 BeltFlaskId = -1;
	UFUNCTION(Client, Reliable) void ClientUnlockHut(FVector_NetQuantize At);
	void ShopNote(const FString& Text, float Seconds = 3.f);   // shown at the bottom of the shop or smithy screen

	void EquipFromInventory(int32 Idx);          // hold or wear the gear in that slot (swaps with what you have on)
	void UnequipHotbar(int32 H);
	void UnequipWorn(int32 Kind, bool bSecondRing = false);
	void SelectHotbar(int32 H);
	// Test pictures (-JeebShot -ShotMove=...): the knight faces FaceYaw (not the camera) and does the move with a sword
	void ShotPlayMove(const FString& Move, float FaceYaw);
	float GatherSwingSeconds() const { return SwingPeriod; }   // one swing of the tool he is working with
	bool WorkingOn(const AResourceNode* Node) const;
	static float SwingSpeedScale();   // testing: sword swings slowed by DefaultGame.ini [Jeebsland.Testing] SwingSpeed (1 = normal)
	bool bShotFace = false; float ShotFaceYaw = 0.f;
	void RefreshEquipment();                     // redraws armour, shield and the weapon in your hand
	bool GuardActive() const;                    // the guard can be raised: a shield, or a troll's bare arm
	bool ShieldActive() const;                   // a shield is worn and the weapon in hand allows one
	int32 SelectedKind() const;                  // kind of the weapon in hand, or -1
	// Gathering: puts the right tool in hand (and tells you if you have none). OutTimeMult speeds up better tools.
	bool PrepareTool(ESkillType Skill, float& OutTimeMult);
	float GetStaminaMult() const;
	// Riding: T calls the horse, E beside it mounts. The bar counts 100 down while sprinting, then refills before the next sprint.
	bool IsMounted() const { return bMounted; }
	void MountHorse(AHorse* H) { Mount(H); }   // (test pictures: -ShotMount)
	void DismountHorse() { Dismount(); }      // (test pictures: -ShotDismount)
	void SetStrafeForTest(bool bOn) { bStrafeMode = bOn; }
	void SprintForTest(bool bOn) { if (bOn) StartSprint(); else StopSprint(); }
	bool OrientsToMovement() const;
	float GetHorseEnergy() const { return HorseEnergy; }   // 0 - 100
	float GetHorseHealth() const { return HorseHealth; }
	float GetHorseMaxHealth() const { return HorseMaxHealth; }
	// The horse is hurt separately from the knight (its own hit boxes come later). Heals only after 10 s unhurt.
	void HurtHorse(float Amount);
	bool IsHorseTired() const { return bHorseTired; }
	// Shield guard, for the HUD
	float GetGuardFraction() const { return GuardMax > 0.f ? GuardPool / GuardMax : 0.f; }
	float GetGuardMax() const { return GuardMax; }
	bool IsGuardBroken() const { return bGuardBroken; }
	bool IsBlocking() const { return bBlocking; }
	USkillComponent* GetSkills() const { return Skills; }
	UQuestComponent* GetQuests() const { return Quests; }   // (this character's quests, QuestComponent.h)
	UTravelComponent* GetTravel() const { return Travel; }   // (fast travel between visited towns, FastTravel.h)
	float GetLastHurtTime() const { return LastHurtTime; }   // (world seconds of the last blow that reached him: the fast travel combat check)
	FString TravelBlocker() const;                          // why he cannot start or keep a fast travel now, or empty (KnightCharacterTravel.cpp)
	void TravelPlaced(const FVector& Dest, float Yaw);     // put him at Dest facing Yaw (every machine: the server on the jump, the player on ClientTravelArrived)
	// Text for the "Press E ..." prompt, or false if nothing is in reach
	bool GetInteractPrompt(FString& OutText) const;
	// Where the floating E prompt hangs: just in front of and above the thing you can use. bOutE is false for things that are not an E action (a locked door you cannot pick, a place to fish).
	bool GetInteractAnchor(FVector& Out, bool& bOutE) const;

	// Combat: health, damage dealt and taken
	float GetHealth() const { return Health; }
	bool IsCrouchedNow() const { return bCrouched; }   // (the animals see a crouched man less far: Deer.cpp)
	int32 GetMaxHealth() const { return MaxHealth; }
	// Something hits you for Raw points. A raised shield facing it takes all of it out of the guard; otherwise the armour piece
	// covering Zone (JeebHit::EZone, where it landed; Z_None: the average of your armour) lets only part through
	// (JeebItems::ZoneTake). bHeadshot: a projectile to the head (already x1.5; its number is drawn as a headshot).
	// bEnvironmental: a hazard (poison smoke, fire): armour and shields do not stop it, and it trains no Defence.
	void Hurt(int32 Raw, AActor* From, bool bEnvironmental = false, uint8 Zone = 0, bool bHeadshot = false);
	// You (or your arrow / bolt) hit something: applies the damage and awards skill XP (Zone, bHeadshot: as for Hurt)
	void DealDamage(UHealthComponent* Target, int32 Dmg, bool bCrit, ESkillType Skill, bool bHitBoxes = false, uint8 Zone = 0, bool bHeadshot = false);
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
	// Hold the right button to draw (owner, 2026-11-06): the knight turns side-on, the bow up in his left fist, the string pulled back to his face,
	// walking at half speed while the camera closes in over his right shoulder and a reticle shows where the arrow goes. The
	// draw fills in BowDrawTime and stays full for as long as it is held; left click shoots, and letting go of the right button lets the string down. The
	// fuller the draw, the faster (so further) and harder the arrow flies. Switching weapons, rolling, a kick, being hit or
	// knocked down lets the string down without shooting. The arrow slot (next to the shield slot) picks the arrows (F).
	bool IsDrawingBow() const { return Bow == EBow::Drawing; }
	bool IsAimingBow() const { return Bow == EBow::Drawing || (bAimHeld && JeebItems::IsShooter(SelectedKind())); }   // (the reticle stays up between shots while the button is held)
	float BowDrawFrac() const { return BowCharge; }
	float BowAimAmount() const { return BowAimAlpha; }
	// The troll's throwing rocks (KnightCharacterRock.cpp): the rocks in hand (key 2), the wind-up and its charge, for the bar and the reticle
	bool InRockMode() const { return bIsTroll && bRockMode; }
	bool IsChargingRock() const { return Rock == ERock::Charging; }
	bool IsAimingRock() const { return InRockMode() && (Rock == ERock::Charging || bAimHeld); }
	float RockChargeFrac() const { return RockCharge; }
	float RockAimAmount() const { return RockAimAlpha; }
	int32 ArrowSlotId() const;   // the arrows the next shot uses (0: none left)
	void CycleArrows();         // F: the next kind of arrows you carry
	void PickArrows(int32 Id);   // (the swap popup on the bar, and F: the server is told, since others see what is in his quiver)

	// ---------------- The wand's and staff's aim (2026-11-18, owner; KnightCharacterCastAim.cpp) ----------------
	// A small reticle up and right of the middle of the screen, clear of the knight (jeeb.Magic.Reticle, as shares of the screen). A cast
	// turns him toward what is under it, and the server fires the bolt from the wand's tip or the staff's orb toward that point: the player's
	// machine sends the point (ServerCastAim) just before the cast; the server checks it lies ahead of where he looks, else uses his aim.
	static FVector2D CastReticleAt();
	static float StaffSlide();                 // (cm) how far the staff's model slides through the fist for the walking-staff hold (jeeb.Knight.StaffHold)
	bool ShowsCastReticle() const;             // a wand or staff in hand, nothing in the way
	FVector CastAimPoint() const;              // the player's machine: what is under the reticle (elsewhere: ahead along his aim)
	FVector CastStartPoint(int32 Kind) const;  // the wand's tip or the staff's orb, as the pose has it (checked: near him, nothing solid between)
	float CastFacingYaw(float Fallback) const; // the heading toward a fresh aim point, or Fallback
	UFUNCTION(Server, Reliable) void ServerCastAim(FVector_NetQuantize Target, FVector_NetQuantize10 From);
	FVector CastTarget = FVector::ZeroVector;  // the last aim point (the server: as the player's machine sent it)
	FVector CastFrom = FVector::ZeroVector;    // (2026-11-22) where the bolt leaves: on the camera's line abreast of him, as the bow's arrow (checked on the server)
	double CastTargetAt = -100.0;              // when it came (world seconds); used once by AttackLands
	FVector CastLineStart(int32 Kind) const;   // the player's machine: the point on the camera's line abreast of the wand's tip or staff's orb
	FVector CheckCastStart(const FVector& Asked, int32 Kind) const;   // the server: Asked if it is near his hand with nothing solid between, else the hand

	// ---------------- The wand's and staff's charged shot (2026-11-22, owner: "up to 2.5x damage at full charge") ----------------
	// Hold the left button to charge (Tuning.ini [Wizard] CastChargeTime), let go to cast. The server times the hold itself (ServerCastCharge)
	// and never believes more charge than it has seen; damage is x1 to x CastChargeMax. Others see the tip's glow grow (bCastChargingRep).
	bool IsChargingCast() const { return bCastCharging; }
	float CastChargeFrac() const;
	void BeginCastCharge();
	void ReleaseCastCharge();
	void CancelCastCharge();
	UFUNCTION(Server, Reliable) void ServerCastCharge(bool bOn);
	bool bCastCharging = false;                // this machine: the player holding the button (or, on the server, as he told us)
	double CastChargeStart = 0.0;              // world seconds it began (each machine its own clock)
	float CastChargeHeldSrv = 0.f;             // (server) how long the last hold lasted as it saw it: the most charge it believes (used once)
	float ServerCastChargeLimit();
	UPROPERTY(ReplicatedUsing = OnRep_CastCharging) bool bCastChargingRep = false;   // the server's word, for everyone else's glow
	UFUNCTION() void OnRep_CastCharging();
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CastChargeGlow;   // the growing ball at the wand's tip or the staff's orb
	void TickCastCharge(float Dt);
	// (2026-11-22, owner) the wizard's magic: full at the start, each cast spends some (a charged one more), it fills again after a pause. Casting is
	// never refused: with less than the cost the spell goes at that share of its power and the bar empties. The server owns it.
	UPROPERTY(Replicated) float Magic = 100.f;
	double MagicUsedAt = -100.0;
	float GetMagicFraction() const;

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

public:
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;   // (the camera never goes under the water: KnightMovementSwim.cpp)
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
	FQuat CapeChestPrev = FQuat::Identity;
	float GuardShare = 1.f;   // the share of the shield's health left, while none is worn (a change of shield carries it over)
	UFUNCTION(Server, Reliable) void ServerInv(EInvOp Op, int32 A, int32 B, int32 C, AActor* Target, FVector_NetQuantize At);
	void DoInv(EInvOp Op, int32 A, int32 B, int32 C, AActor* Target, const FVector& At);
	bool NearTrader(uint8 Role) const;   // a villager of that ENpcRole is close enough to trade with

	// Online combat and moves (KnightCharacterNet.cpp). The server decides every hit; swings, guarding, crouching, rolls and
	// slides are copied to the server (so it moves the knight the same way) and shown to everyone else.
	UFUNCTION(Server, Reliable) void ServerAttack(uint8 Style, int8 Dir, float Charge);
	UFUNCTION(NetMulticast, Reliable) void MulticastAttack(uint8 Style, int8 Dir, float Charge);
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
	static inline const float& KickDur = JeebTune::Storage().KickDur;   // (out in about 0.12 s)
	static inline const float& KickActiveFrom = JeebTune::Storage().KickActiveFrom;
	static inline const float& KickActiveTo = JeebTune::Storage().KickActiveTo;
	static inline const float& KickDamage = JeebTune::Storage().KickDamage;
	TArray<TWeakObjectPtr<class AKnightCharacter>> KickedKnights;   // (a move hits each other knight once: MoveLab)
	// A kick that lands (owner, 2026-10-31): the rest of it plays KickLandedSpeed times faster, and the low sweep can cut it short at
	// once (kick, then sweep him as he falls: the combo). A wheel-down pressed during the kick comes out the moment it lands.
	bool bKickLanded = false;
	static inline const float& KickLandedSpeed = JeebTune::Storage().KickLandedSpeed;
	float QueuedSweep = -10.f;
	float KickFade = 0.f, KickFadeT = 0.f;   // a kick cut short by the sweep: its leg pose fades out quickly from where it was (PoseKick)
	void KickLanded();                       // (server) this kick hit someone: everyone is told
	void ApplyKickLanded();
	UFUNCTION(NetMulticast, Reliable) void MulticastKickLanded();
	// The low sweep (wheel down; sword only), like Link's down tilt in Smash Ultimate: he drops into a squat and sweeps the blade
	// along the ground in front, right to left. It reaches a knight lying on the ground (after a kick: read which way he rolls
	// and sweep there) and takes his feet: whoever it hits drops straight to the ground where he stands (owner, 2026-11-02; it used
	// to launch him) and gets up or rolls away as after a kick. Rooted until it is over.
	void StartSweep();
	static inline const float& SweepDur = JeebTune::Storage().SweepDur;   // (out in about 0.13 s: was 0.2)
	static inline const float& SweepActiveFrom = JeebTune::Storage().SweepActiveFrom;
	static inline const float& SweepActiveTo = JeebTune::Storage().SweepActiveTo;
	static inline const float& SweepDamage = JeebTune::Storage().SweepDamage;
	// The dash attack (owner, 2026-10-31): sprinting forward and clicking with a sword, a lunge with the blade stabbed straight out
	// at full reach like a spear. Out at DashActiveFrom s, hits until DashActiveTo s; damage, hitstun and end lag of a smash
	// attack charged DashCharge (the end lag: DashLag()). Rooted, sliding on with the lunge, until it is over. Once the stab is
	// out he keeps sliding (owner, 2026-11-02: no dead stop), braked from the start of the lunge only by a steady DashSlideBrake
	// cm/s/s with no ground friction: about 95 cm out at the stab, then about 40 cm more (TickAttack)
	static inline const float& DashActiveFrom = JeebTune::Storage().DashActiveFrom;
	static inline const float& DashActiveTo = JeebTune::Storage().DashActiveTo;
	static inline const float& DashCharge = JeebTune::Storage().DashCharge;
	static inline const float& DashLunge = JeebTune::Storage().DashLunge;
	static inline const float& DashSlideBrake = JeebTune::Storage().DashSlideBrake;
	bool bDashSliding = false;
	float SavedBrakeDecel = 2000.f;
	float DashLag() const;
	float DashU() const;   // where the dash is in its key poses (0 - 1): the thrust in the first 0.3 s, the recovery over the lag
	bool IsDashing() const { return bAttacking && AtkStyle == EAtkStyle::Dash; }
	// Hitstun (2026-10-31 balance pass): a clean hit stops whatever you are doing and you can't move, swing, block, roll or
	// jump for this long. Jab hit 1 is long enough for hit 2 to follow for sure; hit 2 pushes you clear (the string ends there).
	static inline const float& StunJab1 = JeebTune::Storage().StunJab1;
	static inline const float& StunJab2 = JeebTune::Storage().StunJab2;
	static inline const float& StunSmash = JeebTune::Storage().StunSmash;
	static inline const float& StunSmashFull = JeebTune::Storage().StunSmashFull;
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
	static inline const float& KnockFall = JeebTune::Storage().KnockFall;   // (KnockAuto counts from landing)
	static inline const float& KnockAuto = JeebTune::Storage().KnockAuto;
	static inline const float& KnockRise = JeebTune::Storage().KnockRise;
	void KnockDownBy(AActor* From, bool bDrop = false);   // (server; bDrop: the low sweep, straight down where he stands, his guard untouched)
	void GetUp(uint8 Opt);            // 0 straight up, 1 roll to his left, 2 roll to his right (the player's machine, or the server)
	void ApplyGetUp(uint8 Opt);
	void TickKnock(float Dt);
	UFUNCTION(Server, Reliable) void ServerGetUp(uint8 Opt);
	UFUNCTION(NetMulticast, Reliable) void MulticastKnock(bool bDown, uint8 Opt);
	bool MoveLocked() const;          // a smash attack's swipe and end lag, a rebound, a kick, a sweep, lying knocked down: no moving
	bool TurnLocked() const;          // ... and no turning either (the smash's swipe and end lag, lying down, hitstun, the dash)
	void AttackLands();         // the moment a swing, thrust or cast lands
	void TryStartClimb();       // walking into a ladder
	void UpdateClimb(float Dt);
	void ExitClimb(bool bAtTop, bool bAtBottom);
	void StartBlock();          // right click (hold)
	void StopBlock();
	void StartSlide();          // C while sprinting
	void StartRoll();           // (wheel down: ScrollDown / RollWheel)
	void RollWheel(float Fwd, float Right);   // wheel down: the roll, the way W/A/S/D point (relative to the camera); none held: straight ahead
	FVector PendingRollDir = FVector::ZeroVector;
	void OnInteractKey();       // E: sit, trade, talk, mount, open doors ...
	void UseDoor(ACottage* Hut);   // open, close, or try to pick the lock
	void CallHorse();           // (T, when the horse is back)
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
	void ScrollDown();   // (the wheel down rolls, 2026-11-15, owner; the low sweep, StartSweep, is shelved: no longer bound)
	bool bTestWheelRoll = false;   // test pictures: let the wheel roll run under -JeebShot
	void NextWeapon() { CycleHotbar(1); }   // R: the next weapon on the bar, left to right (round to the first again)
	void CycleHotbar(int32 Dir);
	void ApplyArmorSlot(int32 Slot);
	// The troll (KnightCharacterTroll.cpp)
	bool bIsTroll = false;
	bool bIsWizard = false;
	double LeapChargeFrom = -1.0;     // when Space went down for a charged jump (this machine's clock; -1 none)
	double ServerChargeFrom = -1.0;   // (server) when the client said it started charging
	float LeapCooldown = 0.f;         // seconds before the next charged jump
	UPROPERTY(Replicated) bool bRushing = false;   // (server) at rush top speed: IsRushing
	UPROPERTY(Replicated) float RushBar = 1.f;    // (server, to the owner) the rush bar 0..1
	UPROPERTY(Replicated) float RushSlow = 0.f;   // (server, to the owner) the ease down to a walk 0..1 (SprintNow)
	float RushT = 0.f, RushXPAcc = 0.f;   // (server) seconds rushing, XP owed
	UPROPERTY(Replicated) bool bRushTired = false;
	TMap<TWeakObjectPtr<AActor>, double> RushHitAt;   // (server) when each victim was last bowled over
	void TickRush(float Dt);
	float LeapCrouch = 0.f;           // the crouch as he charges (0..1, this machine)
	bool bAirLaunch = false;          // a hop or a charged jump is flying: the air speed clamp leaves it alone until he lands
	float ShotLeapCharge = -1.f;      // (test pictures: -SuitDo=chargejump<hundredths>)
	FVector LeapFrom = FVector::ZeroVector; double LeapAt = 0.0; float LeapShare = 0.f;   // (log JEEBLEAP)
	void ApplyTrollBody();            // capsule and camera from Tuning.ini [Troll]
	void StartHop(const FVector& Dir);          // the troll's wheel-down: a quick hop the way he points
	void DoHop(const FVector& Dir);
	void ReleaseLeap();               // Space let go: the tap jump, or the charged one
	void DoLeap(float Charge, const FVector& Dir);
	FVector LeapVelocity(float Charge, const FVector& Dir) const;
	void TickTroll(float Dt);
	void FreeFlight(bool bOn);
	bool bNetDoDone = false; float NetDoT = 0.f;   // (-TrollNetDo)
	bool bFreeFlight = false; float SavedFallBrake = 0.f, SavedFallFriction = 0.f;
	bool TrollSuitTest(const FString& Do);   // (test pictures: -SuitDo=punch, hop<deg>, hopall, chargejump<hundredths>, leapsizes, leaptest)
	virtual void Landed(const FHitResult& Hit) override;
	void BuildKnight();   // his body (FKnightAnimator) and the shield's parts
	TSharedPtr<FKnightAnimator> Anim;   // his body: on every machine, since the hit boxes and hurt boxes are read from its pose
	float AnimAccum = 0.f;
	TOptional<bool> ShotGallery;
	float SuitTestT = 0.f;
	bool bSuitTestActed = false;
	void FillAnimInput(JeebAnim::FInput& In) const;
	void PlaceGear();
	void PlaceStaffModel();         // the held staff's orb glow and its place in the fist, live (jeeb.Magic.OrbGlow, jeeb.Knight.StaffGrip)
	void TickSuitTests(float Dt);   // test pictures (-SuitWalk, -SuitDo)
	void RebuildHeld();   // the weapon(s) in your hands
	UProceduralMeshComponent* MakeMeshComp(USceneComponent* Attach, int32 PivotId, const KM::FMD& Md);
	void ApplyPartMaterial(FArmorPart& P, const FLinearColor& Color, bool bMetal, float Rough = 0.28f);

private:
	UPROPERTY() UInputMappingContext* KnightContext = nullptr;
	UPROPERTY() UInputAction* SprintAction = nullptr;
	UPROPERTY() UInputAction* InteractAction = nullptr;
	UPROPERTY() USkillComponent* Skills = nullptr;
	UPROPERTY() UQuestComponent* Quests = nullptr;
	UPROPERTY() UTravelComponent* Travel = nullptr;
	UPROPERTY() UInputAction* SettingsAction = nullptr;
	UPROPERTY() UInputAction* CrouchAction = nullptr;
	UPROPERTY() UInputAction* BlockAction = nullptr;
	UPROPERTY() UInputAction* SlideAction = nullptr;
	UPROPERTY() UInputAction* StrafeAction = nullptr;   // mouse wheel click (or X): strafe mode on/off
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
	UPROPERTY() TArray<UStaticMeshComponent*> HeldStatics;       // the parts of it that are models from /Game/Props
	bool bShieldBack = false;                                    // the shield is on his back (stowed, or both hands on a tool)
	bool bShieldShown = false;
	// Shield and weapon go on the back (shield over the sword, both under the cape) while climbing, riding or sitting
	bool bStowed = false;
	void PlaceShield(bool bOnBack);
	void PullStowed(float Cm);           // what is stowed on his back, moved this far in toward him (a flat crawl over a ledge, FKnightAnimator::StowPull)
	float StowPullNow = 0.f;
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
	FVector TrailPrevTip = FVector::ZeroVector; bool bTrailPrev = false; float TrailPeak = 0.f; bool bTrailDone = false;
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
	void TickGatherWalk(float Dt);
	FVector GatherSpot = FVector::ZeroVector;   // where he stands so that the tool's blow lands on the node
	float GatherYaw = 0.f, GatherWalkLeft = 0.f;
	bool TickRollSlide(float Dt, bool bFalling, float& OutSpecialSpeed);
	void TickAttack(float Dt);
	void TickHealth(float Dt);
	bool TickStamina(float Dt, float Speed, bool bFalling, bool bSpecial);
	void TickGear(float Dt);
	bool TickGroundSpeed(float Dt, float Speed, bool bFalling, bool bSpecial, float SpecialSpeed, bool bWantSprint);
	void TickPose(float Dt, const FVector& Vel, float Speed, bool bFalling, bool bFaceCam, float& OutS, float& OutC);
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
	mutable TWeakObjectPtr<class AGearWall> FoundWall;   // the MoveLab gear wall and the peg looked at (E takes a copy)
	mutable int32 FoundPeg = -1;
	double LastWallTake = -10.0;   // (server: one take per 0.2 s)
	bool bShopOpen = false;
	bool bSmithOpen = false;
	bool bDialogueOpen = false;
	TWeakObjectPtr<ANpc> TradeNpc;
	// (2026-11-18) the shop camera (KnightShopCam.cpp): your view eases in to frame the keeper while a shop, the bank or the smithy is open
	void ShopCamView(float DeltaTime, struct FMinimalViewInfo& View);
	float ShopCamT = 0.f;
	TWeakObjectPtr<ANpc> ShopCamNpc;
	bool bShopCamHid = false;
	TArray<TWeakObjectPtr<AActor>> ShopCamHidden;
public:
	void ShopCamTestOpen(bool bOpen);   // (test pictures, -ShopCamTest: opens or closes the nearest keeper's window; nothing in Shipping)
private:
	TArray<FString> DialogueLines;
	int32 DialogueIdx = 0;
	enum class EInteract : uint8 { None, Bench, Npc, Horse, Door, Loot, Drop, Arrow, GearWall };
	EInteract FindInteractable(int32& OutIndex, ANpc*& OutNpc, AHorse*& OutHorse) const;
	TWeakObjectPtr<AHorse> MyHorse;
	bool bMounted = false;
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
	int32 GatherKind = -1;   // (what he works with: TickGatherReach ends the work if it leaves his hand)
	float ElbowRCur = 0.f;

	static inline const float& WalkSpeed = JeebTune::Storage().WalkSpeed;   // 5 m/s (owner, 2026-11-01; was 4)
	static inline const float& CrouchSpeed = JeebTune::Storage().CrouchSpeed;
	static inline const float& WalkAccel = JeebTune::Storage().WalkAccel;    // starting up to walking speed is instant (about 1/60 s)
	static inline const float& SprintAccel = JeebTune::Storage().SprintAccel;   // walk to sprint keeps its ramp (the engine's usual acceleration)
	static inline const float& SprintSpeed = JeebTune::Storage().SprintSpeed;


	// Where the pose puts his gear (KnightCharacterPose.cpp, PlaceGear): empty joints, placed in the world every frame
	UPROPERTY() USceneComponent* UpperPivot = nullptr;  // the chest: its axes are the actor's when he stands at rest (the cape; what is on his back)
	UPROPERTY() USceneComponent* WristR = nullptr;      // the weapon's joint in the right hand
	UPROPERTY() USceneComponent* ElbowL = nullptr;      // the left hand's joint (a second blade)

	// The cape: a small cloth simulation (a grid of points pinned across the shoulders), redrawn every frame
	UPROPERTY() UProceduralMeshComponent* CapeCloth = nullptr;
	UPROPERTY() class UMaterialInstanceDynamic* CapeMid = nullptr;
	static constexpr int32 CapeNX = 7, CapeNY = 10;
	TArray<FVector> CapeP, CapePrev;
	bool bCapeInit = false;
	FVector CapeRootPrev = FVector::ZeroVector; bool bCapeRootSet = false;
	float CapeYawPrev = 0.f;   // the chest's heading last frame (the cloth is carried round when he turns)
	FVector CapeRelPrev = FVector::ZeroVector;   // the chest against his capsule last frame (the pose's own moves carry the cloth; only travelling trails it)
	float CapeActorYawPrev = 0.f;   // his own heading last frame (a sudden turn of his whole body starts the cloth again)
	TArray<FVector> CapeNrmPrev;
	bool bCapeMeshMade = false;
	float CapeAccum = 0.f;
	float CapeTime = 0.f;
	void TickCape(float Dt);
	FVector CapePin(int32 i) const;

	// Animation state
	float MoveBlend = 0.f;   // 0 = standing, 1 = moving
	float RunAlpha = 0.f;    // 0 = walking, 1 = full sprint
	float AirAlpha = 0.f;    // 0 = on the ground, 1 = in the air
	float LandTimer = 0.f;
	float LandImpact = 0.f;
	float LandDuration = 0.35f;
	float LastFallVz = 0.f;
	float PrevYaw = 0.f;
	float YawRate = 0.f;
	float AirSpeedMul = 1.f;
	bool bSprinting = false;
	bool bWasFalling = false;

	// Moves and stances
	bool bCrouched = false;
	bool bBlocking = false;
	float StrafeSprint = 1.f;   // strafe mode: how much of the sprint applies, eased (it fades in as you turn toward straight ahead)
	float SnowSinkNow = 0.f;   // cm the drawn body stands sunk in deep snow (JeebOpen::SnowSink, eased; looks only, the same on every machine)
	FVector PrintLast = FVector::ZeroVector; int32 PrintSide = 0; bool bPrintNoDecals = false;   // footprints in the snow (KnightCharacterPose.cpp)
	void SnowFeet(float Dt, FVector& Feet);
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
	float ClimbPhase = 0.f;
	FVector2D ClimbPos = FVector2D::ZeroVector, ClimbOut = FVector2D(1.f, 0.f);
		float ClimbGround = 0.f, ClimbDeckTop = 0.f;
	float ClimbT = 0.f;
	bool bSavedOrient = true;
	float CrouchAlpha = 0.f, BlockAlpha = 0.f;
	float Stamina = 10000.f, MaxStamina = 10000.f, StaminaDelay = 0.f;
	float SlideT = 0.f, SlideSpeed = 0.f, SlideCooldown = 0.f, SlideGlide = 0.f;
	FVector SlideDir = FVector::ForwardVector;
	float RollT = 0.f, RollDur = 0.6f, RollCooldown = 0.f;
	FVector RollDir = FVector::ForwardVector;
	float AttackT = 0.f, AttackDur = 0.7f;
	EAtkStyle AtkStyle = EAtkStyle::Slash;
	EAtkStyle LastStyle = EAtkStyle::Cast;
	int32 AttackDir = 1;            // +1 right-to-left, -1 left-to-right (the follow-up swing); (the sword's jab uses the same two)
	static constexpr int32 JabFirst = 1, JabSecond = -1;   // the sword's two-hit jab (forehand, backhand; they loop)
	float SwingTwistNow = 0.f;      // the chest turning into a sword swing
	float SwingLeanWant = 0.f, SwingLeanNow = 0.f;  // the chest loads back, then leans into the sword cut
	float SwingLoadWant = 0.f, SwingLoadNow = 0.f;  // weight shifts from the rear-side leg through the follow-through
	float BladeYawNow = 0.f, BladePitchNow = 0.f, BladeLeadSign = -1.f;   // a sword slash: where the blade points (knight's frame, degrees) and which way it travels
	float BladeCantNow = 0.f;       // the blade's cant about its own length (degrees; 0 = edge level)
	float BladeAlpha = 0.f;         // how much the wrist is steering the blade (fades in and out of a slash)
	float WristBendNow = 0.f, ForearmTwistNow = 0.f;   // how far the wrist is bent and the forearm twisted for the blade (degrees; for checking a move)
	// Smash-style hit boxes and hurt boxes (HitBoxes.h; KnightCharacterCombat.cpp, TickHitboxes)
	TArray<JeebHit::FCapsule> HitLive;                         // the sword's hit boxes this frame (swept from the last frame); empty outside the active frames
	FVector HitPrev[10]; bool bHitPrevValid = false;            // where each hit box was the frame before
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
	static inline const float& SmashHoldDelay = JeebTune::Storage().SmashHoldDelay;
	static inline const float& SmashChargeTime = JeebTune::Storage().SmashChargeTime;
	static inline const float& SmashHoldMax = JeebTune::Storage().SmashHoldMax;
	static inline const float& SmashEndPause = JeebTune::Storage().SmashEndPause;
	// Hitting a shield (owner, 2026-10-31): the blade bounces back (ReboundBack s) and he staggers back a step, open and unable
	// to move, swing, block, roll or jump for a fixed time: a jab hit ReboundJab s, a smash attack ReboundSmash s (longer than
	// its own end lag). A press in the last moment comes out right after, as after a smash.
	static constexpr float ReboundBack = 0.15f;
	static inline const float& ReboundJab = JeebTune::Storage().ReboundJab;   // (balance pass 2026-10-31: was 0.45 / 0.8)
	static inline const float& ReboundSmash = JeebTune::Storage().ReboundSmash;
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
	float ShotJumpFrom = -1.0e9f;  // test pictures (jump, hop): his height at take-off
	float ShotJumpT = 0.f;         // ...and when Space went down
	float ShotBowAt = -1.f;        // test pictures (bow60): let the string go at this time
	float ShotStrafe = 0.f;        // test pictures (-ShotStrafe=1): walk sideways to his right (-1 left) while shooting
	bool bShotWalk = false;        // test pictures (-ShotMove=bowwalk): walk straight ahead with the bow in hand
	bool ShotSideOn() const;       // test pictures taken from the side: the bow aims straight ahead and the camera stays put
	void SetSmash(ESmash P, float Charge = 0.f, bool bTell = true);
	bool SmashInputOk(ESmash P) const;   // a phase change the player's machine asked for fits what this machine is doing
	void TickSmash(float Dt);
	float SmashTimeLeft() const;   // how long until the smash's end lag is over
	float JabPause() const;        // seconds he holds still after a sword's jab before the next swing can start (Tuning.ini Jab1Pause / Jab2Pause); 0 for anything else
	UFUNCTION(Server, Reliable) void ServerSmash(uint8 Phase, float Charge);
	UFUNCTION(NetMulticast, Reliable) void MulticastSmash(uint8 Phase, float Charge);
	bool SmashLocked() const { return Smash == ESmash::Swing || Smash == ESmash::EndLag || Smash == ESmash::Rebound; }   // no moving
	float AtkCharge = 0.f;
	float LastSwingEnd = -10.f, QueuedClick = -10.f;
	bool bCharging = false;
	float ChargeT = 0.f;
	int32 PendingArrowTier = 0;
	// The bow (see IsDrawingBow above). The player's machine decides the draw and the shot from the button and tells the server,
	// which checks them (it keeps its own draw timer: never more charge than it has seen) and fires the arrow; everyone else is
	// shown the draw.
	enum class EBow : uint8 { None, Drawing, Recover, Reload };
	float CrossReloadLeft = 0.f;                                 // the crossbow: seconds of loading still to do (paused while something else is going on)
	bool bCrossQueued = false;                                   // the crossbow: a click before it was up shoots as soon as it is
	int32 BowKindDrawn = 0;                                      // which bow the draw began with (changing it lets the draw down)
	float DrawTimeNow() const;                                   // how long a full draw takes with what is in his hand
	float BowWalkShare() const;                                  // x walking speed with it, in the state it is in
	EBow Bow = EBow::None;
	float BowT = 0.f;              // time in this phase
	float BowCharge = 0.f;         // 0 - 1: how far drawn
	float BowAimAlpha = 0.f;       // the aiming pose and camera, eased in and out
	float BowCamAlpha = 0.f;
	int32 ArrowPick = 0;           // the arrows picked in the arrow slot (when you still have some)
public:
	// Free climbing and the mantle (2026-11-12): the movement component is KnightMovement.h; the knight's side is KnightCharacterClimb.cpp
	friend class UKnightMovement;
	class UKnightMovement* KnightMove() const;
	bool InClimbMove() const;            // on a rock face, its jump, the ledge climb-up or a mantle
	bool ClimbStowed() const;            // on the rock, climbing up a ledge or mantling: shield and weapon on his back
	mutable FVector MantleEdge = FVector::ZeroVector; mutable uint8 MantleEdgeMode = 0;   // (the pose: where the hands go on a fence, FillWallInput)
	mutable FVector MantleAlong = FVector::ZeroVector; mutable float MantleFar = 0.f;     // (the pose: the way over and how deep the top is, so his feet clear it)
	void DropFromWall(bool bAll = false);   // (server) a clean hit, a knockdown or death takes him off it
	void JumpFromMove(float Dt);         // the ground jump, from inside a move (Space near a fence that turned out not to be a mantle)
	void TickWallClimb(float Dt);
	void TickClimbTests(float Dt);       // (test pictures: -ClimbKeys, -ClimbDo)
	void TickMantleTests(float Dt);      // (tests in the real map: -MantleAt, online -NetMantle)
	float NetMantleT = 0.f;
	void FillWallInput(JeebAnim::FInput& In) const;
	static float ClimbSeconds(int32 ClimbingLevel);   // a full climbing bar, climbing upward, at this Climbing level (Tuning.ini [Climb])
	static float SwimSeconds(int32 SwimmingLevel);    // a full swimming bar, swimming normally, at this Swimming level (Tuning.ini [Swim])
	static float BreathSeconds(int32 SwimmingLevel);  // how long he can keep his head under water
	void TickSwimTests(float Dt);        // (test pictures: -SwimKeys, -SwimDo)
	void TickWaterWalk(float Dt);        // (test runs: -BankScan, -WaterWalk; KnightWaterTests.cpp)
	void FillSwimInput(JeebAnim::FInput& In) const;
	bool bSwimTestFast = false;
	static float BowDrawSeconds(int32 ArcheryLevel);   // the bow's full draw at this Archery level (Tuning.ini [Archery]); the skills window shows it
	static float LongbowDrawSeconds(int32 ArcheryLevel) { return BowDrawSeconds(ArcheryLevel) * JeebTune::V().LongDrawTime / FMath::Max(JeebTune::V().BowDrawTime, 0.01f); }
private:
	static inline const float& BowDrawTime = JeebTune::Storage().BowDrawTime;   // (like the smash attack's charge)
	bool bAimHeld = false;         // the right button is down (the player's own machine): with a bow in hand he aims, and draws again after each shot
	void RightDown();              // the right button: guard with a shield, aim with a bow
	void RightUp();
	static inline const float& BowRecover = JeebTune::Storage().BowRecover;
	// By the draw (none, half, full; straight lines between): a level shot lands about 3 m out barely drawn, 12 m half drawn,
	// 44 m full (owner, 2026-10-31: 10 ft, 40 ft, as before); the speed and how fast it drops together set that
	static inline const float& BowSpeedMin = JeebTune::Storage().BowSpeedMin;   // cm/s
	static inline const float& BowSpeedMid = JeebTune::Storage().BowSpeedMid;
	static inline const float& BowSpeedMax = JeebTune::Storage().BowSpeedMax;
	static inline const float& BowSagMin = JeebTune::Storage().BowSagMin;          // cm/s/s it drops (from half a draw up, about real gravity: owner 2026-11-02, was 300 near full)
	static inline const float& BowSagMid = JeebTune::Storage().BowSagMid;
	static inline const float& BowSagMax = JeebTune::Storage().BowSagMax;
	// Breath of the Wild style (owner, 2026-11-01): a full draw flies dead straight for BowStraight (no drop at all), then falls
	// at BowSagFull; anything less drops from the start (the numbers above) and falls short of the reticle. (2026-11-02, owner:
	// the straight part cut from 50 m to 20 m, then to 5 m the same day, so long shots drop and must be aimed high.)
	static inline const float& BowStraight = JeebTune::Storage().BowStraight;
	static inline const float& BowSagFull = JeebTune::Storage().BowSagFull;
	static constexpr float ShotLeft = 12.f, ShotDown = 8.f;   // the arrow leaves this far left of and below the middle of the screen
	static inline const float& BowDamageMin = JeebTune::Storage().BowDamageMin;   // times the bow's damage, by the draw to the power 1.5 (half drawn: x0.54)
	static inline const float& BowDamageMax = JeebTune::Storage().BowDamageMax;
	static inline const float& BowCamLength = JeebTune::Storage().BowCamLength;                                      // the camera's distance while aiming (540 normally)
	void StartDraw();
	void ReleaseDraw();
	void CancelDraw();
	void SetBow(EBow P);
	void TickBow(float Dt);
	FVector ArrowStart() const;    // where arrows leave from (the same on every machine)
	FVector BowAimDir(FVector* OutTarget = nullptr) const;     // the player's machine: from ShotStart through what is under the reticle; elsewhere where he looks
	FVector ShotStart() const;     // the player's machine: where the arrow really flies from, on the reticle's line abreast of the bow (else ArrowStart)
	FVector CheckShotStart(const FVector& Asked) const;   // the server: that point if it is near the bow and nothing solid is between, else ArrowStart
	void FireArrow(const FVector& Dir, float Charge, int32 ArrowId, const FVector& From);   // (server; From: CheckShotStart's answer)
	UFUNCTION(Server, Reliable) void ServerBowDraw();
	UFUNCTION(Server, Reliable) void ServerBowShoot(FVector_NetQuantizeNormal Dir, float Charge, int32 ArrowId, FVector_NetQuantize10 From);
	UFUNCTION(Server, Reliable) void ServerBowCancel();
	UFUNCTION(NetMulticast, Reliable) void MulticastBow(uint8 Phase);
	UPROPERTY() USceneComponent* BowHold = nullptr;              // the bow in the left fist, built at its grip (RebuildHeld)
	int32 LookArrows = 0;                                        // other players' knights: the arrows they have (RepLook), for their quivers
	UPROPERTY() UProceduralMeshComponent* BowString = nullptr;   // its straight string, hidden while drawn (BowBits take over)
	UPROPERTY() TArray<UStaticMeshComponent*> BowBits;           // the drawn string's two halves, the nocked arrow and its head
	bool bBitsCross = false;                                     // (the string is painted for the crossbow)
	UPROPERTY() TArray<UStaticMeshComponent*> QuiverParts;       // the quiver of arrows on his back while the bow is in hand
	int32 QuiverArrowId = -1;                                    // which arrows its feathers are coloured for
	bool bNockModel = false;                                     // (the nocked arrow is the arrows' model, JeebProps)
	int32 NockArrowId = -1;                                      // which arrows it is coloured for
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> QuiverBody;   // the quiver's model (2026-11-12)
	// ---- The troll's throwing rocks (2026-11-22, owner: "remove the bow and arrow slots from the troll ... give him rocks to throw";
	// KnightCharacterRock.cpp). Key 2 (the bow's slot) takes the rocks in hand: the club goes on his back and the sack of rocks stays in his
	// left hand. A mouse button held winds up (the longer, the further and harder, up to a full charge), letting go throws toward the reticle.
	// The player's machine decides the throw from the button and tells the server, which checks the charge against its own clock and throws.
	enum class ERock : uint8 { None, Charging, Throw };
	ERock Rock = ERock::None;
	float RockT = 0.f;              // seconds in this phase
	float RockCharge = 0.f;         // 0..1 the wind-up (this machine)
	float RockThrowCharge = 0.f;    // the charge the last throw went with (its follow-through is bigger)
	float RockAimAlpha = 0.f;       // the aiming camera, eased
	float RockCooldown = 0.f;       // seconds before the next wind-up may start
	UPROPERTY(ReplicatedUsing = OnRep_RockMode) bool bRockMode = false;   // the rocks in hand (key 2), the server's word
	UFUNCTION() void OnRep_RockMode();
	void SetRockMode(bool bOn);
	void RockPress();
	void RockRelease();
	void SetRockPhase(ERock P);
	void TickRock(float Dt);
	void ThrowRock(const FVector& Dir, float Charge, const FVector& From);   // (server)
	FVector RockStart() const;      // where a rock leaves from (the same on every machine): high over his right shoulder
	FVector RockAimDir(float Charge, FVector* OutTarget = nullptr) const;   // the player's machine: the launch to land on what is under the reticle, as far as the charge reaches
	void UpdateRockSack();          // the sack in his left fist and the rock in his right while he winds up (players' machines)
	bool TrollRockTest(const FString& Do);   // (test pictures and online tests: -SuitDo=rock<hundredths> / -TrollNetDo=rock<hundredths>)
	float RockSpeedFor(float Charge) const;
	float RockSagFor(float Charge) const;
	UFUNCTION(Server, Reliable) void ServerRockMode(bool bOn);
	UFUNCTION(Server, Reliable) void ServerRockCharge();
	UFUNCTION(Server, Reliable) void ServerRockThrow(FVector_NetQuantizeNormal Dir, float Charge, FVector_NetQuantize10 From);
	UFUNCTION(Server, Reliable) void ServerRockCancel();
	UFUNCTION(NetMulticast, Reliable) void MulticastRock(uint8 Phase, float Charge);
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RockSack;     // the sack of rocks in his left fist
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RockInHand;   // the rock in his right fist while he winds up
	bool bRockSackTried = false;
	bool bRockClubHidden = false;   // (the club's parts are hidden while the rocks are in hand)
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> AmuletParts;       // the worn amulet on its chain round his neck (UpdateAmulet)
	int32 AmuletShownId = -1;
	void UpdateAmulet();
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> RingParts;         // the worn rings on his gauntlet fingers (UpdateRings, 2026-11-21)
	UPROPERTY(Transient) TObjectPtr<USceneComponent> RingHand[2];                // each hand's joint the rings hang on (0 right, 1 left)
	struct FRingSlot { TWeakObjectPtr<USceneComponent> Band, Stone; int32 Side = 0; float Across = 0.f, AlongAdd = 0.f; };
	TArray<FRingSlot> RingSlots;   // (each ring's two parts, placed every frame by how far its hand is closed)
	int32 RingShownKey = -1;
	void UpdateRings();
	void UpdateBowBits();
	void UpdateQuiver();
	// (online: the server's numbers, sent to everyone)
	UPROPERTY(Replicated) float Health = 100.f;
	UPROPERTY(Replicated) int32 MaxHealth = 100;
	float LastHurtTime = -100.f, InvulnUntil = 0.f;
	FVector SpawnLoc = FVector::ZeroVector;
	float PrevC = 1.f;
	float SlideDustTimer = 0.f;
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
	float ArmYaw = 0.f;
	bool bSideways = false; // woodcutting sweeps the arm sideways instead of overhead

	TArray<TPair<UStaticMeshComponent*, FLinearColor>> Parts;
};

// The playable troll (2026-11-19): the knight's pawn in troll mode (KnightCharacterTroll.cpp). AKnightGameMode gives one to a player who
// asked for it (-PlayAs=Troll; AKnightPlayerController::ServerChooseClass).
UCLASS()
class ATrollCharacter : public AKnightCharacter
{
	GENERATED_BODY()
public:
	ATrollCharacter(const FObjectInitializer& ObjectInitializer);
};

// The playable wizard (2026-11-21): the knight's pawn in wizard mode (KnightCharacterWizard.cpp). AKnightGameMode gives one to a player who
// asked for it (-PlayAs=Wizard; AKnightPlayerController::ServerChooseWizard).
UCLASS()
class AWizardCharacter : public AKnightCharacter
{
	GENERATED_BODY()
public:
	AWizardCharacter(const FObjectInitializer& ObjectInitializer);
};
