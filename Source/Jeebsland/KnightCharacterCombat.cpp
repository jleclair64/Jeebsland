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
	bAttacking = true;
	bBlocking = false;   // the shield drops for the swing; Tick raises it again afterwards if the block button is still held
	bAttackHit = false;
	AttackT = 0.f;
	QueuedClick = -10.f;
	if (AController* C = GetController()) SetActorRotation(FRotator(0.f, C->GetControlRotation().Yaw, 0.f));   // face where you aim
	// Online: our swing goes to the server (which lands it); the server shows it to everyone else
	if (GetNetMode() != NM_Standalone)
	{
		if (IsLocallyControlled() && !HasAuthority()) ServerAttack(static_cast<uint8>(Style), static_cast<int8>(Dir), Charge);
		else if (HasAuthority()) MulticastAttack(static_cast<uint8>(Style), static_cast<int8>(Dir), Charge);
	}
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
		const int32 Dmg = JeebItems::RollDamage(Skills, K, Tier, 0, bCrit, bThrust ? (1.f + AtkCharge) : 1.f);
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
	SpeedUntil = -1.f; VigourUntil = -1.f;
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
	if (Type < 0 || Type >= JeebItems::NumPotions) return 0.f;
	return FMath::Max(0.f, PotionReadyAt[Type] - GetWorld()->GetTimeSeconds());
}

// The server checks the cooldown, takes the potion and runs its effect (DoInv, EInvOp::Drink)
void AKnightCharacter::DrinkPotion()
{
	if (!Skills || IsUIOpen()) return;
	Inv(EInvOp::Drink);
}

