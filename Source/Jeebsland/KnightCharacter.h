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
	float Shade = 1.f;
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
	virtual void Jump() override; // also stands you up from a bench, and ends a slide

	// For the HUD
	float GetStaminaFraction() const { return Stamina / MaxStamina; }
	bool IsExhausted() const { return bExhausted; }

	// Mouse/stick look passes through here, so the sensitivity setting scales it directly
	virtual void AddControllerYawInput(float Val) override;
	virtual void AddControllerPitchInput(float Val) override;

	// Called by resource nodes: start/stop the working animation
	void BeginGathering(AResourceNode* Node, ESkillType Skill);
	void EndGathering();

	// Screens: Tab = inventory, E at the store / smithy = trade, O = settings
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
	USkillComponent* GetSkills() const { return Skills; }
	// Text for the "Press E ..." prompt, or false if nothing is in reach
	bool GetInteractPrompt(FString& OutText) const;
	// Where the floating E prompt hangs: just in front of and above the thing you can use. bOutE is false for things that are not an E action (a locked door you cannot pick, a place to fish).
	bool GetInteractAnchor(FVector& Out, bool& bOutE) const;

	// Combat: health, damage dealt and taken
	float GetHealth() const { return Health; }
	int32 GetMaxHealth() const { return MaxHealth; }
	// Something hits you for Raw points; armour, Defence level and a raised shield soak some of it
	void Hurt(int32 Raw, AActor* From);
	// You (or your arrow / bolt) hit something: applies the damage and awards skill XP
	void DealDamage(UHealthComponent* Target, int32 Dmg, bool bCrit, ESkillType Skill);

	// Settings screen (press O)
	void ToggleSettings();
	bool IsSettingsOpen() const { return bSettingsOpen; }
	float GetLookSensitivity() const { return LookSensitivity; }
	void SetLookSensitivity(float Value);

	// Graphics options (also in the settings screen)
	bool GetFancyLighting() const { return bFancyLighting; }
	void SetFancyLighting(bool bOn);
	float GetRenderScale() const { return RenderScale; }
	void SetRenderScale(float Value);

protected:
	virtual void BeginPlay() override;
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
	void OnInteractKey();       // E: sit, trade, talk, mount, open doors ...
	void UseDoor(ACottage* Hut);   // open, close, or try to pick the lock
	void CallHorse();           // T
	void Mount(AHorse* H);
	void Dismount();
	void SitOnSeat(int32 SeatIdx);
	void StandUp();
	void OnInventoryKey() { ToggleInventory(); }
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
	UPROPERTY() UInstancedStaticMeshComponent* DustISM = nullptr;
	UPROPERTY() UInputModifierScalar* LookScalar = nullptr;
	bool bSettingsOpen = false;
	bool bInventoryOpen = false;
	bool bSkillsOpen = false;
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
	float LastHorseHurtTime = -100.f;
	bool bHorseTired = false;
	bool bFishing = false;
	bool bFancyLighting = false;   // Lumen global illumination, reflections and volumetric clouds
	float RenderScale = 1.f;       // 0.5 - 1.0 of the screen resolution
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
	UPROPERTY() USceneComponent* ArmPivot = nullptr;    // right shoulder (sword arm); mining and chopping drive this
	UPROPERTY() USceneComponent* ArmPivotL = nullptr;   // left shoulder (shield arm)
	UPROPERTY() USceneComponent* ElbowR = nullptr;
	UPROPERTY() USceneComponent* ElbowL = nullptr;

	// The cape: a small cloth simulation (a grid of points pinned across the shoulders), redrawn every frame
	UPROPERTY() UProceduralMeshComponent* CapeCloth = nullptr;
	UPROPERTY() class UMaterialInstanceDynamic* CapeMid = nullptr;
	static constexpr int32 CapeNX = 7, CapeNY = 10;
	TArray<FVector> CapeP, CapePrev;
	bool bCapeInit = false;
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
	float Health = 100.f;
	int32 MaxHealth = 100;
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
