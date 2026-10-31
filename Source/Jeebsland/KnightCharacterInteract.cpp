#include "KnightCharacter.h"
#include "Messages.h"
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
#include "Loot.h"
#include "Pond.h"
#include "HealthComponent.h"
#include "Projectile.h"
#include "Combat.h"
#include "WeaponDesign.h"
#include "ProceduralMeshComponent.h"
#include "Misc/ConfigCacheIni.h"

void AKnightCharacter::Interact()
{
	bAttackDown = true;   // (held: the sword's smash attack, see TickSmash)
	if (bKnockedDown) { GetUp(0); return; }   // (a click gets straight up)
	if (IsUIOpen() || bPanelCursor || bSitting || bRolling || bSliding || bClimbing || StunLeft > 0.f) return; // (nor in hitstun) clicks belong to the open screen (or the side panel); no attacks mid-move
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
		if (K == JeebItems::K_Spear)
		{
			bCharging = true;       // hold to charge, let go to thrust
			ChargeT = 0.f;
		}
		else if (K == JeebItems::K_Bow)
		{
			// (online, combat is not on the server yet: a player's own machine shoots without using up the arrow)
			const int32 Tier = !Skills ? -1 : HasAuthority() ? Skills->TakeBestArrow() : Skills->BestArrowTier();
			if (Tier < 0)
			{
				if (GEngine) JeebMsg::Post(2.f, FColor::Orange, TEXT("You have no arrows."));
				return;
			}
			PendingArrowTier = Tier;
			StartAttack(EAtkStyle::Cast, 1, 0.f);
		}
		else if (K == JeebItems::K_Wand || K == JeebItems::K_Staff)
		{
			StartAttack(EAtkStyle::Cast, 1, 0.f);
		}
		else if (K == JeebItems::K_Sword)
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
	// The sword's smash attack: let go before it starts charging and it was just hit 1; let go while charging and it swings
	if (Smash == ESmash::Deciding) { SetSmash(ESmash::None); return; }
	if (Smash == ESmash::Charging) { SetSmash(ESmash::Swing, SmashCharge); return; }
	if (!bCharging) return;
	bCharging = false;
	if (IsUIOpen() || bAttacking || bSitting || bRolling || bSliding || bClimbing) return;
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
		if (It->HasRider()) continue;
		if (FVector2D::Distance(Me, FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y)) < 200.f) { OutHorse = *It; return EInteract::Horse; }
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
	if (IsUIOpen() || bRolling || bSliding || bClimbing) return false;
	if (bSitting || bMounted) return false;   // E to stand up or dismount still works; it just shows no prompt
	if (GetCharacterMovement()->IsFalling()) return false;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: OutText = TEXT("Press E to mount the horse"); return true;
	case EInteract::Npc:
		OutText = FString::Printf(TEXT("Press E to talk to %s"), *Npc->GetNpcName());
		return true;
	case EInteract::Bench: OutText = TEXT("Press E to sit"); return true;
		case EInteract::Loot:  OutText = TEXT("Press E to take the ingots"); return true;
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
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: Float(Horse->GetActorLocation(), 150.f); return true;
	case EInteract::Npc:   Float(Npc->GetActorLocation(), 130.f); return true;
	case EInteract::Bench:
	{
		const FVector2D P = JeebTerrain::Seats()[Idx].Pos;
		Float(FVector(P.X, P.Y, JeebTerrain::HeightAt(P.X, P.Y)), 90.f);
		return true;
	}
	case EInteract::Loot:
		if (const ACottage* Hut = FoundCottage.Get()) { Float(Hut->LootPos(), 70.f); return true; }
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
	if (IsUIOpen() || bRolling || bSliding || bClimbing || GetCharacterMovement()->IsFalling()) return;
	int32 Idx = -1;
	ANpc* Npc = nullptr;
	AHorse* Horse = nullptr;
	switch (FindInteractable(Idx, Npc, Horse))
	{
	case EInteract::Horse: Mount(Horse); break;
	case EInteract::Npc:   StartDialogue(Npc); break;
	case EInteract::Bench: SitOnSeat(Idx); break;
		case EInteract::Door:  UseDoor(FoundCottage.Get()); break;
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
	default: break;
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

void AKnightCharacter::CallHorse()
{
	if (IsUIOpen() || bMounted) return;
	AHorse* Best = nullptr;
	for (TActorIterator<AHorse> It(GetWorld()); It; ++It) { if (!It->HasRider()) { Best = *It; break; } }
	if (!Best) return;
	Best->CallTo(this);
	if (GEngine) JeebMsg::Post(2.5f, FColor::Yellow, TEXT("You whistle. Your horse comes galloping."));
}

void AKnightCharacter::Mount(AHorse* H)
{
	if (!H || bMounted || bSitting || bClimbing) return;
	bMounted = true;
	MyHorse = H;
	H->SetRider(this);
	bCrouched = false;
	bBlocking = false;
	bAttacking = false;
	HorseEnergy = 100.f;
	bHorseTired = false;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed * 2.f;
	bSavedOrientMount = GetCharacterMovement()->bOrientRotationToMovement;
	GetCharacterMovement()->bOrientRotationToMovement = false;   // the horse faces where it steers, not where it is pushed
}

void AKnightCharacter::Dismount()
{
	if (!bMounted) return;
	bMounted = false;
	GetCharacterMovement()->bOrientRotationToMovement = bSavedOrientMount;
	if (AHorse* H = MyHorse.Get()) H->SetRider(nullptr);
	// Hop off to the right of the horse
	SetActorLocation(GetActorLocation() + GetActorRightVector() * 190.f + FVector(0.f, 0.f, 20.f));
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}

void AKnightCharacter::SitOnSeat(int32 SeatIdx)
{
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

void AKnightCharacter::EndGathering()
{
	if (ActiveNode.IsValid() && HasAuthority() && GetNetMode() != NM_Standalone && !IsLocallyControlled()) ClientGather(FVector::ZeroVector, 0, false);
	ActiveNode.Reset();
	bSwinging = false;
	bFishing = false;
}

