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
	Loc.Z += Dir * 280.f * Dt;
	SetActorLocation(Loc, false);
	ClimbPhase += Dir * Dt * 7.f;

	const float FeetZ = Loc.Z - 90.f;
	if (Dir > 0.f && FeetZ >= ClimbDeckTop - 5.f) ExitClimb(true, false);
	else if (Dir < 0.f && FeetZ <= ClimbGround + 4.f) ExitClimb(false, true);
}

void AKnightCharacter::ExitClimb(bool bAtTop, bool bAtBottom)
{
	if (!bClimbing) return;
	bClimbing = false;
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

	SlideDir = GetVelocity().GetSafeNormal2D();
	SlideSpeed = FMath::Max(Speed, 750.f) * 1.15f;
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

