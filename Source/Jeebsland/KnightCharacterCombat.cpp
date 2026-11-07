#include "KnightCharacter.h"
#include "JeebProps.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

#include "KnightAnimator.h"
#include "Camera/CameraComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
#include "HealthComponent.h"
#include "HitBoxes.h"
#include "ProceduralMeshComponent.h"
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
#include "TimerManager.h"

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
	if (bDrinking) return;   // (the bottle is in that hand)
	const int32 K = SelectedKind();
	const float Cd = (K >= 0) ? JeebItems::WeaponBase(K).Cooldown : 0.5f;
	if (bAttacking && AtkStyle == EAtkStyle::Kick && Style == EAtkStyle::Sweep) { KickFade = 1.f; KickFadeT = AttackT / FMath::Max(AttackDur, 0.01f); }   // (a landed kick cut short by the sweep: the leg eases out from there)
	bKickLanded = false;
	AtkStyle = Style;
	AttackDir = Dir;
	AtkCharge = Charge;
	AttackDur = (Style == EAtkStyle::Thrust) ? 0.45f : (Style == EAtkStyle::Cast ? FMath::Max(0.45f, Cd * 1.1f) : Cd * 1.25f);
	if (Dir < 0) AttackDur *= 0.85f;           // the follow-up swing is 15% faster
	KickedKnights.Reset();   // (a new move can hit every other knight again)
	if (Style == EAtkStyle::Kick)
	{
		AttackDur = KickDur / SwingSpeedScale();
		if ((IsLocallyControlled() || HasAuthority()) && !GetCharacterMovement()->IsFalling())
			LaunchCharacter(GetActorForwardVector().GetSafeNormal2D() * 160.f, true, false);   // a small step in as the knee comes up
	}
	if ((Style == EAtkStyle::Slash || Style == EAtkStyle::Sweep || Style == EAtkStyle::Dash) && JeebItems::FightsLikeSword(K))
	{
		// The sword's jab: two quick swings that alternate, each with a small step forward (the low sweep and the dash attack:
		// one each, keyed the same way)
		AttackDur = (Style == EAtkStyle::Sweep ? SweepDur : Style == EAtkStyle::Dash ? 0.3f + DashLag() : Cd * (Dir == JabSecond ? JeebTune::V().Jab2Share : JeebTune::V().Jab1Share)) / SwingSpeedScale();   // (testing: DefaultGame.ini SwingSpeed slows it)
		if ((IsLocallyControlled() || HasAuthority()) && !GetCharacterMovement()->IsFalling())
			LaunchCharacter(GetActorForwardVector().GetSafeNormal2D() * (Style == EAtkStyle::Sweep ? 120.f : Style == EAtkStyle::Dash ? DashLunge : 210.f), true, false);   // (the player's machine and the server step together; the dash lunges on)
	}
	bAttacking = true;
	bBlocking = false;   // the shield drops for the swing; Tick raises it again afterwards if the block button is still held
	bAttackHit = false;
	HitThisSwing.Reset();     // (a new move can hit everyone again)
	bHitPrevValid = false;
	AttackT = 0.f;
	QueuedClick = -10.f;
	QueuedJabs = 0;   // (a new swing starts with an empty buffer: presses made before it began are used up)
	Smash = ESmash::None; bSmashSwing = false; SmashT = 0.f; SmashCoilNow = 0.f;   //(the caller makes hit 1 a smash; online the player's next swing ends the server's end lag)
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

// Testing: sword swings can be played slowly (DefaultGame.ini, [Jeebsland.Testing] SwingSpeed=0.2 is 80% slower). 1 = normal.
float AKnightCharacter::SwingSpeedScale()
{
	static float Scale = -1.f;
	if (Scale < 0.f)
	{
		Scale = 1.f;
		if (GConfig) GConfig->GetFloat(TEXT("Jeebsland.Testing"), TEXT("SwingSpeed"), Scale, GGameIni);
		Scale = FMath::Clamp(Scale, 0.05f, 4.f);
	}
	return Scale;
}

void AKnightCharacter::ShotPlayMove(const FString& Move, float FaceYaw)
{
	if (!Skills) return;
#if WITH_EDITOR
	// (a new material still compiling shows as plain grey, and a short test run ends before it is done: test pictures wait for it)
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
#endif
	bShotFace = true;
	ShotFaceYaw = FaceYaw;
	SetActorRotation(FRotator(0.f, FaceYaw, 0.f));
	{
		float Fov = 0.f;   // (-ShotFov=30: a close look)
		if (FParse::Value(FCommandLine::Get(), TEXT("ShotFov="), Fov) && GetFollowCamera()) GetFollowCamera()->SetFieldOfView(Fov);
	}
	for (int32 h = 0; h < 5; ++h)   // a sword in hand
		if (JeebItems::IsGear(Skills->Hotbar[h]) && JeebItems::KindOf(Skills->Hotbar[h]) == JeebItems::K_Sword) { SelectHotbar(h); break; }
	{
		int32 ShotKind = -1;   // (-ShotKind=<number>: that weapon in the first bar slot instead)
		if (FParse::Value(FCommandLine::Get(), TEXT("ShotKind="), ShotKind)) { Skills->Hotbar[0] = JeebItems::GearId(ShotKind, 0); SelectHotbar(0); RebuildHeld(); Skills->OnGearChanged.Broadcast(); }   // (and the armour redrawn: a two-handed weapon puts the shield away)
	}
	if (Move == TEXT("jab")) { StartAttack(EAtkStyle::Slash, JabFirst, 0.f); QueuedJabs = 1; }   // two hits (the buffer holds one press)
	if (Move == TEXT("jab1")) StartAttack(EAtkStyle::Slash, JabFirst, 0.f);                     // hit 1 alone
	if (Move == TEXT("kick")) StartAttack(EAtkStyle::Kick, 1, 0.f);                             // the kick (wheel up)
	if (Move == TEXT("sweep")) StartAttack(EAtkStyle::Sweep, 1, 0.f);                           // the low sweep (wheel down)
	if (Move == TEXT("dash")) StartAttack(EAtkStyle::Dash, 1, 0.f);                              // the dash attack (sprint + click): the lunge
	if (Move == TEXT("kickcombo")) { StartAttack(EAtkStyle::Kick, 1, 0.f); QueuedSweep = GetWorld()->GetTimeSeconds() + 0.1f; }   // a kick with the wheel-down buffered: the sweep cuts in when it lands
	int32 TestBow = JeebItems::K_Bow;   // (-ShotBowKind=21 longbow, 22 crossbow)
	FParse::Value(FCommandLine::Get(), TEXT("ShotBowKind="), TestBow);
	int32 TestArrowTier = -1;   // (-ShotArrow=4: he carries that tier's arrows, for the quiver and the feathers)
	if (FParse::Value(FCommandLine::Get(), TEXT("ShotArrow="), TestArrowTier) && Skills->AddInvItem(JeebItems::ArrowId(TestArrowTier), 10)) ArrowPick = JeebItems::ArrowId(TestArrowTier);
	if (Move.StartsWith(TEXT("bow")) && TestBow != JeebItems::K_Bow) Skills->Hotbar[1] = JeebItems::GearId(TestBow, 0);
	if (Move == TEXT("bowidle") || Move == TEXT("bowwalk"))   // the bow in hand, not drawn: standing, or walking ahead
	{
		for (int32 h = 0; h < 5; ++h)
			if (JeebItems::IsGear(Skills->Hotbar[h]) && JeebItems::KindOf(Skills->Hotbar[h]) == TestBow) { SelectHotbar(h); break; }
		bShotWalk = Move == TEXT("bowwalk");
	}
	else if (Move.StartsWith(TEXT("bow")))   // the bow: bow = a full draw held 2 s, then the shot; bow60 = shot after 0.6 s
	{
		for (int32 h = 0; h < 5; ++h)
			if (JeebItems::IsGear(Skills->Hotbar[h]) && JeebItems::KindOf(Skills->Hotbar[h]) == TestBow) { SelectHotbar(h); break; }
		if (ArrowSlotId() == 0 && !Skills->AddInvItem(JeebItems::ArrowId(JeebItems::T_Iron), 10) && Skills->Slots.Num() > 0)   // (test pictures are never saved)
		{ Skills->Slots.Last().Id = JeebItems::ArrowId(JeebItems::T_Iron); Skills->Slots.Last().Count = 10; }
		FParse::Value(FCommandLine::Get(), TEXT("ShotStrafe="), ShotStrafe);
		StartDraw();
		ShotBowAt = GetWorld()->GetTimeSeconds() + ((Move.Len() > 3) ? FCString::Atoi(*Move.Mid(3)) / 100.f : DrawTimeNow() + 2.f);
	}
	if (Move == TEXT("kicksweep")) { StartAttack(EAtkStyle::Kick, 1, 0.f); ShotSweepAt = GetWorld()->GetTimeSeconds() + KickDur + 0.02f; }   // a kick, then the sweep on him lying there
	if (Move == TEXT("block")) { bBlockHeld = true; StartBlock(); }                              // the shield held up
	if (Move == TEXT("jump") || Move.StartsWith(TEXT("hop")))   // a full jump (Space held 0.5 s) or a short hop (tapped 0.02 s; hop60 = held 60 ms); log JEEBJUMP gives the height
	{
		Jump();
		ShotJumpFrom = GetActorLocation().Z;
		ShotJumpT = GetWorld()->GetTimeSeconds();
		const float Hold = Move == TEXT("jump") ? 0.5f : Move.Len() > 3 ? FCString::Atoi(*Move.Mid(3)) / 1000.f : 0.02f;
		FTimerHandle T;
		GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(this, [this]() { StopJumping(); }), Hold, false);
	}
	if (Move == TEXT("jab2")) { StartAttack(EAtkStyle::Slash, JabFirst, 0.f); QueuedJabs = 1; }   // hits 1 and 2
	if (Move.StartsWith(TEXT("smash")))   // the smash attack: smash = full charge (1 s), smash40 = 0.4 s of charge (hundredths: the command line stops at a dot)
	{
		StartAttack(EAtkStyle::Slash, JabFirst, 0.f);
		SetSmash(ESmash::Deciding);
		ShotSmashHold = (Move.Len() > 5) ? FCString::Atoi(*Move.Mid(5)) / 100.f : SmashChargeTime;
	}
}

void AKnightCharacter::DealDamage(UHealthComponent* Target, int32 Dmg, bool bCrit, ESkillType Skill, bool bHitBoxes, uint8 Zone, bool bHeadshot)
{
	if (!Target || !Target->IsAlive()) return;
	const int32 Dealt = Target->ApplyDamage(Dmg, this, bCrit, bHitBoxes, Zone, bHeadshot);
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

	if (AtkStyle == EAtkStyle::Kick || AtkStyle == EAtkStyle::Sweep || AtkStyle == EAtkStyle::Dash) return;   // (the kick, the sweep and the dash land through their hit boxes: TickHitboxes)
	const bool bThrust = (AtkStyle == EAtkStyle::Thrust);
	// The sword's jab hits softer than a full swing (it is much quicker, and it loops)
	const bool bJab = (AtkStyle == EAtkStyle::Slash && JeebItems::FightsLikeSword(K));
	if (bJab) return;   // (the sword's jab lands through its hit boxes: TickHitboxes)
	const float JabMul = 1.f;
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

void AKnightCharacter::Hurt(int32 Raw, AActor* From, bool bEnvironmental, uint8 Zone, bool bHeadshot)
{
	if (Health <= 0.f || Raw <= 0 || !Skills || !HasAuthority()) return;   // (only the server hurts anyone)
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < InvulnUntil) return;

	bool bFront = false;
	if (!bEnvironmental && bBlocking && ShieldActive() && From)
	{
		// (the shield's wedge: 45 degrees either side of straight ahead, HitBoxes.h)
		const FVector To = (From->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		bFront = FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), To) >= FMath::Cos(FMath::DegreesToRadians(JeebHit::FShieldWedge().HalfAngle));
	}
	// The shield takes the blow (owner, 2026-10-31): a blow on a raised shield never hurts the knight; its health takes it, one for one (owner, 2026-11-06).
	// The blow that empties it breaks it: the shield drops and can't be raised until it has healed to full.
	int32 ShieldStop = 0;
	if (bFront && !bGuardBroken)
	{
		ShieldStop = Raw;
		GuardPool -= Raw;
		if (GuardPool < 1.f)
		{
			GuardPool = 0.f;
			bGuardBroken = true;
			bBlocking = false;
			Skills->Say(2.5f, FColor::Orange, TEXT("Guard broken! Your shield is down until it recovers."));
		}
		LastGuardHitTime = Now;
	}
	const int32 Through = Raw - ShieldStop;
	// Armour by body part (owner, 2026-11-02): the piece covering where it landed lets only part of it through (smoke seeps
	// through armour). The Defence level no longer softens blows: it sizes the shield's guard.
	const float Take = bEnvironmental ? 1.f : JeebItems::ZoneTake(Skills, Zone);
	const int32 Taken = (Through <= 0) ? 0 : FMath::Max(1, FMath::RoundToInt(Through * Take));

	const FVector Head = GetActorLocation() + FVector(0.f, 0.f, 120.f);
	if (ShieldStop > 0) JeebCombat::AddNumber(GetWorld(), Head + FVector(0.f, 0.f, 25.f), ShieldStop, JeebCombat::N_Blocked);
	if (Taken > 0) JeebCombat::AddNumber(GetWorld(), Head, Taken, bHeadshot ? JeebCombat::N_Headshot : JeebCombat::N_Damage);
	Health -= Taken;
	LastHurtTime = Now;
	// (hazards tick: a short safe spell after each. Blows don't: each move hits once anyway, and combos must be able to land)
	if (bEnvironmental) InvulnUntil = Now + 0.4f;
	// Defence trains only from blows you block with a raised shield, facing the attacker.
	// Armour soaking a hit trains nothing, nor do hazards like poison smoke, nor damage that got through.
	if (!bEnvironmental && ShieldStop > 0) Skills->AddXP(ESkillType::Defense, 2 + ShieldStop / 2);
	if (bDrinking && !bEnvironmental) MulticastDrink(DrinkType, false);   // (any blow stops a drink, stun or not: he keeps the potion)
	if (Health <= 0.f) { Die(); return; }   // any death: the sack, and a fresh start in Blackrock
	// Attack beats kick (owner, 2026-10-31): a blow that lands while you are kicking throws you to the ground
	if (!bEnvironmental && ShieldStop == 0 && IsKicking()) KnockDownBy(From);
}

void AKnightCharacter::StartBlock()
{
	bBlockHeld = true;
	if (bSitting || bRolling || bClimbing || bDrinking || IsUIOpen() || !ShieldActive() || bAttacking || bKnockedDown || bGuardBroken || StunLeft > 0.f) return;   // no blocking mid-swing, lying down, in hitstun or with the guard broken
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
	if (AResourceNode* Node = ActiveNode.Get()) Node->CancelGathering();
	if (bKnockedDown) MulticastKnock(false, 0);   // (he wakes standing)
	bKnockedDown = false; KnockPhase = 0;
	if (bDrinking) MulticastDrink(DrinkType, false);   // (Health is refilled below in this same call, so TickDrink would never see the death)
	CrossReloadLeft = 0.f;
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
	bAttacking = false; bCharging = false; bBlocking = false; bBlockHeld = false; bAimHeld = false;
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

// The server checks the cooldown and starts the drinking (DoInv, EInvOp::Drink); the potion goes and works at the end of it (TickDrink)
void AKnightCharacter::DrinkPotion()
{
	if (!Skills || IsUIOpen() || !CanDrinkNow()) return;
	if (!HasAuthority())
	{
		const FInvSlot& P = Skills->PotionSlot;
		bDrinkPredicted = false;
		if (P.Id != 0 && JeebItems::IsPotion(P.Id) && P.Count > 0 && GetPotionCooldownLeft(JeebItems::PotionType(P.Id)) <= 0.f)
			{
				MulticastDrink_Implementation(static_cast<uint8>(JeebItems::PotionType(P.Id)), true);
				bDrinkPredicted = true;
			}
	}
	Inv(EInvOp::Drink);
}

void AKnightCharacter::ClientDrinkRefused_Implementation()
{
	bDrinkPredicted = false;
	if (bDrinking) MulticastDrink_Implementation(DrinkType, false);
}

bool AKnightCharacter::CanDrinkNow() const
{
	return !bDrinking && Health > 0.f && !bAttacking && !bKnockedDown && StunLeft <= 0.f && !bRolling && !bSliding && !bClimbing && !bSitting && !bMounted && Bow == EBow::None
		&& !ActiveNode.IsValid() && GetWorld()->GetTimeSeconds() - LastDrinkStart >= 0.5f;   // (not over and over: each start is a message to everyone in range)
}

// The bottle's size in the hand, and how far up it the hand holds it (the middle of its belly, in cm from the model's origin at this size). Its belly is about this thick there.
static constexpr float BottleScale = 0.9f, BottleGripZ = 8.55f, BottleR = 6.1f;

// The open right hand round the bottle (owner 2026-11-06: the skeleton has no finger bones, so the fist can't open). Plated like the gauntlet, "a tiny bit cartoonish" with crisp bevels.
// Its frame: Z along the bottle (the cork at -Z), the palm out along +X, the fingers run on round the bottle toward +Y, the wrist at FClipLib::DrinkInHand's (9.5, -8.5, 0), the thumb toward the cork.
static void BuildOpenHand(KM::FMD& Out)
{
	// One plate: a rounded box lying along the circle round the bottle, from angle A to angle B (degrees, 0 = straight out through the palm) at radius R and height Z
	auto Plate = [&Out](float A, float B, float R, float Z, float HalfWide, float HalfThick, float Round, float ZTo = TNumericLimits<float>::Max())
	{
		const float Mid = FMath::DegreesToRadians((A + B) * 0.5f), Z2 = ZTo == TNumericLimits<float>::Max() ? Z : ZTo;
		const FVector P0(R * FMath::Cos(FMath::DegreesToRadians(A)), R * FMath::Sin(FMath::DegreesToRadians(A)), Z);
		const FVector P1(R * FMath::Cos(FMath::DegreesToRadians(B)), R * FMath::Sin(FMath::DegreesToRadians(B)), Z2);
		const FVector Along = (P1 - P0).GetSafeNormal(), Normal(FMath::Cos(Mid), FMath::Sin(Mid), 0.f);
		const FQuat Q = FRotationMatrix::MakeFromXZ(Along, Normal).ToQuat();
		KM::FMD S;
		KM::RBox(S, FVector::Dist(P0, P1) * 0.5f + 0.25f, HalfWide, HalfThick, Round, KM::Xf(FVector::ZeroVector));
		const int32 Base = Out.V.Num();
		for (const FVector& V : S.V) Out.V.Add((P0 + P1) * 0.5f + Q.RotateVector(V));
		for (const int32 I : S.I) Out.I.Add(Base + I);
	};
	const float FingerR = BottleR + 0.75f, PalmR = BottleR + 1.9f;
	// The back of the hand: three plates from the wrist to the knuckles, bending round the bottle
	Plate(-44.f, -24.f, PalmR + 0.3f, 0.f, 4.0f, 1.45f, 0.45f);
	Plate(-27.f, -7.f, PalmR + 0.1f, 0.f, 4.5f, 1.5f, 0.45f);
	Plate(-9.f, 10.f, PalmR, 0.f, 4.8f, 1.5f, 0.45f);
	// The knuckles' ridge
	Plate(10.f, 15.f, PalmR - 0.35f, 0.f, 4.9f, 1.25f, 0.6f);
	// Four fingers, three plates each, closing round the bottle. Index at the thumb's side (toward the cork).
	const float FingerZ[4] = { -3.45f, -1.15f, 1.15f, 3.35f }, FingerW[4] = { 1.13f, 1.18f, 1.12f, 0.98f }, Reach[4] = { 1.f, 1.08f, 1.f, 0.82f };
	for (int32 f = 0; f < 4; ++f)
	{
		float A = 12.f;
		const float Len[3] = { 34.f, 26.f, 22.f };
		for (int32 s = 0; s < 3; ++s)
		{
			const float B = A + Len[s] * Reach[f];
			Plate(A, B, FingerR, FingerZ[f], FingerW[f] * (1.f - s * 0.07f), 0.82f - s * 0.06f, 0.55f);
			A = B;
		}
	}
	// The thumb, the other way round the bottle and up toward the cork, over its shoulder
	Plate(-30.f, -58.f, BottleR + 0.95f, -3.6f, 1.35f, 1.0f, 0.55f, -4.4f);
	Plate(-58.f, -84.f, BottleR + 0.55f, -4.4f, 1.2f, 0.85f, 0.55f, -4.9f);
	// Its root, joining it to the palm
	Plate(-24.f, -38.f, PalmR - 0.4f, -3.2f, 1.6f, 1.2f, 0.6f, -3.6f);
	// The wrist: over the stub of the folded fist, as far as the gauntlet's cuff
	KM::FMD W;
	KM::Ellipsoid(W, 4.9f, 5.4f, 5.5f, 0.75f, 0.75f, 16, 10, KM::Xf(FVector::ZeroVector));   // (as wide as the cuff's opening)
	const FQuat WQ = FRotationMatrix::MakeFromX(FVector(0.64f, 0.77f, 0.f)).ToQuat();
	const FVector WAt(9.5f, -8.5f, 0.f);
	const int32 Base = Out.V.Num();
	for (const FVector& V : W.V) Out.V.Add(WAt + WQ.RotateVector(V));
	for (const int32 I : W.I) Out.I.Add(Base + I);
}

// Everyone sees it: the weapon goes away and a bottle in the potion's colour comes into the right hand (the bow's hand is the left, so it stays).
void AKnightCharacter::MulticastDrink_Implementation(uint8 Type, bool bStart)
{
	// (the player's own machine started it the moment he pressed the key: the server's echo of that start must not start the clock again, or restart one he has already cut short)
	if (bStart && bDrinkPredicted && !HasAuthority() && IsLocallyControlled()) { bDrinkPredicted = false; return; }
	if (bStart) LastDrinkStart = GetWorld()->GetTimeSeconds();
	bDrinking = bStart;
	DrinkT = 0.f;
	DrinkType = Type;
	if (GetNetMode() == NM_DedicatedServer) return;
	if (bStart && !BottleMesh)
		if (UStaticMesh* Flask = JeebProps::PotionMesh())
		{
			DrinkGroup = NewObject<USceneComponent>(this);
			DrinkGroup->SetupAttachment(GetRootComponent());
			DrinkGroup->RegisterComponent();
			BottleMesh = NewObject<UStaticMeshComponent>(this);
			BottleMesh->SetupAttachment(DrinkGroup);
			BottleMesh->SetStaticMesh(Flask);
			BottleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BottleMesh->SetRelativeTransform(FTransform(FQuat::Identity, FVector(0.f, 0.f, -BottleGripZ), FVector(BottleScale)));   // (the model's cork is along -Z: the hand round its belly, the neck past the thumb)
			BottleMesh->RegisterComponent();
			KM::FMD Hand;
			BuildOpenHand(Hand);
			DrinkHand = MakeMeshComp(DrinkGroup, KnightDesign::P_WristR, Hand);
			DrinkHand->SetRelativeLocation(FVector::ZeroVector);
		}
	if (BottleMesh)
	{
		if (bStart)
		{
			JeebProps::Tint(BottleMesh, JeebItems::PotionRow(FMath::Clamp<int32>(Type, 0, JeebItems::NumPotions() - 1)).Color, 0.f, 0.15f);
			// (in the gauntlets' metal, or bare mail with none on. The suit's own texture is dark paint lifted and tinted, so the same tier colour on a plain surface has to be darkened to match it: HandPaint, by eye against tin and emerald)
			static constexpr float HandPaint = 0.36f;
			const int32 Tier = Anim ? Anim->WornTier(JeebAnim::Piece_Arms) : -1;
			UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(DrinkHand->GetMaterial(0));
			if (!M) M = DrinkHand->CreateDynamicMaterialInstance(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Metal.M_Metal")));
			if (M)
			{
				M->SetVectorParameterValue(TEXT("Color"), Tier >= 0 ? JeebItems::Palette(Tier, 0) * HandPaint : FLinearColor(0.16f, 0.17f, 0.18f));
				M->SetScalarParameterValue(TEXT("Metallic"), Tier >= 0 ? JeebItems::TierMetal(Tier) * 0.75f : 0.5f);
				M->SetScalarParameterValue(TEXT("Roughness"), Tier >= 0 ? JeebItems::TierRough(Tier) : 0.6f);
			}
		}
		BottleMesh->SetVisibility(bStart);
		DrinkHand->SetVisibility(bStart);
	}
	if (!JeebItems::IsDrawnBow(SelectedKind()))
	{
		for (UProceduralMeshComponent* C : HeldComps) if (C) C->SetVisibility(!bStart);
		for (UStaticMeshComponent* C : HeldStatics) if (C) C->SetVisibility(!bStart);
	}
}

// (owner 2026-11-05) A small flask on the back left of his belt, the other side from the quiver, in the potion's colour, while the potion slot holds something
void AKnightCharacter::UpdateBeltFlask()
{
	const int32 Id = (IsLocallyControlled() || GetNetMode() == NM_Standalone) ? (Skills ? Skills->PotionSlot.Id : 0) : LookPotion;
	const bool bShow = !bStowed && !bDrinking && Id != 0 && JeebItems::IsPotion(Id) && Anim && Anim->WornTier(JeebAnim::Piece_Chest) >= 0;   // (the belt is part of the chest plate: with that off there is nothing to hang it from)
	if (!BeltFlask && bShow)
		if (UStaticMesh* Flask = JeebProps::PotionMesh())
		{
			BeltFlask = NewObject<UStaticMeshComponent>(this);
			BeltFlask->SetupAttachment(GetRootComponent());
			BeltFlask->SetStaticMesh(Flask);
			BeltFlask->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BeltFlask->RegisterComponent();
			// (what holds it there: a leather collar round the neck and a strap up to the belt. In the flask's frame: -Z towards the cork, +X and -Y towards his middle.)
			UMaterialInterface* Col = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"));
			auto Leather = [&](const TCHAR* Shape, const FTransform& At)
			{
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
				C->SetupAttachment(BeltFlask);
				C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Shape));
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetCastShadow(false);
				C->SetRelativeTransform(At);
				C->RegisterComponent();
				if (Col) if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, Col)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.11f, 0.055f, 0.03f));
			};
			Leather(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FTransform(FQuat::Identity, FVector(0.f, 0.f, -1.5f), FVector(0.052f, 0.052f, 0.022f)));
			const FVector From(1.6f, -1.6f, -1.5f), To(6.5f, -6.5f, -9.f);
			Leather(TEXT("/Engine/BasicShapes/Cube.Cube"), FTransform(FRotationMatrix::MakeFromZX((To - From).GetSafeNormal(), FVector(1.f, 1.f, 0.f)).ToQuat(), (From + To) * 0.5f, FVector(0.012f, 0.03f, float((To - From).Size()) / 100.f)));
		}
	if (!BeltFlask) return;
	BeltFlask->SetVisibility(bShow, true);
	if (!bShow) return;
	if (Id != BeltFlaskId)
	{
		BeltFlaskId = Id;
		JeebProps::Tint(BeltFlask, JeebItems::PotionRow(FMath::Clamp<int32>(JeebItems::PotionType(Id), 0, JeebItems::NumPotions() - 1)).Color, 0.f, 0.15f);
	}
	if (Anim) BeltFlask->SetWorldTransform(Anim->BeltFlaskWorld());
}

void AKnightCharacter::TickDrink(float Dt)
{
	if (!bDrinking) return;
	DrinkT += Dt;
	const float Takes = JeebTune::V().DrinkTime;
	if (!IsNetMode(NM_DedicatedServer) && !JeebItems::IsDrawnBow(SelectedKind()))   // (a weapon changed meanwhile is rebuilt showing)
	{
		for (UProceduralMeshComponent* C : HeldComps) if (C) C->SetVisibility(false);
		for (UStaticMeshComponent* C : HeldStatics) if (C) C->SetVisibility(false);
	}
	// A roll or anything else that takes over stops it: he keeps the potion (blows and deaths stop it in Hurt and Die)
	const bool bStop = bAttacking || bKnockedDown || StunLeft > 0.f || bRolling || bSliding || bClimbing || bMounted || Bow != EBow::None;
	if (!HasAuthority())
	{
		// (the player's own machine keeps its own clock, so its speed cap ends with the server's; others wait for the server, and never stay stuck holding a bottle)
		if (IsLocallyControlled() ? (bStop || DrinkT >= Takes) : DrinkT > Takes + 3.f) MulticastDrink_Implementation(DrinkType, false);
		return;
	}
	const int32 T = DrinkType;
	FInvSlot* P = Skills ? &Skills->PotionSlot : nullptr;
	const bool bHasIt = P && P->Id != 0 && JeebItems::IsPotion(P->Id) && JeebItems::PotionType(P->Id) == T && P->Count > 0;   // (it may have been moved out of the slot meanwhile)
	if (bStop || !bHasIt) { MulticastDrink(DrinkType, false); return; }
	if (DrinkT < Takes) return;
	ApplyPotion(T);
	if (!IsLocallyControlled() && GetNetMode() != NM_Standalone) ClientDrank(static_cast<uint8>(T));   // the player's machine runs the effect too (speed, the bigger bar)
	if (--P->Count <= 0) *P = FInvSlot();
	Skills->Say(2.5f, FColor(170, 255, 170), FString::Printf(TEXT("You drink a %s."), JeebItems::PotionName(T)));
	MulticastDrink(DrinkType, false);
}

// ---------------- Tick steps (see AKnightCharacter::Tick) ----------------
// A swing landing and ending (and the follow-up swing), and the spear's charge
void AKnightCharacter::TickAttack(float Dt)
{
	TickSmash(Dt);
	if (StunLeft > 0.f) StunLeft = FMath::Max(0.f, StunLeft - Dt);   // (hitstun wears off: ApplyReact)
	if (ShotSweepAt > 0.f && GetWorld()->GetTimeSeconds() >= ShotSweepAt && !bAttacking) { ShotSweepAt = -1.f; StartAttack(EAtkStyle::Sweep, 1, 0.f); }   // (test pictures)
	if (bShotFace && ShotJumpFrom > -1.0e8f && GetCharacterMovement()->IsFalling())   // (test pictures: how high the jump goes)
		UE_LOG(LogTemp, Log, TEXT("JEEBJUMP up %.1f vz %.0f t %.3f"), GetActorLocation().Z - ShotJumpFrom, GetCharacterMovement()->Velocity.Z, GetWorld()->GetTimeSeconds() - ShotJumpT);
	// The dash slides on after its stab is out instead of stopping dead (owner, 2026-11-02): for the whole dash the ground brakes
	// him steadily (no friction, DashSlideBrake), so he reaches the stab about as far as before and skids on about 40 cm. The player's machine and the server
	// both brake the same way, so the slide is predicted like any walk.
	{
		const bool bSlide = IsDashing();
		if (bSlide != bDashSliding)
		{
			UCharacterMovementComponent* M = GetCharacterMovement();
			bDashSliding = bSlide;
			M->bUseSeparateBrakingFriction = bSlide;
			M->BrakingFriction = 0.f;
			if (bSlide) { SavedBrakeDecel = M->BrakingDecelerationWalking; M->BrakingDecelerationWalking = DashSlideBrake; }
			else M->BrakingDecelerationWalking = SavedBrakeDecel;
		}
	}
	if (bShotFace && bAttacking && AtkStyle == EAtkStyle::Dash)   // (test pictures: how far the dash carries him)
		UE_LOG(LogTemp, Log, TEXT("JEEBDASH t %.2f at %s speed %.0f"), AttackT, *GetActorLocation().ToCompactString(), GetVelocity().Size2D());
	if (bAttacking)
	{
		// A sword hit keeps facing the way it started (as in Smash): sliding along a wall or being steered never turns him
		// mid-swing. Not in strafe mode or the shield stance: there he keeps turning with the camera, swing or not.
		// Charging a smash he turns as he walks; the swipe goes the way he faces when it is let go.
		if (AtkStyle == EAtkStyle::Slash && JeebItems::FightsLikeSword(SelectedKind()) && ((!bStrafeMode && !bBlockHeld) || SmashLocked()))
		{
			if (Smash == ESmash::Charging) AttackYaw = GetActorRotation().Yaw;
			else SetActorRotation(FRotator(0.f, AttackYaw, 0.f));
		}
		// (the kick and the low sweep turn as a jab does, their hit boxes with him: owner 2026-11-06. The dash is a straight lunge and stays on its line.)
		if (AtkStyle == EAtkStyle::Dash || ((AtkStyle == EAtkStyle::Kick || AtkStyle == EAtkStyle::Sweep) && !bStrafeMode && !bBlockHeld)) SetActorRotation(FRotator(0.f, AttackYaw, 0.f));
		// The smash attack: hit 1 waits at its pull-back while deciding and charging, and the end lag waits at the end of the swipe
		float Step = Dt;
		if (Smash == ESmash::Deciding || Smash == ESmash::Charging) Step = FMath::Clamp(SmashApexT * AttackDur - AttackT, 0.f, Dt);
		else if (Smash == ESmash::EndLag && SmashT < SmashEndPause) Step = 0.f;
		else if (Smash == ESmash::Rebound) Step = (SmashT < ReboundBack) ? -Dt * 0.12f * AttackDur / ReboundBack : 0.f;   // bounced off a shield: the blade springs back the way it came, then he is stuck
		if (AtkStyle == EAtkStyle::Kick && bKickLanded) Step *= KickLandedSpeed;   // (a kick that landed: less end lag)
		AttackT = FMath::Max(0.f, AttackT + Step);
		if (Smash == ESmash::Swing && AttackT >= SmashPauseT * AttackDur) { AttackT = SmashPauseT * AttackDur; SetSmash(ESmash::EndLag, SmashCharge, false); }
		const float HitFrac = (AtkStyle == EAtkStyle::Slash) ? 0.43f : (AtkStyle == EAtkStyle::Thrust ? 0.5f : 0.3f);
		if (!bAttackHit && AttackT >= AttackDur * HitFrac)
		{
			bAttackHit = true;
			AttackLands();
		}
		if (AttackT >= AttackDur || (Smash == ESmash::Rebound && SmashT >= ReboundDur))
		{
			bAttacking = false;
			// (a rebound, a kick and a sweep end like a smash attack: only a late press comes out)
			const bool bWasSmash = bSmashSwing || Smash == ESmash::Rebound || AtkStyle == EAtkStyle::Kick || AtkStyle == EAtkStyle::Sweep || AtkStyle == EAtkStyle::Dash;
			Smash = ESmash::None; bSmashSwing = false;
			const float Now = GetWorld()->GetTimeSeconds();
			LastSwingEnd = Now;
			LastSwingDir = bWasSmash ? 0 : AttackDir;   // (after a smash the next press is hit 1 again)
			LastStyle = AtkStyle;
			// Clicked during the swing: go straight into the next one. The sword alternates its two hits (forehand, backhand)
			// for as long as you click, and any click during a swing counts, so one click = one swing. Other blades chain two,
			// and the click must come in the last 0.3 s.
			if ((AtkStyle == EAtkStyle::Slash || AtkStyle == EAtkStyle::Kick || AtkStyle == EAtkStyle::Sweep || AtkStyle == EAtkStyle::Dash) && !IsUIOpen())
			{
				if (bWasSmash)
				{
					// Smash's buffer: a press in the last moments of the end lag comes out as soon as it is over (hit 1; still held,
					// it can be another smash). Earlier presses are lost, so the end lag can't be skipped.
					if (Now - QueuedClick <= 0.17f && JeebItems::FightsLikeSword(SelectedKind()))
					{
						StartAttack(EAtkStyle::Slash, JabFirst, 0.f);
						if (bAttackDown && IsLocallyControlled()) SetSmash(ESmash::Deciding);
					}
				}
				else if (JeebItems::FightsLikeSword(SelectedKind()))
				{
					// (one buffered press per swing, Smash style; the two hits alternate for as long as you keep clicking)
					const int32 Next = (AttackDir == JabFirst) ? JabSecond : JabFirst;
					if (QueuedJabs > 0) StartAttack(EAtkStyle::Slash, Next, 0.f);   // (the buffered press; StartAttack empties the buffer)
				}
				else if (AtkStyle == EAtkStyle::Slash && AttackDir > 0 && Now - QueuedClick <= 0.3f) StartAttack(EAtkStyle::Slash, -1, 0.f);
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

// ---------------- The sword's smash attack (Smash Ultimate style) ----------------
// Phases: Deciding (hit 1 started from a press, waiting at its pull-back while the button is held), Charging (held past
// SmashHoldDelay), Swing (let go: hit 1's swipe from the pull-back), EndLag (a pause at the end of the swipe, then the rest of
// hit 1's recovery; always the same length, however charged). The player's machine decides from the button and tells the
// server, which checks it and shows it to everyone else; the timed changes (end lag, the automatic release) every machine
// makes by itself.
void AKnightCharacter::SetSmash(ESmash P, float Charge, bool bTell)
{
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBSMASH phase %d after %.2f s, charge %.2f, swing T %.2f"), (int32)P, SmashT, Charge, AttackT / FMath::Max(AttackDur, 0.01f));   // (test pictures)
	Smash = P;
	SmashT = 0.f;
	if (P == ESmash::Charging) SmashCharge = 0.f;
	if (P == ESmash::Swing)
	{
		bSmashSwing = true;
		// (hit 1 alone got quicker in Tuning.ini, owner 2026-11-06; the smash shares its wind-up but keeps its own swipe and end lag: the same share of a longer swing)
		if (AttackDir == JabFirst && AtkStyle == EAtkStyle::Slash)
		{
			const float Longer = 0.8f / FMath::Max(JeebTune::V().Jab1Share, 0.1f);
			AttackT *= Longer;
			AttackDur *= Longer;
		}
		SmashCharge = FMath::Clamp(Charge, 0.f, 1.f);
		HitThisSwing.Reset();   // (a new move: it can hit everyone again)
		bHitPrevValid = false;
		AttackYaw = GetActorRotation().Yaw;   // the swipe goes the way he faces now
		if ((IsLocallyControlled() || HasAuthority()) && !GetCharacterMovement()->IsFalling())
			LaunchCharacter(GetActorForwardVector().GetSafeNormal2D() * 240.f, true, false);   // a step into the cut (the player's machine and the server together)
	}
	if (!bTell || GetNetMode() == NM_Standalone) return;
	if (IsLocallyControlled() && !HasAuthority()) ServerSmash(static_cast<uint8>(P), SmashCharge);
	else if (HasAuthority()) MulticastSmash(static_cast<uint8>(P), SmashCharge);
}

bool AKnightCharacter::SmashInputOk(ESmash P) const
{
	const bool bHit1 = bAttacking && AtkStyle == EAtkStyle::Slash && AttackDir == JabFirst && JeebItems::FightsLikeSword(SelectedKind()) && !bSmashSwing;
	switch (P)
	{
	case ESmash::Deciding: return bHit1 && Smash == ESmash::None && AttackT <= SmashApexT * AttackDur + 0.01f;   // (not once the cut is under way)
	case ESmash::None:     return Smash == ESmash::Deciding;
	case ESmash::Charging: return bHit1 && Smash == ESmash::Deciding;
	case ESmash::Swing:    return Smash == ESmash::Charging;
	default:               return false;   // (the end lag is timed by every machine itself)
	}
}

void AKnightCharacter::ServerSmash_Implementation(uint8 Phase, float Charge)
{
	if (Phase > static_cast<uint8>(ESmash::EndLag)) return;
	const ESmash P = static_cast<ESmash>(Phase);
	if (!SmashInputOk(P)) return;
	// (never more charge than the server itself has seen build up, give or take a little for the network)
	SetSmash(P, FMath::Min(Charge, SmashT / SmashChargeTime + 0.1f));
}

void AKnightCharacter::MulticastSmash_Implementation(uint8 Phase, float Charge)
{
	if (HasAuthority() || IsLocallyControlled() || Phase > static_cast<uint8>(ESmash::EndLag)) return;   // (they already know)
	const ESmash P = static_cast<ESmash>(Phase);
	if (SmashInputOk(P)) SetSmash(P, Charge, false);
}

float AKnightCharacter::SmashTimeLeft() const
{
	if (bKnockedDown) return 1.f;
	float Left = StunLeft;   // (hitstun, too)
	if (bAttacking && (AtkStyle == EAtkStyle::Kick || AtkStyle == EAtkStyle::Sweep || AtkStyle == EAtkStyle::Dash) && Smash != ESmash::Rebound) Left = FMath::Max(Left, (AttackDur - AttackT) / ((AtkStyle == EAtkStyle::Kick && bKickLanded) ? KickLandedSpeed : 1.f));   // (a kick's, a sweep's or a dash's end lag)
	if (Smash == ESmash::Rebound) Left = FMath::Max(Left, ReboundDur - SmashT);
	if (Smash == ESmash::Swing || Smash == ESmash::EndLag)
		Left = FMath::Max(Left, FMath::Max(0.f, AttackDur - AttackT) + (Smash == ESmash::EndLag ? FMath::Max(0.f, SmashEndPause - SmashT) : SmashEndPause));
	return FMath::Max(0.f, Left);
}

// ---------------- The kick, and being knocked down by one ----------------
void AKnightCharacter::StartKick()
{
	if (IsUIOpen() || bPanelCursor || bAttacking || bRolling || bSliding || bClimbing || bSitting || bMounted || bKnockedDown || StunLeft > 0.f) return;
	if (Smash != ESmash::None || ActiveNode.IsValid() || GetCharacterMovement()->IsFalling()) return;
	CancelDraw();   // (kicking with a bow drawn: the string comes down)
	StartAttack(EAtkStyle::Kick, 1, 0.f);
}

// The low sweep (wheel down): sword only. It can cut short a kick that has landed (the combo); pressed earlier in the kick, it
// waits and comes out the moment the kick lands
void AKnightCharacter::StartSweep()
{
	if (IsUIOpen() || bPanelCursor || bRolling || bSliding || bClimbing || bSitting || bMounted || bKnockedDown || StunLeft > 0.f) return;
	if (bAttacking && !(IsKicking() && bKickLanded))
	{
		if (IsKicking()) QueuedSweep = GetWorld()->GetTimeSeconds();
		return;
	}
	if (Smash != ESmash::None || ActiveNode.IsValid() || GetCharacterMovement()->IsFalling() || !JeebItems::FightsLikeSword(SelectedKind())) return;
	StartAttack(EAtkStyle::Sweep, 1, 0.f);
}

// (server) This kick hit someone: the rest of it is quicker and the sweep may cut it short. Everyone is told (the player's own
// machine needs it to let the sweep out early).
void AKnightCharacter::KickLanded()
{
	if (!HasAuthority() || !IsKicking() || bKickLanded) return;
	ApplyKickLanded();
	if (GetNetMode() != NM_Standalone) MulticastKickLanded();
}

void AKnightCharacter::MulticastKickLanded_Implementation()
{
	if (!HasAuthority()) ApplyKickLanded();
}

void AKnightCharacter::ApplyKickLanded()
{
	if (!IsKicking()) return;
	bKickLanded = true;
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBKICK landed at %.2f (kick %.2f s in)"), GetWorld()->GetTimeSeconds(), AttackT * AttackDur);
	if (IsLocallyControlled() && GetWorld()->GetTimeSeconds() - QueuedSweep <= 0.35f) { QueuedSweep = -10.f; StartSweep(); }   // (the buffered wheel-down)
}

// The dash attack's end lag: a smash attack's (the pause at the end of the swipe, then the rest of hit 1's recovery)
float AKnightCharacter::DashLag() const
{
	const float Hit1 = JeebItems::WeaponBase(JeebItems::FightsLikeSword(SelectedKind()) ? SelectedKind() : JeebItems::K_Sword).Cooldown * 0.8f / SwingSpeedScale();
	return SmashEndPause + (1.f - SmashPauseT) * Hit1;
}

float AKnightCharacter::DashU() const
{
	if (AttackT < 0.3f) return 0.45f * AttackT / 0.3f;
	return FMath::Min(1.f, 0.45f + 0.55f * (AttackT - 0.3f) / FMath::Max(AttackDur - 0.3f, 0.01f));
}

bool AKnightCharacter::MoveLocked() const
{
	return SmashLocked() || bKnockedDown || StunLeft > 0.f || (bAttacking && (AtkStyle == EAtkStyle::Kick || AtkStyle == EAtkStyle::Sweep || AtkStyle == EAtkStyle::Dash));
}

// Can't turn to the camera: a smash's swipe and end lag, lying down, hitstun, the dash's lunge (a kick and a sweep stop his feet, not his turning)
bool AKnightCharacter::TurnLocked() const
{
	return SmashLocked() || bKnockedDown || StunLeft > 0.f || (bAttacking && AtkStyle == EAtkStyle::Dash);
}

bool AKnightCharacter::BladeComing() const
{
	if (!bAttacking || !JeebItems::FightsLikeSword(SelectedKind()) || Smash == ESmash::Rebound) return false;
	const float T = AttackT / FMath::Max(AttackDur, 0.01f);
	if (AtkStyle == EAtkStyle::Sweep) return T <= SweepActiveTo;
	if (AtkStyle == EAtkStyle::Dash) return AttackT <= DashActiveTo;
	return AtkStyle == EAtkStyle::Slash && Smash != ESmash::EndLag && T <= ((AttackDir == JabSecond) ? 0.74f : 0.8f);   // (to the end of its hit frames)
}

// ---------------- Hitstun and launches (fighting-game style) ----------------
// The server lands a clean hit and says how it moves him (TakeReact); everyone plays it (ApplyReact). DI: the player's machine
// sends the direction held (NoteDI, ServerDI); the server bends the launch by it.
void AKnightCharacter::TakeReact(AActor* From, const JeebHit::FReact& R)
{
	if (!HasAuthority() || Health <= 0.f || bMounted || bClimbing || bSitting) return;
	if (GetWorld()->GetTimeSeconds() < InvulnUntil) return;   // (getting up: safe)
	const FVector Away = (GetActorLocation() - (From ? From->GetActorLocation() : GetActorLocation() - GetActorForwardVector())).GetSafeNormal2D();
	if (R.bDrop) { KnockDownBy(From, true); return; }   // the low sweep: down on the spot, then up or a roll (GetUp)
	if (R.bLaunch)
	{
		const FVector Vel = JeebHit::LaunchVelocity(Away, HeldDI());
		ApplyReact(0.f, Vel, true);
		if (GetNetMode() != NM_Standalone) MulticastReact(0.f, Vel, true);
		return;
	}
	if (bKnockedDown || R.Stun <= 0.f) return;   // (on the ground or flying: the blow lands, but he stays down)
	ApplyReact(R.Stun, Away * R.Push, false);
	if (GetNetMode() != NM_Standalone) MulticastReact(R.Stun, Away * R.Push, false);
}

void AKnightCharacter::MulticastReact_Implementation(float Stun, FVector Vel, bool bLaunch)
{
	if (!HasAuthority()) ApplyReact(Stun, Vel, bLaunch);
}

void AKnightCharacter::ApplyReact(float Stun, const FVector& Vel, bool bLaunch)
{
	// whatever he was doing is cut short
	bAttacking = false; bCharging = false; QueuedJabs = 0; QueuedClick = -10.f;
	Smash = ESmash::None; bSmashSwing = false; SmashT = 0.f;
	bBlocking = false; bSprinting = false;
	if (bRolling) { bRolling = false; GetCharacterMovement()->bOrientRotationToMovement = bSavedOrient; }
	if (bLaunch)
	{
		// up into the air on his back; he lands lying down (TickKnock), as after a kick. Already down: he stays flat
		KnockT = bKnockedDown ? KnockFall : 0.f;
		bKnockedDown = true;
		KnockPhase = 0;
		bCrouched = false;
		if (IsLocallyControlled() || HasAuthority()) LaunchCharacter(Vel, true, true);
		return;
	}
	StunLeft = FMath::Max(StunLeft, Stun);
	if ((IsLocallyControlled() || HasAuthority()) && !GetCharacterMovement()->IsFalling()) LaunchCharacter(Vel, true, false);   // pushed back
}

void AKnightCharacter::NoteDI(const FVector& Dir)
{
	DIDir = FVector(Dir.X, Dir.Y, 0.f).GetClampedToMaxSize(1.f);
	const float Now = GetWorld()->GetTimeSeconds();
	DITime = Now;
	if (IsLocallyControlled() && !HasAuthority() && Now - DISent > 0.08f && !DIDir.IsNearlyZero())
	{
		DISent = Now;
		ServerDI(DIDir.GetSafeNormal());
	}
}

void AKnightCharacter::ServerDI_Implementation(FVector_NetQuantizeNormal Dir)
{
	DIDir = FVector(Dir.X, Dir.Y, 0.f).GetClampedToMaxSize(1.f);
	DITime = GetWorld()->GetTimeSeconds();
}

FVector AKnightCharacter::HeldDI() const
{
	return (GetWorld()->GetTimeSeconds() - DITime < 0.25f) ? DIDir : FVector::ZeroVector;   // (let go: the player's machine stops sending)
}

// (server) A kick (or a blow during his own kick): he goes down on his back, pushed away from whoever did it
void AKnightCharacter::KnockDownBy(AActor* From, bool bDrop)
{
	if (!HasAuthority() || bKnockedDown || Health <= 0.f || bMounted || bClimbing || bSitting) return;
	if (bDrop) { MulticastKnock(true, 1); return; }   // (Opt 1: dropped where he stands)
	if (bBlocking && !bGuardBroken && GuardMax > 0.f)   // a kick on a raised shield crushes half its guard (balance pass 2026-10-31)
	{
		GuardPool -= 0.5f * GuardMax;
		LastGuardHitTime = GetWorld()->GetTimeSeconds();
		if (GuardPool < 1.f) { GuardPool = 0.f; bGuardBroken = true; if (Skills) Skills->Say(2.5f, FColor::Orange, TEXT("Guard broken! Your shield is down until it recovers.")); }
	}
	MulticastKnock(true, 0);   // (runs here too)
}

void AKnightCharacter::MulticastKnock_Implementation(bool bDown, uint8 Opt)
{
	if (!bDown)
	{
		if (!HasAuthority() && !IsLocallyControlled()) ApplyGetUp(Opt);   // (others see him get up the way he chose)
		return;
	}
	bKnockedDown = true;
	KnockPhase = 0;
	KnockT = 0.f;
	bAttacking = false; bCharging = false; QueuedJabs = 0;
	Smash = ESmash::None; bSmashSwing = false; SmashT = 0.f;
	bBlocking = false; bCrouched = false; bSprinting = false;
	if (bRolling) { bRolling = false; GetCharacterMovement()->bOrientRotationToMovement = bSavedOrient; }
	if (IsLocallyControlled() || HasAuthority())
	{
		if (Opt == 1) GetCharacterMovement()->StopMovementImmediately();   // swept: his feet go and he drops straight down on the spot (a zero launch would be ignored)
		else LaunchCharacter(-GetActorForwardVector().GetSafeNormal2D() * 300.f + FVector(0.f, 0.f, 100.f), true, true);   // knocked back off his feet (landing before KnockFall is over)
	}
}

void AKnightCharacter::GetUp(uint8 Opt)
{
	if (!bKnockedDown || KnockPhase != 0 || KnockT < KnockFall || GetCharacterMovement()->IsFalling()) return;   // (he has to land first)
	if (!HasAuthority()) ServerGetUp(Opt);
	ApplyGetUp(Opt);
	if (HasAuthority() && GetNetMode() != NM_Standalone) MulticastKnock(false, Opt);
}

void AKnightCharacter::ServerGetUp_Implementation(uint8 Opt)
{
	if (!bKnockedDown || KnockPhase != 0 || Opt > 2) return;
	if (KnockT < KnockFall - 0.2f) return;   // (not before he has fallen: the player's own check, with a little room for the network)
	ApplyGetUp(Opt);
	MulticastKnock(false, Opt);
}

void AKnightCharacter::ApplyGetUp(uint8 Opt)
{
	if (!bKnockedDown || KnockPhase != 0) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Opt == 0)   // straight up (KnockRise s), safe from blows while he rises
	{
		KnockPhase = 1;
		KnockT = 0.f;
		InvulnUntil = FMath::Max(InvulnUntil, Now + KnockRise + 0.1f);
		return;
	}
	// A roll away to his left (1) or right (2): the roll itself (StartRoll) carries him up out of it
	bKnockedDown = false;
	KnockPhase = 0;
	InvulnUntil = FMath::Max(InvulnUntil, Now + 0.5f);
	RollCooldown = 0.f;
	Stamina = FMath::Max(Stamina, 20.f);
	if (IsLocallyControlled())   // (the player's machine rolls and tells the server; others see the roll in his move flags)
	{
		PendingRollDir = GetActorRightVector().GetSafeNormal2D() * (Opt == 1 ? -1.f : 1.f);
		StartRoll();
		PendingRollDir = FVector::ZeroVector;
	}
}

void AKnightCharacter::TickKnock(float Dt)
{
	if (!bKnockedDown) { KnockAlpha = FMath::FInterpConstantTo(KnockAlpha, 0.f, Dt, 4.f); return; }   // (rolled out of it: eases into the roll)
	KnockT += Dt;
	if (KnockPhase == 1)   // getting up
	{
		KnockAlpha = 1.f - FMath::Clamp(KnockT / KnockRise, 0.f, 1.f);
		if (KnockT >= KnockRise) { bKnockedDown = false; KnockPhase = 0; KnockAlpha = 0.f; }
		return;
	}
	if (GetCharacterMovement()->IsFalling()) KnockT = FMath::Min(KnockT, KnockFall);   // (launched or falling: lying back in the air; the time to get up starts once he lands)
	KnockAlpha = FMath::Clamp(KnockT / KnockFall, 0.f, 1.f);
	// He gets up by himself if nothing is pressed (the player's machine decides; the server and others only if it never says)
	if (IsLocallyControlled()) { if (KnockT >= KnockAuto) GetUp(0); }
	else if (HasAuthority()) { if (KnockT >= KnockAuto + 1.f) { ApplyGetUp(0); MulticastKnock(false, 0); } }
	else if (KnockT >= KnockAuto + 3.f) ApplyGetUp(0);
}

void AKnightCharacter::TickSmash(float Dt)
{
	if (Smash == ESmash::None) return;
	const bool bSweepRebound = AtkStyle == EAtkStyle::Sweep && Smash == ESmash::Rebound;   // (a sweep bounces off a shield too)
	if (!bAttacking || (AtkStyle != EAtkStyle::Slash && !bSweepRebound) || (AttackDir != JabFirst && Smash != ESmash::Rebound) || !JeebItems::FightsLikeSword(SelectedKind()))
	{
		Smash = ESmash::None; bSmashSwing = false;   // (cut short: a weapon change, a death, a ladder ...)
		return;
	}
	SmashT += Dt;
	const bool bMine = IsLocallyControlled();
	const bool bHeld = bAttackDown || (bShotFace && ShotSmashHold >= 0.f);
	if (Smash == ESmash::Deciding)
	{
		if (bMine && (!bHeld || bRolling)) SetSmash(ESmash::None);   // just hit 1 after all (a jump does the same: Jump)
		else if (bMine && SmashT >= SmashHoldDelay) SetSmash(ESmash::Charging);
		else if (!bMine && HasAuthority() && SmashT > 1.5f) SetSmash(ESmash::None);   // (no word from the player: just hit 1)
	}
	else if (Smash == ESmash::Charging)
	{
		SmashCharge = FMath::Clamp(SmashT / SmashChargeTime, 0.f, 1.f);   // builds for a second, then stays full
		if (bShotFace && ShotSmashHold >= 0.f && SmashT >= ShotSmashHold) SetSmash(ESmash::Swing, SmashCharge);
		else if (SmashT >= SmashChargeTime + SmashHoldMax) SetSmash(ESmash::Swing, 1.f, bMine);   // held too long: it goes by itself
	}
}

// ---------------- Bouncing off a shield ----------------
// The server sees a hit box of this swing strike a raised shield: the swing is over, the blade springs back and the knight is
// stuck for a fixed time (longer after a smash attack). Everyone is told, the player's own machine too (its swing may already
// have ended there: it gets the same stuck time from its last swing's follow-through).
void AKnightCharacter::StartRebound()
{
	const float Dur = (bSmashSwing || AtkStyle == EAtkStyle::Dash) ? ReboundSmash : ReboundJab;   // (the dash attack rebounds like a smash)
	BeginRebound(Dur);
	if (GetNetMode() != NM_Standalone) MulticastRebound(Dur);
}

void AKnightCharacter::MulticastRebound_Implementation(float Dur)
{
	if (!HasAuthority()) BeginRebound(Dur);
}

void AKnightCharacter::BeginRebound(float Dur)
{
	if (!JeebItems::FightsLikeSword(SelectedKind())) return;
	if (!bAttacking)   // (online: the swing had already finished on the player's machine)
	{
		bAttacking = true; AtkStyle = EAtkStyle::Slash; bAttackHit = true;
		AttackDir = (LastSwingDir == JabSecond) ? JabSecond : JabFirst;
		AttackT = 0.6f * AttackDur;
	}
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBSMASH rebound %.2f s, swing T %.2f"), Dur, AttackT / FMath::Max(AttackDur, 0.01f));   // (test pictures)
	Smash = ESmash::Rebound;
	SmashT = 0.f;
	ReboundDur = Dur;
	QueuedJabs = 0;   // (the combo is broken: a buffered press is lost unless it comes in the last moment)
	QueuedClick = -10.f;
	if ((IsLocallyControlled() || HasAuthority()) && !GetCharacterMovement()->IsFalling())
		LaunchCharacter(-GetActorForwardVector().GetSafeNormal2D() * 260.f, true, false);   // knocked back a step
}

bool AKnightCharacter::GetShieldWedge(JeebHit::FShieldWedge& Out) const
{
	if (!bBlocking || !ShieldActive() || Health <= 0.f) return false;
	if (!Anim || !Anim->IsReady()) return false;
	Out = Anim->ShieldWedge(GetActorLocation(), GetActorForwardVector());   // (up to the top of his helm, and where the raised shield is)
	return true;
}

// ---------------- Hit boxes and hurt boxes (Smash style, HitBoxes.h) ----------------
// A blade's hit boxes: spheres along it, every BladeBoxStep cm from the tip down to the fist, the tip first (the lowest ID wins when two touch the same target), live only
// in each hit's active frames and swept from where each was the frame before. The server lands the blows from the pose, which every machine makes the same way.
// How far they reach comes from what is in the hand (WeaponDesign::BladeReach).
static constexpr int32 MaxBladeBoxes = 8;
static constexpr float BladeBoxStep = 22.f;
static constexpr float JabTipR = 10.f, SmashTipR = 13.f;   // (a jab's reach just past the point; a smash and a sweep a little more)
static int32 BladeBoxes(int32 Kind, float TipR, JeebHit::FHitSphere* Out)
{
	const float Reach = WeaponDesign::BladeReach(Kind);
	int32 N = 0;
	for (float A = Reach - 0.5f * TipR; A > 12.f && N < MaxBladeBoxes; A -= BladeBoxStep)
	{
		Out[N] = { N, A, N == 0 ? TipR : TipR - 1.f, 1.f };
		++N;
	}
	return N;
}
static const float& SwordJabDamage = JeebTune::Storage().SwordJabDamage;   // (the jab hits softer than a full swing: it is much quicker, and it loops)
// The smash attack: hit 1's swipe with bigger boxes. Uncharged it hits like a full sword swing; a full charge hits 2.5 times as hard (in between by how long it was charged).
static const float& SwordSmashDamage = JeebTune::Storage().SwordSmashDamage;
static const float& SwordSmashFullMul = JeebTune::Storage().SwordSmashFullMul;
// The jab's hit frames (fractions of the swing): hit 1 from the blade swinging out on his right to the end of its follow-through
// low on his left; hit 2 the same, the other way
static const float& Jab1From = JeebTune::Storage().Jab1From;
static const float& Jab1To = JeebTune::Storage().Jab1To;
static const float& Jab2From = JeebTune::Storage().Jab2From;
static const float& Jab2To = JeebTune::Storage().Jab2To;
// Pushes (cm/s away from the attacker) with each hit's hitstun (StunJab1 ...): hit 1 keeps him close for hit 2; hit 2 sends
// him clear; a smash further the more it was charged
static const float& PushJab1 = JeebTune::Storage().PushJab1;
static const float& PushJab2 = JeebTune::Storage().PushJab2;
static const float& PushSmash = JeebTune::Storage().PushSmash;
static const float& PushSmashFull = JeebTune::Storage().PushSmashFull;

// The kick: four spheres on the kicking (right) leg: the toes, the foot, the shin, the knee. Kick beats shield: it goes straight through a raised shield (its wedge counts as a
// touch too) and knocks anyone it hits down, shield or not. Attack beats kick: a kick into a knight whose sword is coming loses, and the kicker falls instead.
static const JeebHit::FHitSphere KickBoxes[] = { { 0, 0.f, 10.f, 1.f }, { 1, 0.f, 13.f, 1.f }, { 2, 0.f, 11.f, 0.9f }, { 3, 0.f, 11.f, 0.8f } };   // (Along unused)

void AKnightCharacter::TickHitboxes(float Dt)
{
	HitLive.Reset();
	const int32 HeldKind = SelectedKind();
	const bool bSword = bAttacking && (AtkStyle == EAtkStyle::Slash || AtkStyle == EAtkStyle::Sweep || AtkStyle == EAtkStyle::Dash) && JeebItems::FightsLikeSword(HeldKind) && WristR;
	const bool bSweep = bSword && AtkStyle == EAtkStyle::Sweep;
	const bool bDash = bSword && AtkStyle == EAtkStyle::Dash;   // (hits like a smash attack charged DashCharge)
	const bool bKick = bAttacking && AtkStyle == EAtkStyle::Kick && Anim && Anim->IsReady();
	const float T = (bSword || bKick) ? AttackT / FMath::Max(AttackDur, 0.01f) : 0.f;
	const float From = bKick ? KickActiveFrom : bSweep ? SweepActiveFrom : bDash ? DashActiveFrom / FMath::Max(AttackDur, 0.01f) : (AttackDir == JabSecond) ? Jab2From : Jab1From;
	const float To = bKick ? KickActiveTo : bSweep ? SweepActiveTo : bDash ? DashActiveTo / FMath::Max(AttackDur, 0.01f) : (AttackDir == JabSecond) ? Jab2To : Jab1To;
	JeebHit::FHitSphere BladeSet[MaxBladeBoxes];
	const int32 NumBlade = bSword ? BladeBoxes(HeldKind, (bSweep || bDash || bSmashSwing) ? SmashTipR : JabTipR, BladeSet) : 0;
	const JeebHit::FHitSphere* Boxes = bKick ? KickBoxes : BladeSet;
	const int32 NumBoxes = bKick ? UE_ARRAY_COUNT(KickBoxes) : NumBlade;
	const float Power = bKick ? KickDamage : bSweep ? SweepDamage : bDash ? SwordSmashDamage * FMath::Lerp(1.f, SwordSmashFullMul, DashCharge) : bSmashSwing ? SwordSmashDamage * FMath::Lerp(1.f, SwordSmashFullMul, SmashCharge) : SwordJabDamage;
	// what a clean sword hit does to whoever it lands on
	JeebHit::FReact React;
	if (bSweep) React.bDrop = true;   // (owner, 2026-11-02: straight down where he stands, no launch)
	else if (bDash) { React.Stun = FMath::Lerp(StunSmash, StunSmashFull, DashCharge); React.Push = FMath::Lerp(PushSmash, PushSmashFull, DashCharge); }
	else if (bSmashSwing) { React.Stun = FMath::Lerp(StunSmash, StunSmashFull, SmashCharge); React.Push = FMath::Lerp(PushSmash, PushSmashFull, SmashCharge); }
	else if (AttackDir == JabSecond) { React.Stun = StunJab2; React.Push = PushJab2; }
	else { React.Stun = StunJab1; React.Push = PushJab1; }
	if ((bSword || bKick) && T >= From && T <= To && (Smash == ESmash::None || Smash == ESmash::Swing))
	{
		const FTransform W = bKick ? FTransform::Identity : WristR->GetComponentTransform();
		for (int32 i = 0; i < NumBoxes; ++i)
		{
			const JeebHit::FHitSphere& S = Boxes[i];
			FVector Now;
			if (!bKick) Now = W.TransformPosition(KnightDesign::GripPos(false) + KnightDesign::BladeDir(1) * S.Along - KnightDesign::PivotPos(KnightDesign::P_WristR));
			else
			{
				using namespace JeebAnim;
				const FVector Foot = Anim->BoneAt(RFoot), Toe = Anim->BoneAt(RToe), Knee = Anim->BoneAt(RLeg);
				Now = (i == 0) ? Toe + (Toe - Foot).GetSafeNormal() * 5.f : (i == 1) ? (Foot + Toe) * 0.5f : (i == 2) ? (Foot + Knee) * 0.5f : Knee;
			}
			JeebHit::FCapsule C;
			C.A = bHitPrevValid ? HitPrev[i] : Now; C.B = Now; C.R = S.Radius;
			HitLive.Add(C);
			HitPrev[i] = Now;
		}
		bHitPrevValid = true;
		if (bShotFace && bSweep)   // test pictures: how low the sweep's tip runs (log: JEEBSWEEP)
			UE_LOG(LogTemp, Log, TEXT("JEEBSWEEP T %.2f tip %s"), T, *GetActorTransform().InverseTransformPosition(HitLive[0].B).ToCompactString());
		if (HasAuthority() && Skills)
		{
			const int32 Id = Skills->SelectedItem();
			const int32 Tier = (!bKick && JeebItems::IsGear(Id)) ? JeebItems::VariantOf(Id) : 0;   // (a kick is a kick, whatever you hold)
			const int32 DmgKind = bKick ? JeebItems::K_Sword : HeldKind;   // (a kick is a kick, whatever you hold)
			const ESkillType Sk = JeebItems::CombatSkillFor(DmgKind);
			TArray<JeebHit::FCapsule> Hurt;
			// What a hit box meets first along its sweep this frame: the shield's wedge or the body. The body first (from the side
			// or behind) is a clean hit; the wedge first is a block (at the same moment, the shield: Smash checks shields first).
			// Hit: the box that meets it (the lowest ID among those touching the body), and Zone the body part it meets (armour).
			// Returns false if nothing is touched.
			auto Meet = [&](const TArray<JeebHit::FCapsule>& HurtBoxes, const JeebHit::FShieldWedge* Shield, int32& OutHit, bool& bOutShield, uint8& OutZone)
			{
				float FirstWedge = 2.f, FirstBody = 2.f; int32 WedgeBox = -1; OutHit = -1; OutZone = JeebHit::Z_None;
				for (int32 i = 0; i < HitLive.Num(); ++i)
				{
					uint8 Z = JeebHit::Z_None;
					const float Tw = Shield ? JeebHit::FirstTouchWedge(HitLive[i], *Shield) : 2.f;
					const float Tb = JeebHit::FirstTouch(HitLive[i], HurtBoxes, &Z);
					if (Tw < FirstWedge) { FirstWedge = Tw; WedgeBox = i; }
					if (Tb <= 1.f && OutHit < 0) { OutHit = i; OutZone = Z; }
					FirstBody = FMath::Min(FirstBody, Tb);
				}
				bOutShield = WedgeBox >= 0 && FirstWedge <= 1.f && FirstWedge <= FirstBody;
				if (bOutShield) OutHit = WedgeBox;
				return OutHit >= 0;
			};
			bool bOver = false;   // (the move was stopped: a rebound, or a kick that lost)
			for (UHealthComponent* H : UHealthComponent::All())
			{
				if (!H || !H->IsAlive() || H->GetOwner() == this || HitThisSwing.Contains(H)) continue;
				if (FVector::DistSquared(H->GetOwner()->GetActorLocation(), GetActorLocation()) > FMath::Square(450.f)) continue;
				Hurt.Reset();
				JeebHit::HurtOf(H, Hurt);
				JeebHit::FShieldWedge Shield;
				const bool bShieldUp = JeebHit::ShieldOf(H, Shield);
				int32 Hit = -1; bool bOnShield = false; uint8 Zone = JeebHit::Z_None;
				if (!Meet(Hurt, bShieldUp ? &Shield : nullptr, Hit, bOnShield, Zone)) continue;
				HitThisSwing.Add(H);
				bool bCrit = false;
				const int32 Dmg = JeebItems::RollDamage(Skills, DmgKind, Tier, 0, bCrit, Power * Boxes[Hit].DamageMul);
				if (bKick)
				{
					if (H->IsSwinging && H->IsSwinging()) { KnockDownBy(H->GetOwner()); bOver = true; break; }   // attack beats kick: the kicker falls
					if (H->KnockDown) H->KnockDown(this);   // kick beats shield (and knocks anyone down)
					KickLanded();
					DealDamage(H, Dmg, bCrit, Sk, true, bOnShield ? JeebHit::Z_None : Zone);
					continue;
				}
				if (bOnShield)
				{
					const int32 Through = H->ShieldBlock ? H->ShieldBlock(Dmg, this) : 0;   // (the shield takes the whole blow)
					if (Through > 0) DealDamage(H, Through, false, Sk, true);
					StartRebound();   // the blade bounces off: the rest of this swing is lost
					bOver = true;
					break;
				}
				DealDamage(H, Dmg, bCrit, Sk, true, Zone);
				if (H->React && H->IsAlive()) H->React(this, React);
			}
			// Other players' knights: only in MoveLab for now (no PvP yet)
			if (!bOver && JeebHit::InMoveLab(GetWorld()))
				for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
				{
					AKnightCharacter* K = *It;
					if (K == this || K->GetHealth() <= 0.f || KickedKnights.Contains(K)) continue;
					if (FVector::DistSquared(K->GetActorLocation(), GetActorLocation()) > FMath::Square(450.f)) continue;
					Hurt.Reset();
					K->GatherHurt(Hurt);
					JeebHit::FShieldWedge Shield;
					const bool bShieldUp = K->GetShieldWedge(Shield);
					int32 Hit = -1; bool bOnShield = false; uint8 Zone = JeebHit::Z_None;
					if (!Meet(Hurt, bShieldUp ? &Shield : nullptr, Hit, bOnShield, Zone)) continue;
					KickedKnights.Add(K);
					bool bCrit = false;
					const int32 Dmg = JeebItems::RollDamage(Skills, DmgKind, Tier, 0, bCrit, Power * Boxes[Hit].DamageMul);
					if (bKick)
					{
						if (K->BladeComing()) { KnockDownBy(K); break; }   // attack beats kick
						K->KnockDownBy(this);   // (his shield is down now, so the blow lands)
						KickLanded();
						K->Hurt(Dmg, this, false, bOnShield ? JeebHit::Z_None : Zone);
						continue;
					}
					K->Hurt(Dmg, this, false, Zone);   // (on his shield: the guard takes it, Hurt)
					if (bOnShield) { StartRebound(); break; }
					K->TakeReact(this, React);
				}
		}
	}
	else bHitPrevValid = false;
	DrawBoxes();
}

void AKnightCharacter::GatherHurt(TArray<JeebHit::FCapsule>& Out) const
{
	if (Anim && Anim->IsReady()) Anim->GatherHurt(Out);
}

// The view (H turns it on and off; MoveLab starts with it on): every hurt box yellow and every live hit box red, see-through and
// over the bodies, drawn by the player's own knight for everyone nearby
void AKnightCharacter::DrawBoxes()
{
	if (!IsLocallyControlled() || IsNetMode(NM_DedicatedServer)) return;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
		if (PC->WasInputKeyJustPressed(EKeys::H) && !IsUIOpen()) JeebHit::ToggleShow(GetWorld());
	const bool bShow = JeebHit::ShowBoxes(GetWorld());
	if (!bShow) { if (BoxView) BoxView->SetVisibility(false); return; }
	if (!BoxView)
	{
		BoxView = NewObject<UProceduralMeshComponent>(this);
		BoxView->SetUsingAbsoluteLocation(true);
		BoxView->SetUsingAbsoluteRotation(true);
		BoxView->SetUsingAbsoluteScale(true);
		BoxView->SetupAttachment(GetRootComponent());
		BoxView->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoxView->SetCastShadow(false);
		BoxView->RegisterComponent();
		BoxView->SetWorldLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_HitBox.M_HitBox"))) BoxView->SetMaterial(0, M);
	}
	BoxView->SetVisibility(true);
	static const FLinearColor HurtCol(1.f, 0.82f, 0.08f, 0.22f), HitCol(1.f, 0.08f, 0.04f, 0.45f), ShieldCol(0.15f, 0.5f, 1.f, 0.16f);
	TArray<FVector> V, N; TArray<int32> I; TArray<FLinearColor> Cols;
	TArray<JeebHit::FCapsule> Caps;
	JeebHit::FShieldWedge Wedge;
	const FVector Me = GetActorLocation();
	for (TActorIterator<AKnightCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsHidden() || FVector::DistSquared(It->GetActorLocation(), Me) > FMath::Square(3000.f)) continue;
		Caps.Reset();
		It->GatherHurt(Caps);
		for (const JeebHit::FCapsule& C : Caps) JeebHit::AddCapsuleMesh(C, HurtCol, V, N, I, Cols);
		for (const JeebHit::FCapsule& C : It->LiveHitboxes()) JeebHit::AddCapsuleMesh(C, HitCol, V, N, I, Cols);
		if (It->GetShieldWedge(Wedge)) JeebHit::AddWedgeMesh(Wedge, ShieldCol, V, N, I, Cols);   // a raised shield: blue walls
	}
	for (UHealthComponent* H : UHealthComponent::All())
	{
		if (!H || !H->IsAlive() || !H->GetOwner() || H->GetOwner()->IsHidden()) continue;
		if (FVector::DistSquared(H->GetOwner()->GetActorLocation(), Me) > FMath::Square(3000.f)) continue;
		Caps.Reset();
		JeebHit::HurtOf(H, Caps);
		for (const JeebHit::FCapsule& C : Caps) JeebHit::AddCapsuleMesh(C, HurtCol, V, N, I, Cols);
		if (JeebHit::ShieldOf(H, Wedge)) JeebHit::AddWedgeMesh(Wedge, ShieldCol, V, N, I, Cols);
	}
	BoxView->CreateMeshSection_LinearColor(0, V, I, N, TArray<FVector2D>(), Cols, TArray<FProcMeshTangent>(), false);
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
		// The shield's health (owner, 2026-11-06). GuardMax follows what is worn, whatever is in the hand. This is the one place that answers a change of shield, by whatever path it came:
		// the share that was left carries over, so no swap ever refills it, and a broken guard stays broken.
		const int32 ShieldId = Skills ? Skills->Worn[JeebItems::K_Shield] : 0;
		const float NewMax = JeebItems::ShieldHealth(ShieldId);
		const float RecoverDelay = ShieldId ? JeebItems::ShieldRecoverDelay(JeebItems::StyleOf(ShieldId)) : 6.25f;
		if (!FMath::IsNearlyEqual(NewMax, GuardMax))
		{
			if (GuardMax > 0.f) GuardShare = FMath::Clamp(GuardPool / GuardMax, 0.f, 1.f);
			GuardPool = NewMax * GuardShare;
			GuardMax = NewMax;
		}
		const float SinceHit = GetWorld()->GetTimeSeconds() - LastGuardHitTime;
		if (GuardMax <= 0.f)
		{
			// (no shield worn: what was left keeps healing while it is off, about as a tin kite shield would)
			if (SinceHit > RecoverDelay) GuardShare = FMath::Min(1.f, GuardShare + 0.15f * Dt);
			if (GuardShare >= 1.f) bGuardBroken = false;
		}
		else if (bBlocking && !bGuardBroken)
		{
			static const float& ShieldDrainPerSecond = JeebTune::Storage().ShieldDrainPerSecond;
			GuardPool -= ShieldDrainPerSecond * Dt;
			if (GuardPool < 1.f)
			{
				GuardPool = 0.f;
				bGuardBroken = true;
				LastGuardHitTime = GetWorld()->GetTimeSeconds();
				if (Skills) Skills->Say(2.5f, FColor::Orange, TEXT("Guard broken! Your shield is down until it recovers."));
			}
		}
		else if (GuardPool < GuardMax && SinceHit > RecoverDelay)
		{
			GuardPool = FMath::Min(GuardMax, GuardPool + JeebItems::ShieldHeal(Skills->GetLevel(ESkillType::Defense), JeebItems::StyleOf(ShieldId)) * Dt);
			if (GuardPool >= GuardMax && bGuardBroken)
			{
				bGuardBroken = false;
				if (Skills) Skills->Say(2.5f, FColor(140, 200, 255), TEXT("Your shield guard is back to full."));
			}
		}
		else if (GuardPool >= GuardMax) bGuardBroken = false;
	}

	// The shield and the sword never work together: it is down during a swing, and comes back up once the swing is over if the button is still held
	if (bAttacking || bRolling || Move->IsFalling() || bKnockedDown || bGuardBroken || StunLeft > 0.f || bDrinking) bBlocking = false;   // no guard mid-swing, mid-roll, in the air, knocked down, in hitstun or broken
	else if (bBlockHeld && !bBlocking && ShieldActive() && !bRolling && !Move->IsFalling() && !bSliding && !bClimbing && !bSitting && !bMounted && !IsUIOpen()) bBlocking = true;
}
