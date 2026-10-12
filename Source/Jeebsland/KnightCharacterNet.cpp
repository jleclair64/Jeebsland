#include "KnightCharacter.h"
#include "Messages.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Store.h"
#include "Loot.h"
#include "Npc.h"
#include "Cottage.h"
#include "ResourceNode.h"
#include "KnightHUD.h"
#include "KnightGameState.h"
#include "KnightGameMode.h"
#include "KnightPlayerController.h"
#include "JeebSave.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"

using namespace JeebItems;

// ---------------------------------------------------------------------------------------------------
// Requests: everything that changes a player's pack, gear or money goes through here.
// Online the player's machine only asks (ServerInv); the server checks the request against ITS copy of the world (is the
// player beside the banker, do they have the ingots, is that really the item in that slot?) and only then does it. The result
// reaches the player by replication (USkillComponent). In single player, or for the host of a listen server, it runs at once.
// ---------------------------------------------------------------------------------------------------
void AKnightCharacter::Inv(EInvOp Op, int32 A, int32 B, int32 C, AActor* Target, FVector At)
{
	if (HasAuthority()) DoInv(Op, A, B, C, Target, At);
	else ServerInv(Op, A, B, C, Target, At);
}

void AKnightCharacter::ServerInv_Implementation(EInvOp Op, int32 A, int32 B, int32 C, AActor* Target, FVector_NetQuantize At)
{
	DoInv(Op, A, B, C, Target, At);
}

void AKnightCharacter::SaveNow()
{
	if (!HasAuthority()) { ServerSaveNow(); return; }
	if (GetNetMode() == NM_Standalone) { if (Skills) UJeebSave::Save(Skills); return; }   // single player: the save on this PC
	ServerSaveNow_Implementation();
}

void AKnightCharacter::ServerSaveNow_Implementation()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastSaveAsk < 5.0) return;   // (it saves every minute anyway)
	LastSaveAsk = Now;
	if (AKnightGameMode* GM = GetWorld()->GetAuthGameMode<AKnightGameMode>()) GM->SaveCharacter(Cast<AKnightPlayerController>(GetController()));
}

bool AKnightCharacter::NearTrader(uint8 InRole) const
{
	for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
	{
		if (static_cast<uint8>(It->GetRole()) == InRole && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 800.f) return true;
	}
	return false;
}

void AKnightCharacter::ShopNote(const FString& Text, float Seconds)
{
	if (IsLocallyControlled())
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (AKnightHUD* H = PC ? Cast<AKnightHUD>(PC->GetHUD()) : nullptr) H->SetShopMsg(Text, Seconds);
	}
	else if (GetNetMode() != NM_Standalone) ClientShopNote(Text, Seconds);
}

void AKnightCharacter::ClientShopNote_Implementation(const FString& Text, float Seconds)
{
	ShopNote(Text, Seconds);
}

// The server started or stopped our gathering: play (or stop) the working animation here
void AKnightCharacter::ClientGather_Implementation(FVector_NetQuantize At, uint8 Skill, bool bStart)
{
	if (!bStart) { EndGathering(); return; }
	for (TActorIterator<AResourceNode> It(GetWorld()); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), At) < 5.f) { BeginGathering(*It, static_cast<ESkillType>(Skill)); return; }
	}
}

void AKnightCharacter::ClientDrank_Implementation(uint8 Type)
{
	ApplyPotion(Type);
}

void AKnightCharacter::ClientUnlockHut_Implementation(FVector_NetQuantize At)
{
	for (TActorIterator<ACottage> It(GetWorld()); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), At) < 5.f) { if (It->IsLocked()) It->Unlock(); return; }
	}
}

void AKnightCharacter::ApplyPotion(int32 T)
{
	if (T < 0 || T >= NumPotions) return;
	const float Now = GetWorld()->GetTimeSeconds();
	switch (T)
	{
	case P_Speed:  SpeedUntil = Now + 8.f; break;
	case P_Heal:   Health = (float)MaxHealth; break;
	case P_Vigour: VigourUntil = Now + 60.f; break;   // the bigger bar is worked out in Tick (the extra comes full)
	}
	PotionReadyAt[T] = Now + PotionCooldown(T);
}

void AKnightCharacter::DoInv(EInvOp Op, int32 A, int32 B, int32 C, AActor* Target, const FVector& At)
{
	if (!Skills || !HasAuthority()) return;
	USkillComponent* Sk = Skills;
	bool bGear = false;   // the worn gear or the weapon in hand may have changed: redraw the knight
	const FColor Warn = FColor::Orange;

	auto FindCottage = [&]() -> ACottage*
	{
		for (TActorIterator<ACottage> It(GetWorld()); It; ++It)
			if (FVector::Dist2D(It->GetActorLocation(), At) < 5.f && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 900.f) return *It;
		return nullptr;
	};
	auto Named = [](int32 Id, int32 Count) { return Count > 1 ? FString::Printf(TEXT("%d %s"), Count, *ItemName(Id)) : ItemName(Id); };

	switch (Op)
	{
	// ---- the pack, the weapon bar and worn gear ----
	case EInvOp::Equip:         bGear = Sk->EquipFromSlot(A); break;
	case EInvOp::UnequipHot:    if (Sk->UnequipHotbar(A)) bGear = true; else Sk->Say(2.f, Warn, TEXT("No room in your inventory.")); break;
	case EInvOp::UnequipWorn:   if (Sk->UnequipWorn(A, B != 0)) bGear = true; else if (!IsArmorKind(A)) Sk->Say(2.f, Warn, TEXT("No room in your inventory.")); break;
	case EInvOp::SwapInvHot:    bGear = Sk->SwapInvHotbar(A, B); break;
	case EInvOp::SwapInv:       Sk->SwapInv(A, B); break;
	case EInvOp::SwapHot:       bGear = Sk->SwapHotbar(A, B); break;
	case EInvOp::SwapInvWorn:   bGear = Sk->SwapInvWorn(A, B, C != 0); break;
	case EInvOp::SwapInvPotion: Sk->SwapInvPotion(A); break;
	case EInvOp::Select:
		if (A >= 0 && A < 5 && A != Sk->Selected) { Sk->SetSelected(A); bAttacking = false; bCharging = false; bGear = true; }
		break;

	// ---- dropping things on the ground ----
	case EInvOp::DropPack:
	{
		if (!Sk->Slots.IsValidIndex(A)) break;
		FInvSlot& S = Sk->Slots[A];
		if (S.Id == 0 || S.Count <= 0) break;
		const int32 N = FMath::Clamp(B, 1, S.Count);
		TArray<FInvSlot> Items;
		Items.Add(FInvSlot(S.Id, N));
		S.Count -= N;
		if (S.Count <= 0) { S.Id = 0; S.Count = 0; }
		DropBag(Items, 0);
		break;
	}
	case EInvOp::DropIngots:
	{
		const int32 N = FMath::Clamp(A, 0, Sk->GetIngots());
		if (N > 0 && Sk->SpendIngots(N)) DropBag(TArray<FInvSlot>(), N);
		break;
	}
	case EInvOp::DropHot:
	{
		if (A < 0 || A >= 5 || Sk->Hotbar[A] == 0) break;
		TArray<FInvSlot> Items;
		Items.Add(FInvSlot(Sk->Hotbar[A], 1));
		Sk->Hotbar[A] = 0;
		DropBag(Items, 0);
		bGear = true;
		break;
	}

	// ---- the bank (beside the banker) ----
	case EInvOp::BankDeposit:
		if (!NearTrader(static_cast<uint8>(ENpcRole::Banker))) break;
		if (A < 0) { for (int32 i = 0; i < Sk->Slots.Num(); ++i) Sk->BankDeposit(i); }
		else Sk->BankDeposit(A);
		break;
	case EInvOp::BankWithdraw:
		if (NearTrader(static_cast<uint8>(ENpcRole::Banker))) Sk->BankWithdraw(A);
		break;

	// ---- the trading post (beside the shopkeeper) ----
	case EInvOp::SellAll:
	{
		if (!NearTrader(static_cast<uint8>(ENpcRole::Shopkeeper)) || A < 0 || A > static_cast<int32>(EItemType::Bones)) break;
		const int32 Pay = Sk->SellAll(static_cast<EItemType>(A));
		ShopNote(FString::Printf(TEXT("Sold for %d ingots."), Pay));
		break;
	}
	case EInvOp::SellSlot:
	{
		if (!NearTrader(static_cast<uint8>(ENpcRole::Shopkeeper)) || !Sk->Slots.IsValidIndex(A)) break;
		FInvSlot& S = Sk->Slots[A];
		if (S.Id == 0 || S.Id != B || !(IsGear(S.Id) || IsArrow(S.Id))) break;   // (the slot changed since the player clicked)
		const int32 Pay = IsArrow(S.Id) ? S.Count : GearSellPrice(S.Id) * S.Count;
		Sk->AddIngots(Pay);
		ShopNote(FString::Printf(TEXT("Sold %s for %d ingots."), *ItemName(S.Id), Pay));
		S.Id = 0; S.Count = 0;
		break;
	}
	case EInvOp::Buy:
	{
		if (!NearTrader(static_cast<uint8>(ENpcRole::Shopkeeper))) break;
		TArray<JeebStore::FWare>& Wares = JeebStore::Stock();
		if (!Wares.IsValidIndex(A) || Wares[A].Id != B) { ShopNote(TEXT("The stock has just changed. Have another look.")); break; }
		JeebStore::FWare& W = Wares[A];
		if (W.Stock <= 0) ShopNote(TEXT("Sold out."));
		else if (Sk->GetIngots() < W.Price) ShopNote(TEXT("Not enough ingots."));
		else if (!Sk->CanAdd(W.Id, W.Count)) ShopNote(TEXT("Your pack is full."));
		else
		{
			Sk->SpendIngots(W.Price);
			Sk->AddInvItem(W.Id, W.Count);
			--W.Stock;
			ShopNote(FString::Printf(TEXT("Bought %s. It is in your pack (Tab)."), *Named(W.Id, W.Count)));
			if (AKnightGameState* GS = GetWorld()->GetGameState<AKnightGameState>()) GS->SyncStore();
		}
		break;
	}
	case EInvOp::Gamble:
	{
		if (!NearTrader(static_cast<uint8>(ENpcRole::Shopkeeper))) break;
		const int64 Hour = JeebStore::HourNow();
		if (Sk->LastGambleHour == Hour || Sk->GetIngots() < JeebStore::GamblePrice) break;
		FRandomStream R(static_cast<int32>(FPlatformTime::Cycles()));
		int32 Id = 0, Count = 0;
		const int32 Row = JeebStore::RollGamble(R, Id, Count);
		if (Id != 0 && !Sk->CanAdd(Id, Count)) { ShopNote(TEXT("Make room in your pack first."), 6.f); break; }
		Sk->SpendIngots(JeebStore::GamblePrice);
		Sk->LastGambleHour = Hour;
		if (Id == 0) Sk->AddIngots(Count);
		else Sk->AddInvItem(Id, Count);
		const FString Got = Id == 0 ? FString::Printf(TEXT("%d ingots"), Count) : Named(Id, Count);
		ShopNote(FString::Printf(TEXT("The satchel holds... %s!  (a %.1f%% chance)"), *Got, JeebStore::GambleTable()[Row].Percent), 6.f);
		break;
	}

	// ---- the smithy (beside the blacksmith): A kind (-1 arrows), B tier ----
	case EInvOp::Smith:
	{
		if (!NearTrader(static_cast<uint8>(ENpcRole::Blacksmith)) || B < 0 || B >= NumTiers) break;
		const bool bArrows = A < 0;
		if (!bArrows && !(IsWeaponKind(A) || IsArmorKind(A) || A == K_Shield)) break;
		const FRecipe R = SmithRecipe(bArrows ? K_Bow : A, B, bArrows);
		const int32 Id = bArrows ? ArrowId(B) : GearId(A, B);
		const int32 Count = bArrows ? ArrowBundle : 1;
		if (Sk->GetLevel(ESkillType::Smithing) < R.Level) { ShopNote(FString::Printf(TEXT("You need Smithing level %d."), R.Level)); break; }
		if (Sk->CountWood() < R.Logs || Sk->GetItemCount(EItemType::CopperOre) < R.Copper || Sk->GetItemCount(EItemType::Coal) < R.Coal
			|| Sk->GetItemCount(EItemType::Diamond) < R.Diamonds) { ShopNote(TEXT("You don't have the materials.")); break; }
		if (!Sk->CanAdd(Id, Count)) { ShopNote(TEXT("Your pack is full.")); break; }
		if (R.Logs > 0) Sk->TakeWood(R.Logs);
		if (R.Copper > 0) Sk->RemoveItems(MaterialId(EItemType::CopperOre), R.Copper);
		if (R.Coal > 0) Sk->RemoveItems(MaterialId(EItemType::Coal), R.Coal);
		if (R.Diamonds > 0) Sk->RemoveItems(MaterialId(EItemType::Diamond), R.Diamonds);
		Sk->AddInvItem(Id, Count);
		Sk->AddXP(ESkillType::Smithing, R.XP);
		ShopNote(FString::Printf(TEXT("Smithed %s. It is in your pack (Tab)."), bArrows ? *FString::Printf(TEXT("%d %s arrows"), ArrowBundle, TierName(B)) : *ItemName(Id)));
		break;
	}

	// ---- potions: the one in the potion slot, if that kind is ready ----
	case EInvOp::Drink:
	{
		FInvSlot& P = Sk->PotionSlot;
		if (P.Id == 0 || !IsPotion(P.Id) || P.Count <= 0) { Sk->Say(2.f, Warn, TEXT("No potion in the potion slot.")); break; }
		const int32 T = PotionType(P.Id);
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now < PotionReadyAt[T])
		{
			Sk->Say(2.f, Warn, FString::Printf(TEXT("%s is not ready yet (%d s)."), PotionName(T), FMath::CeilToInt(PotionReadyAt[T] - Now)));
			break;
		}
		ApplyPotion(T);
		if (!IsLocallyControlled() && GetNetMode() != NM_Standalone) ClientDrank(static_cast<uint8>(T));   // the player's machine runs the effect too (speed, the bigger bar)
		if (--P.Count <= 0) P = FInvSlot();
		Sk->Say(2.5f, FColor(170, 255, 170), FString::Printf(TEXT("You drink a %s."), PotionName(T)));
		break;
	}

	// ---- drops and sacks on the ground (within reach) ----
	case EInvOp::LootTakeAll:
	case EInvOp::LootTakeOne:
	case EInvOp::LootEquip:
	{
		ALootDrop* Drop = Cast<ALootDrop>(Target);
		if (!Drop || Drop->IsActorBeingDestroyed() || FVector::Dist2D(Drop->GetActorLocation(), GetActorLocation()) > 500.f) break;
		if (Op == EInvOp::LootTakeAll) Drop->TakeAll(this);
		else if (Op == EInvOp::LootTakeOne) { if (A < 0) Drop->TakeIngots(this); else Drop->TakeOne(A, this); }
		else bGear = Drop->EquipTo(A, this, B, C % 100, C >= 100);
		break;
	}

	// ---- cottages: a stash of ingots, and picking a lock ----
	case EInvOp::HutLoot:
		if (ACottage* Hut = FindCottage())
		{
			const int32 Got = Hut->TakeLoot();
			if (Got > 0) { Sk->AddIngots(Got); Sk->Say(3.f, FColor::Yellow, FString::Printf(TEXT("You take %d ingots."), Got)); }
		}
		break;
	case EInvOp::PickLock:
	{
		ACottage* Hut = FindCottage();
		if (!Hut || !Hut->IsLocked()) break;
		const float Now = GetWorld()->GetTimeSeconds();
		const int32 Need = Hut->GetLockLevel();
		const int32 Lv = Sk->GetLevel(ESkillType::Lockpicking);
		if (Lv < Need) { Sk->Say(3.f, Warn, FString::Printf(TEXT("The lock is too fine for you. Lockpicking level %d needed."), Need)); break; }
		if (Now < PickCooldownUntil) break;
		PickCooldownUntil = Now + 1.0f;
		// Better than the lock's level: easier. Always at least a one-in-three chance.
		const float Chance = FMath::Clamp(0.35f + 0.06f * (Lv - Need), 0.35f, 0.95f);
		if (FMath::FRand() < Chance)
		{
			Hut->Unlock();
			if (!IsLocallyControlled() && GetNetMode() != NM_Standalone) ClientUnlockHut(Hut->GetActorLocation());
			Sk->AddXP(ESkillType::Lockpicking, 15 + 6 * Need);
			Sk->Say(3.f, FColor::Green, TEXT("Click. The lock gives way."));
		}
		else
		{
			Sk->AddXP(ESkillType::Lockpicking, 3);
			Sk->Say(2.f, Warn, TEXT("The pick slips. Try again."));
		}
		break;
	}

	// ---- gathering: the server runs it; the player's machine is told to play the animation (ClientGather) ----
	case EInvOp::Gather:
		if (ActiveNode.IsValid()) break;
		for (TActorIterator<AResourceNode> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), At) >= 5.f) continue;
			if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= It->ReachRadius() + 150.f) It->StartGathering(this);
			break;
		}
		break;

	// ---- a fragment of lore, heard from a villager ----
	case EInvOp::Lore:
		for (TActorIterator<ANpc> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 900.f) { Sk->DiscoverLore(A); break; }
		}
		break;
	}

	if (bGear) RefreshEquipment();
}

// ---------------------------------------------------------------------------------------------------
// Combat and moves online. The player's own machine plays everything at once (so it feels instant); the server is told and
// does the same on its copy, where the blows actually land; everyone else is shown the swing (MulticastAttack) and the
// stance (RepMoveFlags).
// ---------------------------------------------------------------------------------------------------
void AKnightCharacter::ServerAttack_Implementation(uint8 Style, int8 Dir, float Charge)
{
	if (!Skills || Style > static_cast<uint8>(EAtkStyle::Cast) || Health <= 0.f) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastServerAttack < 0.25) return;   // no weapon swings faster than this
	LastServerAttack = Now;
	const EAtkStyle S = static_cast<EAtkStyle>(Style);
	if (S == EAtkStyle::Cast && SelectedKind() == K_Bow)
	{
		const int32 Tier = Skills->TakeBestArrow();
		if (Tier < 0) { Skills->Say(2.f, FColor::Orange, TEXT("You have no arrows.")); return; }
		PendingArrowTier = Tier;
	}
	const float C = FMath::Clamp(Charge, 0.f, 1.f);
	StartAttack(S, Dir < 0 ? -1 : 1, C);
	if (S == EAtkStyle::Thrust && !GetCharacterMovement()->IsFalling())
		LaunchCharacter(GetActorForwardVector().GetSafeNormal2D() * (200.f + 700.f * C), true, false);   // the spear lunge, as on the player's machine
}

void AKnightCharacter::MulticastAttack_Implementation(uint8 Style, int8 Dir, float Charge)
{
	if (HasAuthority() || IsLocallyControlled()) return;   // (the server and the swinging player already swing)
	if (Style > static_cast<uint8>(EAtkStyle::Cast)) return;
	StartAttack(static_cast<EAtkStyle>(Style), Dir < 0 ? -1 : 1, Charge);
}

void AKnightCharacter::SendState()
{
	if (HasAuthority() || !IsLocallyControlled()) return;
	const uint8 F = (bSprinting ? 1 : 0) | (bCrouched ? 2 : 0) | (bBlockHeld ? 4 : 0);
	if (F == SentState) return;
	SentState = F;
	ServerState(F);
}

void AKnightCharacter::ServerState_Implementation(uint8 Flags)
{
	bSprinting = (Flags & 1) != 0;
	if (bSprinting) bCrouched = false;
	const bool bCrouch = (Flags & 2) != 0;
	if (bCrouch != bCrouched) { if (bCrouch) StartCrouch(); else StopCrouch(); }
	const bool bHold = (Flags & 4) != 0;
	if (bHold != bBlockHeld) { if (bHold) StartBlock(); else StopBlock(); }
}

void AKnightCharacter::ServerRoll_Implementation(FVector_NetQuantizeNormal Dir)
{
	if (bRolling) return;
	PendingRollDir = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
	RollCooldown = 0.f;                         // (the player's machine already checked; small timing differences must not refuse it)
	Stamina = FMath::Max(Stamina, 20.f);
	StartRoll();
	PendingRollDir = FVector::ZeroVector;
}

void AKnightCharacter::ServerSlide_Implementation(FVector_NetQuantizeNormal Dir, float Speed)
{
	if (bSliding || bRolling || bMounted || bClimbing || bSitting) return;
	BeginSlide(Dir, Speed);
}

void AKnightCharacter::ServerClimb_Implementation(bool bOn)
{
	// On the ladder the knight is moved by the player's machine; the server follows it instead of pulling it back
	UCharacterMovementComponent* Move = GetCharacterMovement();
	bClimbing = bOn;
	if (bOn)
	{
		GetWorldTimerManager().ClearTimer(ClimbTrustTimer);
		Move->bIgnoreClientMovementErrorChecksAndCorrection = true;
		Move->bServerAcceptClientAuthoritativePosition = true;
	}
	else
	{
		// Keep trusting it until the knight has stood on solid ground for half a second (at most 10 s): stepping off the top,
		// walking about up there and jumping down are all still the player's machine's word
		TWeakObjectPtr<AKnightCharacter> Self(this);
		const double Since = FPlatformTime::Seconds();
		ClimbGroundedFor = 0.f;
		GetWorldTimerManager().SetTimer(ClimbTrustTimer, FTimerDelegate::CreateLambda([Self, Since]()
		{
			if (!Self.IsValid() || Self->bClimbing) return;
			UCharacterMovementComponent* M = Self->GetCharacterMovement();
			Self->ClimbGroundedFor = M->IsMovingOnGround() ? Self->ClimbGroundedFor + 0.25f : 0.f;
			if (Self->ClimbGroundedFor < 0.5f && FPlatformTime::Seconds() - Since < 10.0) return;
			M->bIgnoreClientMovementErrorChecksAndCorrection = false;
			M->bServerAcceptClientAuthoritativePosition = false;
			Self->GetWorldTimerManager().ClearTimer(Self->ClimbTrustTimer);
		}), 0.25f, true);
	}
	if (bOn) { bCrouched = false; bBlocking = false; bAttacking = false; }
}

void AKnightCharacter::OnRep_MoveFlags()
{
	if (HasAuthority() || IsLocallyControlled()) return;
	bClimbing = (RepMoveFlags & 16) != 0;
	// another player's knight: copy the stance so it is drawn the same way
	bCrouched = (RepMoveFlags & 1) != 0;
	bBlocking = (RepMoveFlags & 2) != 0;
	const bool bRoll = (RepMoveFlags & 4) != 0, bSlide = (RepMoveFlags & 8) != 0;
	if (bRoll && !bRolling) { bRolling = true; RollT = 0.f; RollDir = GetActorForwardVector().GetSafeNormal2D(); }
	if (bSlide && !bSliding) { bSliding = true; SlideT = 0.f; SlideSpeed = 800.f; SlideDir = GetActorForwardVector().GetSafeNormal2D(); }
	else if (!bSlide) bSliding = false;
}

// The server says we fell: wake up in Blackrock here too
void AKnightCharacter::ClientDied_Implementation(FVector_NetQuantize Wake, float Yaw)
{
	if (bMounted) Dismount();
	if (bClimbing) ExitClimb(false, false);
	if (bSitting) StandUp();
	EndGathering();
	CloseLoot();
	bAttacking = false; bCharging = false; bBlocking = false; bBlockHeld = false; bRolling = false; bSliding = false;
	SpeedUntil = -1.f; VigourUntil = -1.f;
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	SetActorLocation(Wake);
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	if (AController* C = GetController()) C->SetControlRotation(FRotator(0.f, Yaw, 0.f));
	JeebMsg::Post(5.f, FColor::Red, TEXT("You fell. Everything you had is in a sack where you fell. You wake up in Blackrock."));
}
