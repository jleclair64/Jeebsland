#include "KnightCharacter.h"
#include "GameFramework/PlayerState.h"
#include "JeebBroomData.h"
#include "Messages.h"
#include "KnightAnimator.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "EnhancedInputComponent.h"
#include "SkillComponent.h"
#include "ResourceNode.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Terrain.h"
#include "GrassField.h"
#include "Canopy.h"
#include "Pig.h"
#include "Inventory.h"
#include "Npc.h"
#include "Horse.h"
#include "Cottage.h"
#include "HouseInterior.h"
#include "HouseLoot.h"
#include "Loot.h"
#include "Pond.h"
#include "HealthComponent.h"
#include "Projectile.h"
#include "GearWall.h"
#include "Combat.h"
#include "WeaponDesign.h"
#include "ProceduralMeshComponent.h"
#include "Misc/ConfigCacheIni.h"
#include "OakHill.h"

// Oak Hill's shop counters (no shopkeepers yet, owner 2026-11-13: "We'll add NPCs to the shops later"): standing within this of a
// counter, E opens that shop's window. The server checks its own range (NearTrader, 800 cm, like the market's traders)
static constexpr float CounterReach = 250.f;
static const JeebOakHill::FCounter* CounterInReach(const AActor* Me)
{
	const FVector P = Me->GetActorLocation();
	const JeebOakHill::FCounter* C = JeebOakHill::NearAnyCounter(P.X, P.Y, CounterReach);
	return C && FMath::Abs(P.Z - 90.f - JeebTerrain::HeightAt(C->At.X, C->At.Y)) < 200.f ? C : nullptr;
}

void AKnightCharacter::Interact()
{
	bAttackDown = !(IsUIOpen() || bPanelCursor);   // (held: the sword's smash attack, see TickSmash; a click on the HUD with the cursor out (Tab) is not a game click)
	if (bKnockedDown) { GetUp(0); return; }   // (a click gets straight up)
	if (IsUIOpen() || bPanelCursor || bSitting || bRolling || bSliding || bClimbing || InClimbMove() || StunLeft > 0.f || bDrinking) return; // (nor in hitstun, nor with a bottle in that hand) clicks belong to the open screen (or the side panel); no attacks mid-move
	// Bows (owner 2026-11-06): either button brings it up or draws it while held, and letting go of the last one shoots (InteractReleased, RightUp). A crossbow has no draw: it goes at once, at full power.
	// The crossbow (owner 2026-11-12, like a sniper rifle in Battlefield): only the right button brings it up; a left click while it is up shoots (a click while it is still coming up shoots as soon as it is).
	if (InRockMode()) { RockPress(); return; }   // (the troll's rocks in hand: either button winds up, KnightCharacterRock.cpp)
	if (SelectedKind() == JeebItems::K_Crossbow)
	{
		if (Bow == EBow::Drawing && bAimHeld) ReleaseDraw();
		return;
	}
	if (JeebItems::IsShooter(SelectedKind()))
	{
		if (Bow == EBow::None) StartDraw();
		return;
	}
	if (Bow == EBow::Drawing) return;
	if (bFishing && ActiveNode.IsValid())   // (2026-11-26) fishing: a click reels in; the server judges whether it was in time
	{
		if (AFishingSpot* Spot = Cast<AFishingSpot>(ActiveNode.Get()))
		{
			if (HasAuthority()) Spot->Reel(this);
			else Inv(EInvOp::Reel, 0, 0, 0, nullptr, Spot->GetActorLocation());
		}
		return;
	}
	if (ActiveNode.IsValid()) return;                               // already working
	if (bAttacking) { QueuedClick = GetWorld()->GetTimeSeconds(); QueuedJabs = 1; return; }   // swinging: buffer ONE press for the next swing (Smash style: more presses in the same swing don't stack)

	// Gather from the closest rock or tree within reach that the knight is facing
		AResourceNode* Best = nullptr;
	float BestDist = 1.0e9f;
	for (TActorIterator<AResourceNode> It(GetWorld()); It; ++It)
	{
		const FVector ToNode = It->GetActorLocation() - GetActorLocation();
		const float D = ToNode.Size2D();
		if (D >= It->ReachRadius()) continue;
		const float FacingDot = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), ToNode.GetSafeNormal2D());
		if (FacingDot < It->MinFacing()) continue; // outside about 60 degrees (wider for fishing)
		if (D < BestDist)
		{
			BestDist = D;
			Best = *It;
		}
	}

	// Online the server does the gathering (and its checks); we just ask, and it tells us when to start the animation
	if (Best && !HasAuthority()) { Inv(EInvOp::Gather, 0, 0, 0, nullptr, Best->GetActorLocation()); return; }
	const bool bHandled = Best && Best->StartGathering(this);

	// Nothing to gather: just swing whatever is in your hand
	if (!ActiveNode.IsValid() && !bHandled)
	{
		const int32 K = SelectedKind();
		const float Now = GetWorld()->GetTimeSeconds();
		if (JeebItems::IsShooter(K))
		{
			// (a bow only shoots while drawn: the right button, RightDown)
		}
		else if (WizardDashKind(K) && bSprinting && !GetCharacterMovement()->IsFalling() && GetVelocity().Size2D() > WalkNow() * 1.15f
			&& FVector::DotProduct(GetVelocity().GetSafeNormal2D(), GetActorForwardVector().GetSafeNormal2D()) > 0.7f)
		{
			StartAttack(EAtkStyle::Dash, 1, 0.f);   // (the wizard, 2026-11-21: sprinting forward with his staff or wand, the knight's dash attack, a melee lunge)
		}
		else if (K == JeebItems::K_Wand || K == JeebItems::K_Staff)
		{
			BeginCastCharge();   // (2026-11-22: hold to charge, let go to cast: ReleaseCastCharge)
		}
		else if (JeebItems::FightsLikeSword(K) && bSprinting && !GetCharacterMovement()->IsFalling() && GetVelocity().Size2D() > WalkNow() * 1.15f
			&& FVector::DotProduct(GetVelocity().GetSafeNormal2D(), GetActorForwardVector().GetSafeNormal2D()) > 0.7f)
		{
			StartAttack(EAtkStyle::Dash, 1, 0.f);   // sprinting forward: the dash attack, a lunge
		}
		else if (JeebItems::FightsLikeSword(K))
		{
			// The sword's jab: one swing per click (a click during a swing is remembered, see TickAttack); a click just after
			// a swing has ended still carries on, forehand and backhand in turn
			int32 Next = JabFirst;
			if (LastStyle == EAtkStyle::Slash && Now - LastSwingEnd <= 0.3f && LastSwingDir == JabFirst) Next = JabSecond;
			StartAttack(EAtkStyle::Slash, Next, 0.f);
			if (Next == JabFirst && !GetCharacterMovement()->IsFalling()) SetSmash(ESmash::Deciding);   // (not in the air)   // (held long enough, hit 1 becomes the smash attack)
		}
		else
		{
			// A second swing within 0.3 s of the first sweeps back left-to-right, a little faster
			const bool bCombo = (LastStyle == EAtkStyle::Slash && LastSwingDir > 0 && Now - LastSwingEnd <= 0.3f);
			StartAttack(EAtkStyle::Slash, bCombo ? -1 : 1, 0.f);
		}
	}
}

void AKnightCharacter::InteractReleased()
{
	bAttackDown = false;
	if (InRockMode()) { if (!bAimHeld) { if (IsUIOpen() || bPanelCursor) { if (Rock == ERock::Charging) { SetRockPhase(ERock::None); if (!HasAuthority()) ServerRockCancel(); } } else RockRelease(); } return; }   // (the last button let go throws)
	if (bCastCharging) { ReleaseCastCharge(); return; }   // (the wand's and staff's charged shot)
	if (SelectedKind() == JeebItems::K_Crossbow) return;   // (letting go of the left button does nothing with the crossbow: the click shot)
	if (Bow == EBow::Drawing && !bAimHeld)
	{
		if (IsUIOpen() || bPanelCursor) CancelDraw(); else ReleaseDraw();
		return;
	}
	// The sword's smash attack: let go before it starts charging and it was just hit 1; let go while charging and it swings
	if (Smash == ESmash::Deciding) { SetSmash(ESmash::None); return; }
	if (Smash == ESmash::Charging) { SetSmash(ESmash::Swing, SmashCharge); return; }
	if (!bCharging) return;
	bCharging = false;
	if (IsUIOpen() || bAttacking || bSitting || bRolling || bSliding || bClimbing || InClimbMove()) return;
	const float C = FMath::Clamp(ChargeT / 1.0f, 0.f, 1.f);
	StartAttack(EAtkStyle::Thrust, 1, C);
	if (!GetCharacterMovement()->IsFalling())
	{
		LaunchCharacter(GetActorForwardVector().GetSafeNormal2D() * (200.f + 700.f * C), true, false);   // lunge forward
	}
}

AKnightCharacter::EInteract AKnightCharacter::FindInteractable(int32& OutIndex, ANpc*& OutNpc, AHorse*& OutHorse) const
{
	OutIndex = -1;
	OutNpc = nullptr;
	OutHorse = nullptr;
	const FVector2D Me(GetActorLocation().X, GetActorLocation().Y);
	// Your horse, if it is standing right beside you
	for (TActorIterator<AHorse> It(GetWorld()); It; ++It)
	{
		if (It->HasRider() || It->IsDown()) continue;
		if (FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y)) < JeebHorse::Reach) { OutHorse = *It; return EInteract::Horse; }
	}
	// Villagers (store, smithy, gate knight): talk to the nearest one in reach
	float NpcD = 230.f;
	for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
	{
		const float D = FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
		if (D < NpcD) { NpcD = D; OutNpc = *It; }
	}
		if (OutNpc) return EInteract::Npc;
	// Things left on the ground by what died
	FoundDrop = nullptr;
	{
		float BestDrop = 190.f;
		for (TActorIterator<ALootDrop> It(GetWorld()); It; ++It)
		{
			const float D = FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
			if (D < BestDrop && FMath::Abs(GetActorLocation().Z - 90.f - It->GetActorLocation().Z) < 150.f) { BestDrop = D; FoundDrop = *It; }
		}
		if (FoundDrop.IsValid()) return EInteract::Drop;
	}
	// The MoveLab gear wall: the peg the camera looks at, within reach
	FoundWall = nullptr;
	FoundPeg = -1;
	if (Controller)
	{
		FVector From;
		FRotator Rot;
		Controller->GetPlayerViewPoint(From, Rot);
		for (TActorIterator<AGearWall> It(GetWorld()); It; ++It)
		{
			const int32 Peg = It->PegAt(From, Rot.Vector(), GetActorLocation(), 450.f);
			if (Peg >= 0) { FoundWall = *It; FoundPeg = Peg; return EInteract::GearWall; }
		}
	}
	// Arrows lying in the world (spent ones, in a shield or armour, are never offered)
	FoundArrow = nullptr;
	{
		float BestArrow = 170.f;
		for (TActorIterator<AProjectile> It(GetWorld()); It; ++It)
		{
			if (!It->CanPickUp()) continue;
			const float D = FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
			if (D < BestArrow && FMath::Abs(GetActorLocation().Z - 60.f - It->GetActorLocation().Z) < 170.f) { BestArrow = D; FoundArrow = *It; }
		}
		if (FoundArrow.IsValid()) return EInteract::Arrow;
	}
	// Doors of huts, and the ingot stash inside the locked ones
	FoundCottage = nullptr;
	{
		const FVector Me3 = GetActorLocation();
		ACottage* LootHut = nullptr;
		ACottage* DoorHut = nullptr;
		float BestLoot = 170.f, BestDoor = 210.f;
		for (TActorIterator<ACottage> It(GetWorld()); It; ++It)
		{
			if (It->HasLoot())
			{
				const FVector LP = It->LootPos();
				const float D = FVector::Dist2D(Me3, LP);
				if (D < BestLoot && FMath::Abs(Me3.Z - 90.f - LP.Z) < 150.f) { BestLoot = D; LootHut = *It; }
			}
			const FVector DP = It->DoorPos();
			const float D = FVector::Dist2D(Me3, DP);
			if (D < BestDoor && FMath::Abs(Me3.Z - 90.f - DP.Z) < 200.f) { BestDoor = D; DoorHut = *It; }
		}
		if (LootHut) { FoundCottage = LootHut; return EInteract::Loot; }
		if (DoorHut) { FoundCottage = DoorHut; return EInteract::Door; }
	}
	// Benches
	float BestD = 170.f;
	const TArray<JeebTerrain::FSeat>& Seats = JeebTerrain::Seats();
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float D = FVector2D::Distance(Me, Seats[i].Pos);
		if (D < BestD) { BestD = D; OutIndex = i; }
	}
	return OutIndex >= 0 ? EInteract::Bench : EInteract::None;
}

bool AKnightCharacter::GetInteractPrompt(FString& OutText) const
{
	if (IsUIOpen() || bRolling || bSliding || bClimbing || InClimbMove()) return false;
	if (bSitting || bMounted) return false;   // E to stand up or dismount still works; it just shows no prompt
	if (GetCharacterMovement()->IsFalling()) return false;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	if (const AHouseInterior* H = AHouseInterior::DoorInReach(this, true)) { OutText = H->IsDoorOpen() ? TEXT("Press E to close the door") : TEXT("Press E to open the door"); return true; }
	if (const JeebHouseLoot::FLootBox* Box = JeebHouseLoot::InReach(this))   // (2026-11-26, HouseLoot.h)
	{
		if (GetWorld()->GetTimeSeconds() < LockPickEnd) return false;   // (the pick bar shows instead)
		const TCHAR* What = JeebHouseLoot::KindName(Box->Kind);
		if (Box->Lock <= 0) OutText = FString::Printf(TEXT("Press E to search the %s"), What);
		else if (Skills && Skills->GetLevel(ESkillType::Lockpicking) >= Box->Lock) OutText = FString::Printf(TEXT("Press E to pick the %s's lock (Lockpicking %d)"), What, Box->Lock);
		else OutText = FString::Printf(TEXT("Locked. Needs Lockpicking level %d"), Box->Lock);
		return true;
	}
	if (NearBroomStall()) { OutText = TEXT("Broom shop"); return true; }   // (Silver Point's broom stall, 2026-11-22)
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: OutText = Horse && Horse->IsWild() ? TEXT("Press E to climb on the wild horse") : Horse && Horse->GetOwnerKnight() == this ? TEXT("Press E to ride your horse") : TEXT("Press E to ride the horse"); return true;
	case EInteract::Npc:
		OutText = FString::Printf(TEXT("Press E to talk to %s"), *Npc->GetNpcName());
		return true;
	case EInteract::Bench: OutText = TEXT("Press E to sit"); return true;
		case EInteract::Loot:  OutText = TEXT("Press E to take the ingots"); return true;
	case EInteract::Arrow: OutText = FoundArrow.IsValid() && JeebItems::IsRock(FoundArrow->GetArrowId()) ? TEXT("Press E to pick up the rock") : TEXT("Press E to pick up the arrow"); return true;
	case EInteract::GearWall:
		if (const AGearWall* W = FoundWall.Get(); W && W->GetPegs().IsValidIndex(FoundPeg))
		{
			const AGearWall::FPeg& P = W->GetPegs()[FoundPeg];
			OutText = P.Count > 1 ? FString::Printf(TEXT("Press E to take %d %s"), P.Count, *JeebItems::ItemName(P.Id)) : FString::Printf(TEXT("Press E to take the %s"), *JeebItems::ItemName(P.Id));
			return true;
		}
		return false;
	case EInteract::Drop:
		if (const ALootDrop* Drop = FoundDrop.Get()) { OutText = Drop->IsSack() ? FString(TEXT("Press E to open the sack")) : Drop->Describe(); return true; }
		return false;
	case EInteract::Door:
	{
		const ACottage* Hut = FoundCottage.Get();
		if (!Hut) return false;
		if (Hut->IsLocked())
		{
			const int32 Need = Hut->GetLockLevel();
			OutText = (Skills && Skills->GetLevel(ESkillType::Lockpicking) >= Need)
				? FString::Printf(TEXT("Press E to pick the lock (Lockpicking %d)"), Need)
				: FString::Printf(TEXT("Locked. Needs Lockpicking level %d"), Need);
		}
				else OutText = Hut->IsDoorOpen() ? TEXT("Press E to close the door") : TEXT("Press E to open the door");
		return true;
	}
	default: break;
	}
	// At an open house's door
	if (const AHouseInterior* H = AHouseInterior::DoorInReach(this))
	{
		OutText = H->IsDoorOpen() ? TEXT("Press E to close the door") : TEXT("Press E to open the door");
		return true;
	}
	// At an Oak Hill shop's counter
	if (const JeebOakHill::FCounter* C = CounterInReach(this))
	{
		OutText = C->Shop == JeebOakHill::EShop::Smith ? FString(TEXT("Press E to use the forge"))
			: C->Shop == JeebOakHill::EShop::Bank ? FString::Printf(TEXT("Press E to bank at the %s"), C->Name)
			: FString::Printf(TEXT("Press E to trade at the %s"), C->Name);
		return true;
	}
	// Facing a place to fish
	if (!bFishing)
	{
		for (TActorIterator<AFishingSpot> It(GetWorld()); It; ++It)
		{
			const FVector To = It->GetActorLocation() - GetActorLocation();
			if (To.Size2D() < It->ReachRadius() && FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()) > It->MinFacing())
			{
				OutText = It->PromptText();
				return true;
			}
		}
	}
	// (2026-11-26, Docs/EconomyPlan.md) facing another player: ask them to trade
	if (const AKnightCharacter* O = TradeTargetInReach()) { OutText = FString::Printf(TEXT("Press E to trade with %s"), O->GetPlayerState() ? *O->GetPlayerState()->GetPlayerName() : TEXT("them")); return true; }
	return false;
}

bool AKnightCharacter::GetInteractAnchor(FVector& Out, bool& bOutE) const
{
	if (IsUIOpen() || bRolling || bSliding || bClimbing || bSitting || bMounted || GetCharacterMovement()->IsFalling()) return false;
	const FVector Me = GetActorLocation();
	auto Float = [&](const FVector& P, float Up)   // a hand's breadth toward the knight and up, so it hangs just in front of the thing
	{
		FVector To = Me - P;
		To.Z = 0.f;
		Out = P + To.GetSafeNormal() * 10.f + FVector(0.f, 0.f, Up);   // right against the thing (the prompt is drawn on screen, so it can never sink into a wall)
	};
	bOutE = true;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	if (const AHouseInterior* H = AHouseInterior::DoorInReach(this, true)) { Float(H->DoorHandle(), 0.f); return true; }
	if (const JeebHouseLoot::FLootBox* Box = JeebHouseLoot::InReach(this))
	{
		Float(Box->Top, 0.f);
		if (Box->Lock > 0 && !(Skills && Skills->GetLevel(ESkillType::Lockpicking) >= Box->Lock)) bOutE = false;
		return true;
	}
	if (NearBroomStall()) { Float(JeebBroom::StallSpot(), 160.f); return true; }   // (Silver Point's broom stall: no anchor meant no E prompt on screen, 2026-11-22)
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: Float(Horse->GetActorLocation(), 150.f); return true;
	case EInteract::Npc:   Float(Npc->GetActorLocation(), Npc->HeadTopZ() + 15.f - Npc->GetActorLocation().Z); return true;   // (level with their name tag: the HUD sets it beside the name)
	case EInteract::Bench:
	{
		const FVector2D P = JeebTerrain::Seats()[Idx].Pos;
		Float(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y)), 90.f);
		return true;
	}
	case EInteract::Loot:
		if (const ACottage* Hut = FoundCottage.Get()) { Float(Hut->LootPos(), 70.f); return true; }
		return false;
	case EInteract::Arrow:
		if (const AProjectile* Arrow = FoundArrow.Get()) { Float(Arrow->GetActorLocation(), 40.f); return true; }
		return false;
	case EInteract::GearWall:
		if (const AGearWall* W = FoundWall.Get(); W && W->GetPegs().IsValidIndex(FoundPeg)) { Float(W->GetPegs()[FoundPeg].Pos, 25.f); return true; }
		return false;
	case EInteract::Drop:
		if (const ALootDrop* Drop = FoundDrop.Get()) { Float(Drop->GetActorLocation(), 60.f); return true; }
		return false;
	case EInteract::Door:
	{
		const ACottage* Hut = FoundCottage.Get();
		if (!Hut) return false;
		Float(Hut->DoorPos(), 105.f);   // door handle height
		if (Hut->IsLocked() && !(Skills && Skills->GetLevel(ESkillType::Lockpicking) >= Hut->GetLockLevel())) bOutE = false;
		return true;
	}
	default: break;
	}
	if (const AHouseInterior* H = AHouseInterior::DoorInReach(this)) { Float(H->DoorHandle(), 0.f); return true; }
	if (const JeebOakHill::FCounter* C = CounterInReach(this))
	{
		// at the shop's front, not out at the counter spot (owner 2026-11-16: "move the E prompt closer to the store"), the same height
		const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(C->Facing)), FMath::Sin(FMath::DegreesToRadians(C->Facing)));
		const FVector2D P = C->At + Dir * (JeebOakHill::CounterOut - 25.f);
		Out = FVector(P.X, P.Y, JeebTerrain::HeightAt(C->At.X, C->At.Y) + 120.f);   // (about the counter's height)
		return true;
	}
	if (!bFishing)
	{
		for (TActorIterator<AFishingSpot> It(GetWorld()); It; ++It)
		{
			const FVector To = It->GetActorLocation() - Me;
			if (To.Size2D() < It->ReachRadius() && FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To.GetSafeNormal2D()) > It->MinFacing())
			{
				Float(It->GetActorLocation(), 60.f);
				bOutE = false;   // fishing is a click, not an E
				return true;
			}
		}
	}
	if (const AKnightCharacter* O = TradeTargetInReach()) { Float(O->GetActorLocation(), 90.f); return true; }   // (a player to trade with)
	return false;
}


void AKnightCharacter::OnInteractKey()
{
	if (bSitting) { StandUp(); return; }
	if (bMounted && !IsUIOpen()) { Dismount(); return; }
	if (bDialogueOpen) { AdvanceDialogue(); return; }
	if (bShopOpen) { CloseShop(); return; }
	if (bSmithOpen) { CloseSmith(); return; }
	if (bBankOpen) { CloseBank(); return; }
	if (!IsUIOpen() && !bOnBroom && NearBroomStall()) { OpenBroomShop(); return; }   // (Silver Point's broom stall, 2026-11-22)
	if (IsUIOpen() || bRolling || bSliding || bClimbing || InClimbMove() || GetCharacterMovement()->IsFalling()) return;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	// Facing an open house's door: the door first, even with its villager on the doorstep
	if (const AHouseInterior* H = AHouseInterior::DoorInReach(this, true)) { Inv(EInvOp::HouseDoor, 0, 0, 0, nullptr, H->DoorKey()); return; }
	// (2026-11-26) a chest, cupboard or crate in a town house: search it, or pick its lock (the server decides, HouseLoot.cpp)
	if (const JeebHouseLoot::FLootBox* Box = JeebHouseLoot::InReach(this))
	{
		if (GetWorld()->GetTimeSeconds() < LockPickEnd) return;   // (already picking)
		Inv(Box->Lock > 0 ? EInvOp::BoxPick : EInvOp::BoxSearch, 0, 0, 0, nullptr, Box->At);
		return;
	}
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: Mount(Horse); break;
	case EInteract::Npc:   StartDialogue(Npc); break;
	case EInteract::Bench: SitOnSeat(Idx); break;
		case EInteract::Door:  UseDoor(FoundCottage.Get()); break;
	case EInteract::Arrow:
		if (AProjectile* Arrow = FoundArrow.Get())
		{
			if (!HasAuthority()) Arrow->SetActorHiddenInGame(true);   // (gone from our view at once; the server hands it over)
			Inv(EInvOp::PickArrow, 0, 0, 0, Arrow);
		}
		break;
	case EInteract::GearWall:
		if (FoundPeg >= 0) Inv(EInvOp::TakeFromWall, FoundPeg, 0, 0, nullptr);
		break;
	case EInteract::Drop:
		if (ALootDrop* Drop = FoundDrop.Get())
		{
			if (Drop->IsSack()) OpenLoot(Drop);   // a sack opens its own windows beside yours, with the cursor out
			else Inv(EInvOp::LootTakeAll, 0, 0, 0, Drop);
		}
		break;
	case EInteract::Loot:
		if (ACottage* Hut = FoundCottage.Get())
		{
			if (!HasAuthority()) Hut->TakeLoot();   // empty our own copy of the stash; the server pays out from its copy
			Inv(EInvOp::HutLoot, 0, 0, 0, nullptr, Hut->GetActorLocation());
		}
		break;
	default:
		// An open house's door: the server opens or shuts it, and everyone's copy swings (HouseInterior.h)
		if (const AHouseInterior* H = AHouseInterior::DoorInReach(this)) { Inv(EInvOp::HouseDoor, 0, 0, 0, nullptr, H->DoorKey()); break; }
		// An Oak Hill shop's counter: straight to its window (the server checks the range on every trade, NearTrader)
		if (const JeebOakHill::FCounter* C = CounterInReach(this))
		{
			// (2026-11-18) the keeper behind this counter (NpcLife.h), if there is one within 4 m: they turn to you and talk, and the shop
			// camera frames them (KnightShopCam.cpp); none: walking away is measured from the counter, KnightCharacter.cpp
			TradeNpc = nullptr;
			float KeeperD = 400.f;
			const ENpcRole Keeps = C->Shop == JeebOakHill::EShop::Smith ? ENpcRole::Blacksmith : C->Shop == JeebOakHill::EShop::Bank ? ENpcRole::Banker : ENpcRole::Shopkeeper;
			for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
				if (It->GetRole() == Keeps)
				{
					const float D = FVector2D::Distance(FVector2D(It->GetActorLocation()), C->At);
					if (D < KeeperD) { KeeperD = D; TradeNpc = *It; }
				}
			if (ANpc* K = TradeNpc.Get()) K->SetTalking(true);
			bCrouched = false;
			bBlocking = false;
			SetActorRotation(FRotator(0.f, C->Facing, 0.f));
			if (C->Shop == JeebOakHill::EShop::Smith) OpenSmith();
			else if (C->Shop == JeebOakHill::EShop::Bank) OpenBank();
			else OpenShop();
		}
		else if (AKnightCharacter* O = TradeTargetInReach()) AskTrade(O);   // (another player: ask to trade, KnightCharacterTrade.cpp)
		break;
	}
}

void AKnightCharacter::UseDoor(ACottage* Hut)
{
	if (!Hut || !Skills) return;
	if (!Hut->IsLocked())
	{
		// online the server's door is the real one: ours swings at once, and the server opens its own (and everyone's)
		if (!HasAuthority()) Hut->ToggleDoor();
		Inv(EInvOp::ToggleDoor, 0, 0, 0, nullptr, Hut->GetActorLocation());
		return;
	}
	// Picking the lock: the server rolls it and awards the XP (DoInv, EInvOp::PickLock)
	Inv(EInvOp::PickLock, 0, 0, 0, nullptr, Hut->GetActorLocation());
}

// ---------------- Horses (2026-11-22, Horse.h) ----------------
// The player's machine only asks (ServerHorse); the server checks, then mounts, calls or sets following, and RideHorse replicates back.
void AKnightCharacter::CallHorse()
{
	if (bIsWizard) { CallHorseDown = -1.0; ToggleBroom(); return; }   // (2026-11-22, owner: T calls the wizard's broom: KnightCharacterBroom.cpp)
	CallHorseDown = IsUIOpen() ? -1.0 : FPlatformTime::Seconds();
}

void AKnightCharacter::CallHorseUp()
{
	if (CallHorseDown < 0.0) return;
	const double Held = FPlatformTime::Seconds() - CallHorseDown;
	CallHorseDown = -1.0;
	if (IsUIOpen()) return;
	ServerHorse(Held >= 1.0 ? HO_Follow : HO_Call, nullptr);
}

void AKnightCharacter::Mount(AHorse* H)
{
	if (!H || bMounted || bSitting || bClimbing) return;
	ServerHorse(HO_Mount, H);
}

void AKnightCharacter::Dismount()
{
	if (bMounted) ServerHorse(HO_Dismount, nullptr);
}

void AKnightCharacter::ServerHorse_Implementation(uint8 Op, AHorse* H)
{
	if (Health <= 0.f || !Skills) return;
	AHorse* Mine = Rider ? Rider->MyHorse() : nullptr;
	if (bIsTroll && (Op == HO_Mount || Op == HO_Call || Op == HO_Follow))
	{
		Skills->Say(3.f, FColor(235, 205, 150), TEXT("Trolls are too big to ride, and run fast enough already."));
		return;
	}
	if (bIsWizard && Op == HO_Mount && Magic < JeebTune::V().BroomMinMagic)
	{
		Skills->Say(3.f, FColor(190, 140, 255), TEXT("You have too little magic to ride."));
		return;
	}
	switch (Op)
	{
	case HO_Mount:
	{
		if (!H || bMounted || bSitting || bClimbing || InClimbMove() || H->HasRider() || H->IsDown()) return;
		if (FVector::Dist2D(H->GetActorLocation(), GetActorLocation()) > JeebHorse::Reach + 150.f) return;   // (lag allowance)
		ServerSideMount(H);
		return;
	}
	case HO_Dismount: ServerSideDismount(); return;
	case HO_Call:
	case HO_Follow:
	{
		if (!Mine)
		{
			Skills->Say(4.f, FColor(235, 205, 150), Rider && Rider->bOwnsHorse ? TEXT("Your horse is on its way to its stable.")
				: TEXT("You have no horse of your own yet. Bram the stablemaster, east of the plaza, can help."));
			return;
		}
		if (Mine->IsDown() || Mine->RestLeft() > 0.f)
		{
			Skills->Say(4.f, FColor(255, 170, 130), FString::Printf(TEXT("Your horse is resting at its stable (%d s)."), FMath::Max(1, FMath::CeilToInt(Mine->RestLeft()))));
			return;
		}
		if (Mine->GetRider() == this) return;
		if (Op == HO_Call)
		{
			Mine->CallTo(this);
			Skills->Say(2.5f, FColor::Yellow, TEXT("You whistle. Your horse comes galloping."));
		}
		else
		{
			const bool bOn = !Mine->IsFollowing();
			if (bOn && Mine->HasRider()) Mine->CallTo(this);   // (someone else on it: they are put down first)
			Mine->SetFollow(this, bOn);
			Skills->Say(3.f, FColor::Yellow, bOn ? TEXT("Your horse will follow you.") : TEXT("Your horse stays put."));
		}
		UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s %s %s"), *GetName(), Op == HO_Call ? TEXT("called") : TEXT("toggled following of"), *Mine->GetName());
		return;
	}
	default: return;
	}
}

void AKnightCharacter::ServerSideMount(AHorse* H)
{
	if (!H || !HasAuthority() || bMounted || H->HasRider()) return;
	if (bSitting) StandUp();
	RideHorse = H;
	H->SetRider(this);
	ApplyMount(true);
	ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s mounted %s (%s)"), *GetName(), *H->GetName(), H->IsWild() ? TEXT("wild") : H->GetOwnerKnight() == this ? TEXT("own") : TEXT("someone else's"));
}

void AKnightCharacter::ServerSideDismount()
{
	if (!HasAuthority() || !RideHorse) return;
	AHorse* H = RideHorse;
	RideHorse = nullptr;
	H->SetRider(nullptr);
	// your own horse, left near a stable: that is the stable it waits at from now on
	if (Rider && H->GetOwnerKnight() == this)
		if (AStable* S = AStable::Nearest(GetWorld(), FVector2D(GetActorLocation()), 4000.f)) Rider->StableKey = S->GetKey();
	ApplyMount(false);
	ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("JEEBHORSE %s got off %s"), *GetName(), *H->GetName());
}

void AKnightCharacter::OnRep_RideHorse()
{
	if ((RideHorse != nullptr) != bMounted) ApplyMount(RideHorse != nullptr);
}

bool AKnightCharacter::OrientsToMovement() const { return GetCharacterMovement()->bOrientRotationToMovement; }

void AKnightCharacter::ApplyMount(bool bOn)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bOn)
	{
		if (bMounted) return;
		bMounted = true;
		bCrouched = false;
		bBlocking = false;
		bAttacking = false;
		HorseEnergy = 100.f;
		bHorseTired = false;
		Move->StopMovementImmediately();
		Move->MaxWalkSpeed = WalkNow() * 3.2f;   // (TickGroundSpeed sets the real riding speed from the next frame)
		// The on-foot facing to come back to. Strafe mode (on by default), the bow's draw or the shield stance have already turned
		// turning-to-movement off for themselves (bBackOrient, TickGroundSpeed), so the real on-foot value is the one they saved, not
		// the live one, and they let go of it here (the 2026-11-16 fix).
		bSavedOrientMount = bBackOrient ? bBackSavedOrient : Move->bOrientRotationToMovement;
		bBackOrient = false;
		Move->bOrientRotationToMovement = false;   // the horse faces where it steers, not where it is pushed
		if (AHorse* H = RideHorse) SetActorRotation(FRotator(0.f, H->GetActorRotation().Yaw, 0.f));
		return;
	}
	if (!bMounted) return;
	bMounted = false;
	Move->bOrientRotationToMovement = bSavedOrientMount;   // (strafe mode takes it over again next frame if it is on)
	bBackOrient = false;
	// Hop off to the right of the horse (the server's hop is the one that counts; the owner's machine does the same so it doesn't snap)
	if (HasAuthority() || IsLocallyControlled())
	{
		SetActorLocation(GetActorLocation() + GetActorRightVector() * 190.f + FVector(0.f, 0.f, 20.f));
		Move->SetMovementMode(MOVE_Falling);
	}
}

void AKnightCharacter::SitOnSeat(int32 SeatIdx)
{
	if (!JeebTerrain::Seats().IsValidIndex(SeatIdx)) return;   // (no benches at all since Blackrock went, 2026-11-22)
	const JeebTerrain::FSeat* Best = &JeebTerrain::Seats()[SeatIdx];

	UCharacterMovementComponent* Move = GetCharacterMovement();
	bCrouched = false;
	bBlocking = false;
	Move->StopMovementImmediately();
	Move->DisableMovement();
	const float Ground = JeebTerrain::HeightAt(Best->Pos.X, Best->Pos.Y);
	SeatForward = FRotator(0.f, Best->Yaw, 0.f).Vector();
	SetActorLocationAndRotation(FVector(Best->Pos.X, Best->Pos.Y, Ground + 79.f), FRotator(0.f, Best->Yaw, 0.f));
	bSitting = true;
}

void AKnightCharacter::StandUp()
{
	if (!bSitting) return;
	bSitting = false;
	// Step forward off the bench and drop back to the ground
	SetActorLocation(GetActorLocation() + SeatForward * 85.f + FVector(0.f, 0.f, 25.f));
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}

void AKnightCharacter::BeginGathering(AResourceNode* Node, ESkillType Skill)
{
	ActiveNode = Node;
	GatherStart = GetActorLocation();
	GatherKind = SelectedKind();
	GatherWalkLeft = 0.f;
	if (Skill != ESkillType::Fishing)   // (square on to it, so that the swing lands on it)
	{
		// Into place (owner 2026-11-06: the steel must meet the wood or stone, no chopping off to one side). The clip's blow frame says where the tool's head is from his feet, in his own frame;
		// he stands so that this point is on the near face of the trunk or rock. Worked out alike on the server and his own machine. His own machine walks him there (ordinary movement, which
		// the server checks); reach isn't checked until the walk is over (TickGatherWalk).
		FVector F = Node->GetActorLocation() - GetActorLocation();
		F.Z = 0.f;
		F = F.GetSafeNormal();
		const FVector R = FVector::CrossProduct(FVector::UpVector, F);
		const bool bChop = Skill != ESkillType::Mining;
		const int32 ToolK = bChop ? JeebItems::K_Axe : JeebItems::K_Pickaxe;
		const float FeetZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const JeebAnim::FClipLib& Lib = JeebAnim::FClipLib::Get();
		// (owner 2026-11-06: the steel stops at the rock, it doesn't go through. He stands as far off as the steel's furthest reach at any point of the blow's second half needs, on the node's real surface at that height.)
		float Off = 0.f;
		for (int32 i = 0; i <= 8; ++i)
		{
			const FVector At = Lib.ToolContact(bChop, ToolK, 0.5f + 0.5f * float(i) / 8.f);
			const float Surf = Node->SurfaceAlong(-F, FeetZ + float(At.Z) - float(Node->GetActorLocation().Z));
			Off = FMath::Max(Off, Surf + float(At.X));
			if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBREACH share %.2f tip forward %.1f up %.1f side %.1f, surface there %.1f"), 0.5f + 0.5f * float(i) / 8.f, At.X, At.Z, At.Y, Surf);
		}
		const FVector Head = Lib.ToolContact(bChop, ToolK);
		GatherSpot = Node->GetActorLocation() - F * Off - R * Head.Y;
		GatherSpot.Z = GetActorLocation().Z;
		GatherYaw = F.Rotation().Yaw;
		GatherWalkLeft = IsLocallyControlled() ? 1.f : 1.5f;   // (the server's runs longer: his own machine starts half a round trip later)
		SetActorRotation(FRotator(0.f, GatherYaw, 0.f));
	}
	// online: the server runs the gathering; the player's machine plays the animation
	if (HasAuthority() && GetNetMode() != NM_Standalone && !IsLocallyControlled()) ClientGather(Node->GetActorLocation(), static_cast<uint8>(Skill), true);
	if (Skill == ESkillType::Fishing)
	{
		// Face the water and cast: no swinging, just the pole held out
		const FVector To = Node->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.f, FVector(To.X, To.Y, 0.f).Rotation().Yaw, 0.f));
		bFishing = true;
		bSwinging = false;
		return;
	}
	bFishing = false;
	bSwinging = true;
	SwingTime = 0.f;

	if (Skill == ESkillType::Mining)
	{
		// Pickaxe: big overhead swings, two in a 2-second mine
		bSideways = false;
		SwingPeriod = 1.0f;
		WindupAngle = 150.f;
		StrikeAngle = 30.f;
	}
	else
	{
		// Axe: the arm is held out in front and sweeps sideways, right to left, about four chops in 5 seconds
		bSideways = true;
		SwingPeriod = 1.25f;
		WindupAngle = 85.f;    // arm pulled out to the right
		StrikeAngle = -40.f;   // arm swept across the front
	}
}

bool AKnightCharacter::WorkingOn(const AResourceNode* Node) const { return ActiveNode.Get() == Node; }

// The walk into place: at most a moment, facing the node all the way. Nothing here is a teleport: a blocked spot just means he works from as near as he got.
void AKnightCharacter::TickGatherWalk(float Dt)
{
	if (GatherWalkLeft <= 0.f)
	{
		if (bSwinging && ActiveNode.IsValid()) SetActorRotation(FRotator(0.f, GatherYaw, 0.f));   // (at work: looking round with the camera doesn't turn him off the tree)
		return;
	}
	if (!ActiveNode.IsValid()) { GatherWalkLeft = 0.f; return; }
	GatherWalkLeft -= Dt;
	SetActorRotation(FRotator(0.f, GatherYaw, 0.f));
	FVector To = GatherSpot - GetActorLocation();
	To.Z = 0.f;
	const float D = To.Size();
	if (IsLocallyControlled() && D > 3.f)
		AddMovementInput(To / D, FMath::Clamp(D / 70.f, 0.12f, 0.4f));   // (an easy walk, slowing into the spot, so he stops on it)
	if ((IsLocallyControlled() && D <= 3.f) || GatherWalkLeft <= 0.f) { if (IsLocallyControlled()) GatherWalkLeft = 0.f; }
	if (GatherWalkLeft <= 0.f) GatherStart = GetActorLocation();
}

void AKnightCharacter::EndGathering()
{
	if (ActiveNode.IsValid() && HasAuthority() && GetNetMode() != NM_Standalone && !IsLocallyControlled()) ClientGather(FVector::ZeroVector, 0, false);
	ActiveNode.Reset();
	bSwinging = false;
	bFishing = false;
}


// (server, every frame) A wizard on a horse spends magic (Tuning.ini [Horse] HorseMagicDrain, half the broom's); with none left he is put down.
// The bar replicates already (Magic), so every machine sees it fall. He can still cast; spells just draw from the same bar.
void AKnightCharacter::TickHorseMagic(float Dt)
{
	if (!HasAuthority() || !bIsWizard || !bMounted || !RideHorse || Health <= 0.f) return;
	Magic = FMath::Max(0.f, Magic - JeebTune::V().HorseMagicDrain * Dt);
	MagicUsedAt = GetWorld()->GetTimeSeconds();   // (no refilling while riding)
	if (Magic <= 0.f)
	{
		ServerSideDismount();
		if (Skills) Skills->Say(3.f, FColor(190, 140, 255), TEXT("Your magic is spent. You slide off the horse."));
	}
}
