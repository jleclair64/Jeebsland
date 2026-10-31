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
#include "FoeKnight.h"
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

bool AKnightCharacter::ShieldActive() const
{
	if (!Skills || Skills->Worn[JeebItems::K_Shield] == 0) return false;
	const int32 K = SelectedKind();
	return K < 0 || JeebItems::AllowsShield(K);
}

int32 AKnightCharacter::SelectedKind() const
{
	if (!Skills) return -1;
	const int32 Id = Skills->SelectedItem();
	return JeebItems::IsGear(Id) ? JeebItems::KindOf(Id) : -1;
}

float AKnightCharacter::GetStaminaMult() const
{
	return JeebItems::StaminaMult(Skills);
}

void AKnightCharacter::StartAttack(EAtkStyle Style, int32 Dir, float Charge)
{
	const int32 K = SelectedKind();
	const float Cd = (K >= 0) ? JeebItems::WeaponBase(K).Cooldown : 0.5f;
	AtkStyle = Style;
	AttackDir = Dir;
	AtkCharge = Charge;
	AttackDur = (Style == EAtkStyle::Thrust) ? 0.45f : (Style == EAtkStyle::Cast ? FMath::Max(0.45f, Cd * 1.1f) : Cd * 1.25f);
	if (Dir < 0) AttackDur *= 0.85f;           // the follow-up swing is 15% faster
	if (Style == EAtkStyle::Slash && K == JeebItems::K_Sword)
	{
		// The sword's jab: two quick swings that alternate, each with a small step forward
		AttackDur = Cd * (Dir == JabSecond ? 0.72f : 0.8f);
		SwingFrom[0] = ArmPitch; SwingFrom[1] = ArmYaw; SwingFrom[2] = ArmRollR; SwingFrom[3] = ElbowRCur; SwingFrom[4] = ForearmNow; SwingFrom[5] = SwingTwistNow; SwingFrom[6] = BladeYawNow; SwingFrom[7] = BladePitchNow;   // (the key poses start from here)
		if ((IsLocallyControlled() || HasAuthority()) && !GetCharacterMovement()->IsFalling())
			LaunchCharacter(GetActorForwardVector().GetSafeNormal2D() * 210.f, true, false);   // (the player's machine and the server step together)
	}
	bAttacking = true;
	bBlocking = false;   // the shield drops for the swing; Tick raises it again afterwards if the block button is still held
	bAttackHit = false;
	AttackT = 0.f;
	QueuedClick = -10.f;
	QueuedJabs = 0;   // (a new swing starts with an empty buffer: presses made before it began are used up)
	if (bShotFace) SetActorRotation(FRotator(0.f, ShotFaceYaw, 0.f));   // (test pictures: side-on to the camera)
	// Face where you aim: always for shots (bow, wand, staff fly where the camera points); a melee swing or thrust only in
	// strafe mode, otherwise it goes the way the knight already faces (owner, 2026-10-31)
	else if (AController* C = GetController())
		if (Style == EAtkStyle::Cast || bStrafeMode) SetActorRotation(FRotator(0.f, C->GetControlRotation().Yaw, 0.f));
	AttackYaw = GetActorRotation().Yaw;   // (a sword hit keeps this heading: TickAttack)
	// Online: our swing goes to the server (which lands it); the server shows it to everyone else
	if (GetNetMode() != NM_Standalone)
	{
		if (IsLocallyControlled() && !HasAuthority()) ServerAttack(static_cast<uint8>(Style), static_cast<int8>(Dir), Charge);
		else if (HasAuthority()) MulticastAttack(static_cast<uint8>(Style), static_cast<int8>(Dir), Charge);
	}
}

void AKnightCharacter::ShotPlayMove(const FString& Move, float FaceYaw)
{
	if (!Skills) return;
	bShotFace = true;
	ShotFaceYaw = FaceYaw;
	SetActorRotation(FRotator(0.f, FaceYaw, 0.f));
	for (int32 h = 0; h < 5; ++h)   // a sword in hand
		if (JeebItems::IsGear(Skills->Hotbar[h]) && JeebItems::KindOf(Skills->Hotbar[h]) == JeebItems::K_Sword) { SelectHotbar(h); break; }
	if (Move == TEXT("jab")) { StartAttack(EAtkStyle::Slash, JabFirst, 0.f); QueuedJabs = 1; }   // two hits (the buffer holds one press)
	if (Move == TEXT("jab1")) StartAttack(EAtkStyle::Slash, JabFirst, 0.f);                     // hit 1 alone
	if (Move == TEXT("jab2")) { StartAttack(EAtkStyle::Slash, JabFirst, 0.f); QueuedJabs = 1; }   // hits 1 and 2
}

// Test pictures: where the sword's tip is and which way its cutting edge faces, frame by frame, in the knight's own frame
// (forward, right, up; cm from his centre), from the real joints and the real weapon placement (RebuildHeld)
void AKnightCharacter::LogSwordTip()
{
	if (!WristR) return;
	const FVector Blade = KnightDesign::BladeDir(1);
	const FVector TipCapsule = KnightDesign::GripPos(false) + Blade * 79.5f;    // (WeaponDesign: the sword's tip, 79.5 cm out)
	const FVector EdgeCapsule = KM::RotateEuler(FVector(0.0, 1.0, 0.0), KM::DownTo(Blade));   // (the guard and the edges)
	const FTransform W = WristR->GetComponentTransform();
	const FTransform A = GetActorTransform();
	const FVector Tip = A.InverseTransformPosition(W.TransformPosition(TipCapsule - KnightDesign::PivotPos(KnightDesign::P_WristR)));
	const FVector Edge = A.InverseTransformVector(W.TransformVector(EdgeCapsule)).GetSafeNormal();
	const FVector Fa = A.InverseTransformVector(ElbowR->GetComponentTransform().TransformVector(KnightDesign::WristPos(1) - KnightDesign::PivotPos(KnightDesign::P_ElbowR))).GetSafeNormal();
	const FVector Bl = A.InverseTransformVector(W.TransformVector(Blade)).GetSafeNormal();
	UE_LOG(LogTemp, Log, TEXT("JEEBTIP %d %.3f tip %.0f %.0f %.0f edge %.2f %.2f %.2f | wrist bend %.0f twist %.0f | forearm heading %.0f tilt %.0f | blade heading %.0f tilt %.0f"), AttackDir, AttackT / FMath::Max(AttackDur, 0.01f), Tip.X, Tip.Y, Tip.Z, Edge.X, Edge.Y, Edge.Z, WristBendNow, ForearmTwistNow,
		FMath::RadiansToDegrees(FMath::Atan2(Fa.Y, Fa.X)), FMath::RadiansToDegrees(FMath::Asin(Fa.Z)), FMath::RadiansToDegrees(FMath::Atan2(Bl.Y, Bl.X)), FMath::RadiansToDegrees(FMath::Asin(Bl.Z)));
}

void AKnightCharacter::DealDamage(UHealthComponent* Target, int32 Dmg, bool bCrit, ESkillType Skill)
{
	if (!Target || !Target->IsAlive()) return;
	const int32 Dealt = Target->ApplyDamage(Dmg, this, bCrit);
	if (Dealt > 0 && Skills) Skills->AddXP(Skill, 3 + Dealt / 3);
}

void AKnightCharacter::AttackLands()
{
	if (!Skills || !HasAuthority()) return;   // (the server lands every blow and fires every arrow)
	const int32 Id = Skills->SelectedItem();
	const int32 K = JeebItems::IsGear(Id) ? JeebItems::KindOf(Id) : -1;
	const int32 Tier = (K >= 0) ? JeebItems::VariantOf(Id) : 0;
	const ESkillType Sk = JeebItems::CombatSkillFor(K < 0 ? JeebItems::K_Sword : K);

	if (AtkStyle == EAtkStyle::Cast)
	{
		FRotator Aim = GetActorRotation();
		if (AController* C = GetController()) Aim = C->GetControlRotation();
		const FVector Dir = Aim.Vector();
		const bool bBow = (K == JeebItems::K_Bow);
		bool bCrit = false;
		const int32 Dmg = JeebItems::RollDamage(Skills, K, Tier, bBow ? PendingArrowTier : 0, bCrit);
		const float Range = JeebItems::WeaponBase(K).Range;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Start = GetActorLocation() + GetActorForwardVector() * 70.f + FVector(0.f, 0.f, 30.f);
		if (AProjectile* P = GetWorld()->SpawnActor<AProjectile>(AProjectile::StaticClass(), Start, Dir.Rotation(), SP))
		{
			P->Launch(this, Dir, bBow ? 3500.f : (K == JeebItems::K_Staff ? 1800.f : 2200.f), Range, Dmg, bCrit, Sk, !bBow,
				bBow ? PendingArrowTier : (K == JeebItems::K_Staff ? 100 : 0));
		}
		return;
	}

	const bool bThrust = (AtkStyle == EAtkStyle::Thrust);
	// The sword's jab hits softer than a full swing (it is much quicker, and it loops)
	const bool bJab = (AtkStyle == EAtkStyle::Slash && K == JeebItems::K_Sword);
	const float JabMul = bJab ? 0.75f : 1.f;
	const float Range = ((K >= 0) ? JeebItems::WeaponBase(K).Range : 200.f) + (bThrust ? 140.f * AtkCharge : 0.f);
	const float MinDot = bThrust ? 0.8f : 0.2f;
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();

	TArray<UHealthComponent*> Targets;
	UHealthComponent* Nearest = nullptr;
	float NearestD = 1.0e9f;
	for (UHealthComponent* H : UHealthComponent::All())
	{
		if (!H || !H->IsAlive() || H->GetOwner() == this) continue;
		const FVector To = H->GetOwner()->GetActorLocation() - GetActorLocation();
		const float D = To.Size2D();
		if (D > Range + H->HitRadius * 0.4f) continue;
		if (D > 40.f && FVector::DotProduct(Fwd, To.GetSafeNormal2D()) < MinDot) continue;
		if (FMath::Abs(To.Z) > 260.f) continue;
		Targets.Add(H);
		if (D < NearestD) { NearestD = D; Nearest = H; }
	}
	if (bThrust && Nearest) { Targets.Reset(); Targets.Add(Nearest); }
	for (UHealthComponent* H : Targets)
	{
		bool bCrit = false;
		const int32 Dmg = JeebItems::RollDamage(Skills, K, Tier, 0, bCrit, (bThrust ? (1.f + AtkCharge) : 1.f) * JabMul);
		DealDamage(H, Dmg, bCrit, Sk);
	}
}

void AKnightCharacter::Hurt(int32 Raw, AActor* From, bool bEnvironmental)
{
	if (Health <= 0.f || Raw <= 0 || !Skills || !HasAuthority()) return;   // (only the server hurts anyone)
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < InvulnUntil) return;

	bool bFront = false;
	if (!bEnvironmental && bBlocking && ShieldActive() && From)
	{
		const FVector To = (From->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		bFront = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To) > 0.1f;
	}
	const int32 Armor = bEnvironmental ? 0 : JeebItems::TotalDefense(Skills, false) / 3;   // smoke seeps through armour
	const int32 FromSkill = bEnvironmental ? 0 : Skills->GetLevel(ESkillType::Defense) / 5;
	// The shield takes the blow first. With its guard up it stops damage until the guard (its Defence) is spent; a broken
	// guard stops half of each blow until it has recovered. Whatever gets past the shield is then softened by armour.
	int32 ShieldStop = 0;
	if (bFront)
	{
		if (!bGuardBroken)
		{
			ShieldStop = FMath::Min(Raw, FMath::FloorToInt(GuardPool));
			GuardPool -= ShieldStop;
			if (GuardPool < 1.f)
			{
				GuardPool = 0.f;
				bGuardBroken = true;
				Skills->Say(2.5f, FColor::Orange, TEXT("Guard broken! Your shield stops only half a blow until it recovers."));
			}
		}
		else ShieldStop = Raw / 2;
		LastGuardHitTime = Now;
	}
	const int32 Through = Raw - ShieldStop;
	const int32 Taken = (Through <= 0) ? 0 : FMath::Max(1, Through - (Armor + FromSkill));

	const FVector Head = GetActorLocation() + FVector(0.f, 0.f, 120.f);
	if (ShieldStop > 0) JeebCombat::AddNumber(GetWorld(), Head + FVector(0.f, 0.f, 25.f), ShieldStop, JeebCombat::N_Blocked);
	if (Taken > 0) JeebCombat::AddNumber(GetWorld(), Head, Taken, JeebCombat::N_Damage);
	Health -= Taken;
	LastHurtTime = Now;
	InvulnUntil = Now + 0.4f;
	// Defence trains only from blows you block with a raised shield, facing the attacker.
	// Armour soaking a hit trains nothing, nor do hazards like poison smoke, nor damage that got through.
	if (!bEnvironmental && ShieldStop > 0) Skills->AddXP(ESkillType::Defense, 2 + ShieldStop / 2);
	if (Health <= 0.f) Die();   // any death: the sack, and a fresh start in Blackrock
}

void AKnightCharacter::StartBlock()
{
	bBlockHeld = true;
	if (bSitting || bRolling || bClimbing || IsUIOpen() || !ShieldActive() || bAttacking) return;   // no blocking mid-swing
	bBlocking = true;
}

void AKnightCharacter::StopBlock()
{
	bBlockHeld = false;
	bBlocking = false;
}

void AKnightCharacter::HurtHorse(float Amount)
{
	HorseHealth = FMath::Clamp(HorseHealth - Amount, 0.f, HorseMaxHealth);
	LastHorseHurtTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

// ---------------- Dying ----------------
// Whatever killed you (a blow, the poison gas, anything), everything you own goes into a big sack where you fell: your pack,
// your weapon bar, your armour and the rest of what you wore, your potions and your ingots. You wake up in Blackrock, just
// inside the east gate, in the starting kit, at full health. Skills and levels stay with you.
void AKnightCharacter::Die()
{
	if (!Skills || !HasAuthority()) return;
	if (bMounted) Dismount();
	if (bClimbing) ExitClimb(false, false);
	if (bSitting) StandUp();
	const FVector Fell = GetActorLocation();
	TArray<FInvSlot> Items;
	TArray<uint8> Where;
	int32 Ingots = 0;
	Skills->TakeEverything(Items, Where, Ingots);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ALootDrop* Sack = GetWorld()->SpawnActor<ALootDrop>(FVector(Fell.X, Fell.Y, JeebTerrain::HeightAt(Fell.X, Fell.Y) + 2.f), FRotator::ZeroRotator, SP))
		Sack->SetupSack(Items, Where, Ingots);

	Skills->ResetToStartingKit();
	RefreshEquipment();
	CloseLoot();
	bAttacking = false; bCharging = false; bBlocking = false; bBlockHeld = false;
	SpeedUntil = -1.f; VigourUntil = -1.f; RadarUntil = -1.f;
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
	const FVector2D VC = JeebTerrain::VillageCenter();
	const FVector2D Wake(VC.X + JeebTerrain::VillageHalf() - 450.f, VC.Y);
	const FVector WakeAt(Wake.X, Wake.Y, JeebTerrain::HeightAt(Wake.X, Wake.Y) + 100.f);
	SetActorLocation(WakeAt);
	SetActorRotation(FRotator(0.f, 180.f, 0.f));
	MaxHealth = 100 + (Skills->GetLevel(ESkillType::Defense) - 1) * 3;
	Health = (float)MaxHealth;
	LastHurtTime = -100.f;
	if (!IsLocallyControlled() && GetNetMode() != NM_Standalone) ClientDied(WakeAt, 180.f);   // the player's machine wakes up too
	else JeebMsg::Post(5.f, FColor::Red, TEXT("You fell. Everything you had is in a sack where you fell. You wake up in Blackrock."));
}

// ---------------- Potions ----------------
float AKnightCharacter::GetPotionCooldownLeft(int32 Type) const
{
	if (!PotionReadyAt.IsValidIndex(Type)) return 0.f;
	return FMath::Max(0.f, PotionReadyAt[Type] - GetWorld()->GetTimeSeconds());
}

float AKnightCharacter::GetRadarLeft() const
{
	return FMath::Max(0.f, RadarUntil - GetWorld()->GetTimeSeconds());
}

// Server: while the radar potion lasts, the spots of everything hostile (the pen knight) and every other player within the
// minimap's reach, twice a second. Only the drinker receives them (owner-only replication); dots only, no names.
void AKnightCharacter::UpdateRadar()
{
	if (!HasAuthority() || !GetWorld()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < RadarUntil)
	{
		if (Now - LastRadarScan < 0.5) return;
		LastRadarScan = Now;
		const float Reach = 6600.f;   // a little beyond the minimap's 6500
		const FVector Me = GetActorLocation();
		TArray<FVector2D> Dots;
		for (TActorIterator<AFoeKnight> It(GetWorld()); It; ++It)
			if (!It->IsHidden() && FVector::Dist2D(It->GetActorLocation(), Me) < Reach) Dots.Add(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
		for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
			if (*It != this && !It->IsHidden() && FVector::Dist2D(It->GetActorLocation(), Me) < Reach) Dots.Add(FVector2D(It->GetActorLocation().X, It->GetActorLocation().Y));
		RadarDots = Dots;
	}
	else if (RadarDots.Num() > 0) RadarDots.Reset();   // over: clear the dots once
}

// The server checks the cooldown, takes the potion and runs its effect (DoInv, EInvOp::Drink)
void AKnightCharacter::DrinkPotion()
{
	if (!Skills || IsUIOpen()) return;
	Inv(EInvOp::Drink);
}

// ---------------- Tick steps (see AKnightCharacter::Tick) ----------------
// A swing landing and ending (and the follow-up swing), and the spear's charge
void AKnightCharacter::TickAttack(float Dt)
{
	if (bAttacking)
	{
		// A sword hit keeps facing the way it started (as in Smash): sliding along a wall or being steered never turns him
		// mid-swing. Not in strafe mode or the shield stance: there he keeps turning with the camera, swing or not.
		if (AtkStyle == EAtkStyle::Slash && SelectedKind() == JeebItems::K_Sword && !bStrafeMode && !bBlockHeld) SetActorRotation(FRotator(0.f, AttackYaw, 0.f));
		AttackT += Dt;
		const float HitFrac = (AtkStyle == EAtkStyle::Slash) ? 0.43f : (AtkStyle == EAtkStyle::Thrust ? 0.5f : 0.3f);
		if (!bAttackHit && AttackT >= AttackDur * HitFrac)
		{
			bAttackHit = true;
			AttackLands();
		}
		if (AttackT >= AttackDur)
		{
			bAttacking = false;
			const float Now = GetWorld()->GetTimeSeconds();
			LastSwingEnd = Now;
			LastSwingDir = AttackDir;
			LastStyle = AtkStyle;
			// Clicked during the swing: go straight into the next one. The sword alternates its two hits (forehand, backhand)
			// for as long as you click, and any click during a swing counts, so one click = one swing. Other blades chain two,
			// and the click must come in the last 0.3 s.
			if (AtkStyle == EAtkStyle::Slash && !IsUIOpen())
			{
				if (SelectedKind() == JeebItems::K_Sword)
				{
					// (one buffered press per swing, Smash style; the two hits alternate for as long as you keep clicking)
					const int32 Next = (AttackDir == JabFirst) ? JabSecond : JabFirst;
					if (QueuedJabs > 0) StartAttack(EAtkStyle::Slash, Next, 0.f);   // (the buffered press; StartAttack empties the buffer)
				}
				else if (AttackDir > 0 && Now - QueuedClick <= 0.3f) StartAttack(EAtkStyle::Slash, -1, 0.f);
			}
		}
	}

	// Spear: charging builds up while the button is held
	if (bCharging)
	{
		ChargeT += Dt;
		if (IsUIOpen() || bRolling || bSliding || bClimbing || bSitting || SelectedKind() != JeebItems::K_Spear) bCharging = false;
	}
}

// Health (and the horse's): the bigger bar from Defence, the Vitality amulet and the vigour potion; healing after 10 s unhurt
void AKnightCharacter::TickHealth(float Dt)
{
	// The horse heals the same way: 10 s after its last hit, a full bar takes 10 s. (Not in PvP areas, once those exist.)
	if (HorseHealth < HorseMaxHealth && GetWorld()->GetTimeSeconds() - LastHorseHurtTime > 10.f)
		HorseHealth = FMath::Min(HorseMaxHealth, HorseHealth + HorseMaxHealth * 0.1f * Dt);
	// Health: your maximum grows with Defence and the Vitality amulet; you heal when left alone for 10 s (the server's job online)
	if (Skills && HasAuthority())
	{
		int32 NewMax = 100 + (Skills->GetLevel(ESkillType::Defense) - 1) * 3 + JeebItems::CombinedMods(Skills).MaxHealthBonus;
		if (GetWorld()->GetTimeSeconds() < VigourUntil) NewMax = FMath::RoundToInt(NewMax * VigourMul);   // vigour potion: a bigger bar for a while
		if (NewMax != MaxHealth)
		{
			if (NewMax > MaxHealth) Health += NewMax - MaxHealth;   // a bigger bar comes with the extra already full
			else Health = FMath::Clamp(Health, 1.f, (float)NewMax);   // a smaller one only trims what does not fit
			MaxHealth = NewMax;
		}
		if (Health < MaxHealth && GetWorld()->GetTimeSeconds() - LastHurtTime > 10.f)   // 10 s after the last hit it starts to heal
		{
			Health = FMath::Min((float)MaxHealth, Health + MaxHealth * 0.1f * Dt);   // a full bar takes 10 s
		}
	}
}

// An open sack closing, the shield and weapon going on the back, the map fog, the shield guard draining and recovering, and
// whether the shield is up
void AKnightCharacter::TickGear(float Dt)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	// An open sack closes when you walk away from it, or put the cursor away
	if (LootTarget.IsValid() && (!bPanelCursor || FVector::Dist2D(GetActorLocation(), LootTarget->GetActorLocation()) > 400.f)) LootTarget = nullptr;

	// Climbing, riding or sitting: the shield and the weapon go on the back; they come back to the hands afterwards
	{
		const bool bWantStow = bClimbing || bMounted || bSitting;
		if (bWantStow != bStowed)
		{
			bStowed = bWantStow;
			PlaceShield(bStowed);
			RebuildHeld();
			if (bStowed) bBlocking = false;
		}
	}

	// Shield guard: its size is the shield's Defence; a new or changed shield starts full. It recovers like health:
	// 10 s after the last blow it took, a full bar in 10 s. A broken guard is mended only once it is full again.
	if (HasAuthority())   // (online: the server's job; the player's machine is sent the numbers)
	{
		if (Skills && GetWorld()->GetTimeSeconds() >= NextExploreTime)   // map fog
		{
			NextExploreTime = GetWorld()->GetTimeSeconds() + 0.5f;
			Skills->Explore(GetActorLocation());
		}
		const int32 ShieldId = (Skills && ShieldActive()) ? Skills->Worn[JeebItems::K_Shield] : 0;
		const float NewMax = ShieldId ? float(JeebItems::ShieldGuard(ShieldId)) : 0.f;
		const float RecoverDelay = ShieldId ? JeebItems::ShieldRecoverDelay(JeebItems::StyleOf(ShieldId)) : 10.f;   // the round shield recovers sooner
		if (!FMath::IsNearlyEqual(NewMax, GuardMax)) { GuardMax = NewMax; GuardPool = NewMax; bGuardBroken = false; }
		// Holding the shield up drains the guard slowly (Smash Bros style): a full guard lasts GuardDrainSeconds of holding,
		// and it never refills while raised. Lowered, it recovers as before.
		constexpr float GuardDrainSeconds = 15.f;
		if (bBlocking && GuardMax > 0.f && !bGuardBroken)
		{
			GuardPool -= GuardMax / GuardDrainSeconds * Dt;
			if (GuardPool < 1.f)
			{
				GuardPool = 0.f;
				bGuardBroken = true;
				LastGuardHitTime = GetWorld()->GetTimeSeconds();
				if (Skills) Skills->Say(2.5f, FColor::Orange, TEXT("Guard broken! Your shield stops only half a blow until it recovers."));
			}
		}
		else if (GuardMax > 0.f && GuardPool < GuardMax && GetWorld()->GetTimeSeconds() - LastGuardHitTime > RecoverDelay)
		{
			GuardPool = FMath::Min(GuardMax, GuardPool + GuardMax * 0.1f * Dt);
			if (GuardPool >= GuardMax && bGuardBroken)
			{
				bGuardBroken = false;
				if (Skills) Skills->Say(2.5f, FColor(140, 200, 255), TEXT("Your shield guard is back to full."));
			}
		}
	}

	// The shield and the sword never work together: it is down during a swing, and comes back up once the swing is over if the button is still held
	if (bAttacking || bRolling || Move->IsFalling()) bBlocking = false;   // no guard mid-swing, mid-roll or in the air
	else if (bBlockHeld && !bBlocking && ShieldActive() && !bRolling && !Move->IsFalling() && !bSliding && !bClimbing && !bSitting && !bMounted && !IsUIOpen()) bBlocking = true;
}
