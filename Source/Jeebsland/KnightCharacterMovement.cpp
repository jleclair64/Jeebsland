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

void AKnightCharacter::StartSprint()
{
	bSprinting = true;
	bCrouched = false; // sprinting stands you up
}

void AKnightCharacter::StopSprint()
{
	bSprinting = false;
}

void AKnightCharacter::StartCrouch()
{
	if (bSitting || bRolling || bSliding || bClimbing || bMounted) return;
	bCrouched = true;
}

void AKnightCharacter::StopCrouch()
{
	bCrouched = false;
}

void AKnightCharacter::TryStartClimb()
{
	if (bSitting || bRolling || bSliding || bMounted || IsUIOpen()) return;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const FVector Acc = Move->GetCurrentAcceleration();
	if (Acc.SizeSquared2D() < 1.f) return;
	const FVector2D WishDir = FVector2D(Acc.X, Acc.Y).GetSafeNormal();
	const FVector Loc = GetActorLocation();
	const FVector2D P2(Loc.X, Loc.Y);
	const float FeetZ = Loc.Z - 90.f;
	const FVector2D VC = JeebTerrain::VillageCenter();
	const float VillageDeckTop = JeebTerrain::HeightAt(VC.X, VC.Y) + JeebTerrain::DeckHeight();

	static TArray<JeebTerrain::FLadder> AllLadders;
	if (AllLadders.Num() == 0)
	{
		AllLadders.Append(JeebTerrain::Ladders());
		AllLadders.Append(JeebTerrain::FortLadders());   // rope ladders up to the redwood forts
		AllLadders.Append(JeebTerrain::CastleLadders());   // and up the castle wall
	}
	for (const JeebTerrain::FLadder& L : AllLadders)
	{
		const float DeckTop = (L.Top > 0.f) ? L.Top : VillageDeckTop;
		const FVector2D Rel = P2 - L.Pos;
		const FVector2D Side(-L.Out.Y, L.Out.X);
		if (FMath::Abs(FVector2D::DotProduct(Rel, Side)) > 70.f) continue;
		const float Along = FVector2D::DotProduct(Rel, L.Out);
				const float Ground = (L.Bottom > 0.f) ? L.Bottom : JeebTerrain::HeightAt(L.Pos.X, L.Pos.Y);
		const bool bAtBottom = !Move->IsFalling() && FeetZ < Ground + 60.f && Along > 0.f && Along < 150.f;
		const bool bOnDeck = !Move->IsFalling() && FeetZ > DeckTop - 40.f && Along < 0.f && Along > -150.f;
		int32 Dir = 0;
		if (bAtBottom && FVector2D::DotProduct(WishDir, -L.Out) > 0.6f) Dir = 1;
		else if (bOnDeck && FVector2D::DotProduct(WishDir, L.Out) > 0.6f) Dir = -1;
		if (Dir == 0) continue;

		bClimbing = true;
		bClimbIgnoreUp = (Dir < 0);
		bCrouched = false;
		bBlocking = false;
		bAttacking = false;
		ClimbPos = L.Pos;
		ClimbOut = L.Out;
				ClimbGround = Ground;
		ClimbT = 0.f;
		ClimbDeckTop = DeckTop;
		Move->StopMovementImmediately();
		Move->DisableMovement();
		if (!HasAuthority()) ServerClimb(true);
		const FVector2D Hold = L.Pos + L.Out * 55.f;
		SetActorLocationAndRotation(FVector(Hold.X, Hold.Y, Loc.Z), FRotator(0.f, FVector(-L.Out.X, -L.Out.Y, 0.f).Rotation().Yaw, 0.f));
		return;
	}
}

void AKnightCharacter::UpdateClimb(float Dt)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bUp = PC && (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up));
	const bool bDown = PC && (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down));
	if (!bUp) bClimbIgnoreUp = false;
	const float Dir = (bUp && !bClimbIgnoreUp) ? 1.f : (bDown ? -1.f : 0.f);

			ClimbT += Dt;
	{
		// Nothing else may turn gravity back on while you are on the rope
		UCharacterMovementComponent* Cm = GetCharacterMovement();
		if (Cm->MovementMode != MOVE_None) Cm->DisableMovement();
		Cm->Velocity = FVector::ZeroVector;
	}
	FVector Loc = GetActorLocation();
	const FVector2D Hold = ClimbPos + ClimbOut * 55.f;
	Loc.X = FMath::FInterpTo(Loc.X, Hold.X, Dt, 15.f);
	Loc.Y = FMath::FInterpTo(Loc.Y, Hold.Y, Dt, 15.f);
	Loc.Z += Dir * 420.f * Dt;   // (was 280: half as fast again)
	SetActorLocation(Loc, false);
	ClimbPhase += Dir * Dt * 10.5f;

	const float FeetZ = Loc.Z - 90.f;
	if (Dir > 0.f && FeetZ >= ClimbDeckTop - 5.f) ExitClimb(true, false);
	else if (Dir < 0.f && FeetZ <= ClimbGround + 4.f) ExitClimb(false, true);
}

void AKnightCharacter::ExitClimb(bool bAtTop, bool bAtBottom)
{
	if (!bClimbing) return;
	bClimbing = false;
	if (!HasAuthority() && IsLocallyControlled()) ServerClimb(false);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bAtTop)
	{
		const FVector2D P = ClimbPos - ClimbOut * 85.f;
		SetActorLocation(FVector(P.X, P.Y, ClimbDeckTop + 95.f), false);
	}
	else if (bAtBottom)
	{
				const bool bFromDeck = ClimbGround > JeebTerrain::HeightAt(ClimbPos.X, ClimbPos.Y) + 40.f;   // a ladder between two terraces
		const FVector2D P = ClimbPos + ClimbOut * (bFromDeck ? 60.f : 115.f);
		SetActorLocation(FVector(P.X, P.Y, (bFromDeck ? ClimbGround : JeebTerrain::HeightAt(P.X, P.Y)) + 95.f), false);
	}
	Move->SetMovementMode(MOVE_Falling);
	if (!bAtTop && !bAtBottom)
	{
		Move->Velocity = FVector(ClimbOut.X * 250.f, ClimbOut.Y * 250.f, 120.f); // let go: hop off the ladder
	}
}

void AKnightCharacter::StartSlide()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float Speed = GetVelocity().Size2D();
	if (bSliding || bRolling || bSitting || bClimbing || bMounted || SlideCooldown > 0.f || Move->IsFalling()) return;
	if (!bSprinting || Speed < 480.f || Stamina < 10.f) return; // slides start from a sprint
	BeginSlide(GetVelocity().GetSafeNormal2D(), FMath::Max(Speed, 750.f) * 1.15f);
	if (IsLocallyControlled() && !HasAuthority()) ServerSlide(SlideDir, SlideSpeed);   // online: the server slides us too
}

void AKnightCharacter::BeginSlide(const FVector& Dir, float Speed)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	SlideDir = Dir.GetSafeNormal2D();
	SlideSpeed = FMath::Clamp(Speed, 0.f, 1300.f);
	SlideT = 0.f;
	SlideDustTimer = 0.f;
	Stamina -= 10.f;
	StaminaDelay = 1.0f;
	bCrouched = false;
	bBlocking = false;
	bSavedOrient = Move->bOrientRotationToMovement;
	Move->bOrientRotationToMovement = false;
	SetActorRotation(FRotator(0.f, SlideDir.Rotation().Yaw, 0.f));
	bSliding = true;
}

void AKnightCharacter::StartRoll()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bRolling || bSliding || bSitting || bClimbing || bMounted || RollCooldown > 0.f || Move->IsFalling() || Stamina < 20.f) return;

	// Roll the way you are steering, or straight ahead if you are standing still
	FVector Dir = PendingRollDir.IsNearlyZero() ? Move->GetCurrentAcceleration().GetSafeNormal2D() : PendingRollDir;
	PendingRollDir = FVector::ZeroVector;
	if (Dir.IsNearlyZero()) Dir = GetActorForwardVector().GetSafeNormal2D();
	RollDir = Dir;
	RollT = 0.f;
	Stamina -= 20.f;
	StaminaDelay = 1.0f;
	bCrouched = false;
	bBlocking = false;
	bSavedOrient = Move->bOrientRotationToMovement;
	Move->bOrientRotationToMovement = false;
	SetActorRotation(FRotator(0.f, RollDir.Rotation().Yaw, 0.f));
	bRolling = true;
	Kick(GetActorLocation() + FVector(0, 0, -94.f), 1.2f);
	if (IsLocallyControlled() && !HasAuthority()) ServerRoll(RollDir);   // online: the server rolls us too
}

void AKnightCharacter::Jump()
{
		if (bClimbing)
	{
		if (ClimbT > 0.6f) ExitClimb(false, false);   // letting go takes a deliberate press, not a stray one
		return;
	}
	if (bSitting)
	{
		StandUp();
		return;
	}
	if (bSliding)
	{
		bSliding = false;
		GetCharacterMovement()->bOrientRotationToMovement = bSavedOrient;
	}
	Super::Jump();
}

void AKnightCharacter::AddControllerYawInput(float Val)
{
	Super::AddControllerYawInput(Val * LookSensitivity);
}

void AKnightCharacter::AddControllerPitchInput(float Val)
{
	Super::AddControllerPitchInput(Val * LookSensitivity);
}

// Strafe mode (wheel click or X): on or off. Movement keeps going where you press; the knight just stops turning to face it
// and keeps facing where the camera looks (Tick), sidestepping and backing up like the shield stance but at full speed.
void AKnightCharacter::ToggleStrafe()
{
	if (IsUIOpen()) return;
	bStrafeMode = !bStrafeMode;
	if (Skills) Skills->Say(1.5f, FColor(200, 220, 255), bStrafeMode ? TEXT("Strafe mode on") : TEXT("Strafe mode off"));
}

void AKnightCharacter::DoMove(float Right, float Forward)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bMounted)
	{
		// Shield up: a combat stance. Move relative to the camera (which is where you face), so A and D sidestep and S backs up.
		if (bBlockHeld && !bRolling && !bSliding && !bClimbing && !bSitting && GetController())
		{
			const FRotator Yaw(0.f, GetController()->GetControlRotation().Yaw, 0.f);
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Forward);
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
			StanceFwd = Forward;
			StanceRight = Right;
			return;
		}
		Super::DoMove(Right, Forward);
		return;
	}
	// Like a car in a game: the horse goes forward and turns on a tight circle (a turning radius of about 1.75 m). The faster
	// it goes, the quicker it turns, and standing still it can still swing round. S backs it up slowly (it never sprints backwards).
	const float Dt = GetWorld()->GetDeltaSeconds();
	const float Speed = GetVelocity().Size2D();
	const float TurnRate = FMath::Clamp(FMath::RadiansToDegrees(Speed / 175.f), 70.f, 230.f);   // degrees per second
	if (Forward > 0.1f && GetController())
	{
		// Moving forward, the horse follows the camera like the knight does on foot: turn the mouse and it swings round that way
		// (A and D bend the direction further left or right of where the camera points), at its tight turning rate.
		const FRotator Cam(0.f, GetController()->GetControlRotation().Yaw, 0.f);
		const FVector Want = FRotationMatrix(Cam).GetUnitAxis(EAxis::X) * Forward + FRotationMatrix(Cam).GetUnitAxis(EAxis::Y) * Right;
		const float Cur = GetActorRotation().Yaw;
		const float Delta = FMath::FindDeltaAngleDegrees(Cur, Want.Rotation().Yaw);
		SetActorRotation(FRotator(0.f, Cur + FMath::Clamp(Delta, -TurnRate * Dt, TurnRate * Dt), 0.f));
	}
	else if (!FMath::IsNearlyZero(Right))
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + Right * TurnRate * Dt, 0.f));
	if (Forward < -0.1f)
	{
		LastBackTime = Now;
		AddMovementInput(-GetActorForwardVector(), 1.f);
	}
	else AddMovementInput(GetActorForwardVector(), FMath::Max(Forward, 0.f));
}

// A movement key (A, D, W or S) tapped twice within a quarter second, always (shield or not): a roll that way, relative to the
// camera. Any other movement key pressed in between breaks the pair, so "A D A D" (strafing back and forth) never rolls.
void AKnightCharacter::TapKey(int32 Key)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bDouble = (Key == LastTapKey) && (Now - LastTapTime) < 0.28;
	LastTapKey = Key;
	LastTapTime = Now;
	if (!bDouble || bMounted || IsUIOpen() || !GetController()) return;
	const FRotator Yaw(0.f, GetController()->GetControlRotation().Yaw, 0.f);
	const FVector Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	PendingRollDir = Key == 0 ? -Right : (Key == 1 ? Right : (Key == 2 ? Fwd : -Fwd));
	LastTapKey = -1;   // a third tap does not start another
	StartRoll();
	PendingRollDir = FVector::ZeroVector;
}

// ---------------- Tick steps (see AKnightCharacter::Tick) ----------------
// Rolls and slides carry the knight along by themselves. Returns true while one is going; OutSpecialSpeed is its speed.
bool AKnightCharacter::TickRollSlide(float Dt, bool bFalling, float& OutSpecialSpeed)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	SlideCooldown = FMath::Max(0.f, SlideCooldown - Dt);
	RollCooldown = FMath::Max(0.f, RollCooldown - Dt);
	float SpecialSpeed = 0.f;

	if (bRolling)
	{
		RollT += Dt;
		const float K = FMath::Clamp(RollT / RollDur, 0.f, 1.f);
		SpecialSpeed = FMath::Lerp(900.f, 380.f, K * K);
		Move->Velocity.X = RollDir.X * SpecialSpeed;
		Move->Velocity.Y = RollDir.Y * SpecialSpeed;
		if (RollT >= RollDur || bFalling)
		{
			bRolling = false;
			RollCooldown = 1.0f;   // one second before the next roll (every kind of roll)
			Move->bOrientRotationToMovement = bSavedOrient;
		}
	}
	if (bSliding)
	{
		SlideT += Dt;
		// Downhill keeps the slide going; flat ground slows it
		float Downhill = 0.f;
		if (Move->CurrentFloor.IsWalkableFloor())
		{
			Downhill = FMath::Max(0.f, FVector::DotProduct(Move->CurrentFloor.HitResult.ImpactNormal, SlideDir));
		}
		SlideSpeed = FMath::Clamp(SlideSpeed + (-650.f + 2600.f * Downhill) * Dt, 0.f, 1100.f);
		SpecialSpeed = SlideSpeed + 50.f;
		Move->Velocity.X = SlideDir.X * SlideSpeed;
		Move->Velocity.Y = SlideDir.Y * SlideSpeed;
		if (SlideSpeed < 190.f || SlideT > 3.f || AirAlpha > 0.8f)
		{
			bSliding = false;
			SlideCooldown = 0.9f;
			Move->bOrientRotationToMovement = bSavedOrient;
		}
	}
	OutSpecialSpeed = SpecialSpeed;
	return bRolling || bSliding;
}

// Sprint stamina. Returns whether the knight is trying to sprint (and may).
bool AKnightCharacter::TickStamina(float Dt, float Speed, bool bFalling, bool bSpecial)
{
	const bool bWantSprint = !bMounted && bSprinting && !bCrouched && !bBlocking && !bExhausted;
	const bool bDrain = bWantSprint && Speed > 330.f && !bFalling && !bSpecial;
	if (bDrain)
	{
		Stamina = FMath::Max(0.f, Stamina - 16.f * Dt / GetStaminaMult());   // better boots: it lasts longer...
		StaminaDelay = 1.0f;
		if (Stamina <= 0.f) bExhausted = true;
	}
	else
	{
		StaminaDelay = FMath::Max(0.f, StaminaDelay - Dt);
		if (StaminaDelay <= 0.f) Stamina = FMath::Min(MaxStamina, Stamina + 16.f * Dt * GetStaminaMult());   // ...and comes back faster
	}
	if (bExhausted && Stamina >= 4.f) bExhausted = false;   // sprint again as soon as a little has come back
	return bWantSprint;
}

// How fast the knight may go this frame (on foot, on the horse, in the shield stance, strafing, in the air), and turning to
// face the camera. Returns whether the knight faces the camera (strafe mode or the shield stance).
bool AKnightCharacter::TickGroundSpeed(float Dt, float Speed, bool bFalling, bool bSpecial, float SpecialSpeed, bool bWantSprint)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float AirMul = 0.69f;
	// Riding: twice the walking speed, and twice the sprint. The horse sprints for about four seconds, then needs
	// its 100-point bar to refill completely before the next gallop.
	bool bHorseSprint = false;
	if (bMounted)
	{
		const bool bAsk = bSprinting && Speed > 200.f && !bHorseTired && !bFalling && (GetWorld()->GetTimeSeconds() - LastBackTime > 0.15);
		if (bAsk && HorseEnergy > 0.f)
		{
			// the horse never tires: its gallop energy does not drain
			bHorseSprint = true;
			if (HorseEnergy <= 0.f) bHorseTired = true;
		}
		else
		{
			HorseEnergy = FMath::Min(100.f, HorseEnergy + (bHorseTired ? 10.f : 16.f) * Dt);
			if (bHorseTired && HorseEnergy >= 4.f) bHorseTired = false;
		}
	}
	// The horse is 20% slower than before, and builds up to a gallop: it does not jump to full speed when you sprint
	float HorseTarget = 0.8f * (bHorseSprint ? 2.f * SprintSpeed : 2.f * WalkSpeed);
	if (!bMounted) HorseSpeedNow = 0.f;
	else
	{
		if (HorseSpeedNow < 1.f) HorseSpeedNow = 0.8f * 2.f * WalkSpeed;
		HorseSpeedNow = FMath::FInterpConstantTo(HorseSpeedNow, HorseTarget, Dt, HorseTarget > HorseSpeedNow ? 330.f : 900.f);   // speeds up over about 1.7 s, eases off quicker
	}
	// The horse backs up slowly with S (never sprinting). On foot, a raised shield is a combat stance: you face where the camera
	// points and sidestep or back up instead of turning round, at full walking (jog) speed in every direction; never sprinting.
	const bool bBackStep = bMounted && (GetWorld()->GetTimeSeconds() - LastBackTime < 0.15);
	const bool bStance = bBlockHeld && !bMounted && !bRolling && !bSliding && !bClimbing && !bSitting;
	const bool bFaceCam = (bStance || bStrafeMode) && !bMounted && !bRolling && !bSliding && !bClimbing && !bSitting;   // strafe mode faces the camera too, at full speed
	if (bFaceCam && HasAuthority() && !IsLocallyControlled() && GetController())
	{
		// (the server doesn't get the player's keys: it reads the stance step from the way the knight is being pushed)
		const FVector Acc = Move->GetCurrentAcceleration().GetSafeNormal2D();
		const FRotator CamYaw(0.f, GetController()->GetControlRotation().Yaw, 0.f);
		StanceFwd = FVector::DotProduct(Acc, FRotationMatrix(CamYaw).GetUnitAxis(EAxis::X));
		StanceRight = FVector::DotProduct(Acc, FRotationMatrix(CamYaw).GetUnitAxis(EAxis::Y));
	}
	if (HasAuthority() && GetNetMode() != NM_Standalone) RepMoveFlags = (bCrouched ? 1 : 0) | (bBlocking ? 2 : 0) | (bRolling ? 4 : 0) | (bSliding ? 8 : 0) | (bClimbing ? 16 : 0);
	if (bFaceCam && !bBackOrient) { bBackOrient = true; bBackSavedOrient = Move->bOrientRotationToMovement; Move->bOrientRotationToMovement = false; }
	else if (!bFaceCam && bBackOrient && !bRolling && !bSliding) { bBackOrient = false; Move->bOrientRotationToMovement = bBackSavedOrient; }
	if (bFaceCam && GetController())   // turn to the camera quickly but smoothly (about 0.15 s for a quarter turn)
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, GetController()->GetControlRotation().Yaw, 0.f), Dt, 640.f));
	const float StanceFactor = 1.f;
	const float ShieldMove = (Skills && Skills->Worn[JeebItems::K_Shield]) ? FMath::Min(1.f, JeebItems::ShieldMoveMul(JeebItems::StyleOf(Skills->Worn[JeebItems::K_Shield]))) : 1.f;   // tower: slower; nothing beats a walk
	// Strafe mode works like Fortnite: you can only sprint while heading roughly forward (within about 45 degrees of where
	// you face). Sidestepping and backing up stay at normal speed even with sprint held, and backing up is a bit slower
	// (full speed ahead, 90% sideways, 75% straight back). Read from the knight's actual acceleration, so the server agrees.
	// The sprint fades in rather than switching: none at 60 degrees off straight ahead, all of it within about 20 degrees,
	// and the change itself eases over a fraction of a second.
	float StrafeMul = 1.f;
	float WantSprintFrac = 1.f;
	if (bFaceCam && !bStance)
	{
		const FVector Acc = Move->GetCurrentAcceleration().GetSafeNormal2D();
		if (!Acc.IsNearlyZero())
		{
			const float Fw = FVector::DotProduct(Acc, GetActorForwardVector().GetSafeNormal2D());
			WantSprintFrac = FMath::SmoothStep(0.5f, 0.94f, Fw);
			StrafeMul = Fw >= 0.f ? FMath::Lerp(0.9f, 1.f, Fw) : FMath::Lerp(0.9f, 0.75f, -Fw);
		}
	}
	StrafeSprint = FMath::FInterpTo(StrafeSprint, WantSprintFrac, Dt, 5.f);
	const float RunSpeed = bWantSprint ? FMath::Lerp(WalkSpeed, SprintSpeed, StrafeSprint) : WalkSpeed;
	const float BaseSpeed = bBackStep ? 0.5f * WalkSpeed : bMounted ? HorseSpeedNow
	                                 : (bStance ? WalkSpeed * StanceFactor * ShieldMove : ((bCrouched || bBlocking) ? CrouchSpeed : RunSpeed * StrafeMul)) * ((!bMounted && GetWorld()->GetTimeSeconds() < SpeedUntil) ? SpeedPotionMul : 1.f)   // swiftness potion
	                                 * ((!bMounted && Skills) ? JeebItems::CombinedMods(Skills).SpeedMul : 1.f);   // the Red Helm Brush
	if (bFalling && !bSpecial)
	{
		AirSpeedMul = AirMul;
		const float AirCap = BaseSpeed * AirMul;
		FVector Horizontal(Move->Velocity.X, Move->Velocity.Y, 0.f);
		const float HSpeed = Horizontal.Size();
		if (HSpeed > AirCap)
		{
			const float NewSpeed = FMath::FInterpConstantTo(HSpeed, AirCap, Dt, 1500.f); // about 0.2 s to go from a sprint to the air speed
			Horizontal *= NewSpeed / HSpeed;
			Move->Velocity.X = Horizontal.X;
			Move->Velocity.Y = Horizontal.Y;
		}
	}
	else if (!bFalling)
	{
		AirSpeedMul = FMath::FInterpTo(AirSpeedMul, 1.f, Dt, 6.f); // speeding back up after landing, as before
	}
	Move->MaxWalkSpeed = bSpecial ? SpecialSpeed : BaseSpeed * AirSpeedMul;
	return bFaceCam;
}
