#include "KnightMovement.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "KnightCharacter.h"
#include "SkillComponent.h"
#include "JeebTuning.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

// Free climbing and the mantle: see KnightMovement.h. All of it runs inside a move, so the player's machine, the server and correction
// replays do the same thing from the same inputs; every number is Tuning.ini [Climb].

namespace
{
	FCollisionObjectQueryParams WorldObjects()
	{
		FCollisionObjectQueryParams P;
		P.AddObjectTypesToQuery(ECC_WorldStatic);
		P.AddObjectTypesToQuery(ECC_WorldDynamic);
		return P;
	}
	float Ease(float U) { U = FMath::Clamp(U, 0.f, 1.f); return U * U * (3.f - 2.f * U); }
	FVector Flat(const FVector& V) { return FVector(V.X, V.Y, 0.f).GetSafeNormal(); }
}

// How long a full bar lasts climbing upward, by Climbing level (owner, 2026-11-12: 5 s at level 1 to 30 s at 99, more of it early, like the
// bow's draw); straight lines between the four levels in Tuning.ini [Climb]
float AKnightCharacter::ClimbSeconds(int32 L)
{
	const JeebTune::FValues& T = JeebTune::V();
	const float X = float(FMath::Clamp(L, 1, 99));
	return X <= 10.f ? FMath::GetMappedRangeValueClamped(FVector2f(1.f, 10.f), FVector2f(T.ClimbAt1, T.ClimbAt10), X)
		: X <= 30.f ? FMath::GetMappedRangeValueClamped(FVector2f(10.f, 30.f), FVector2f(T.ClimbAt10, T.ClimbAt30), X)
		: FMath::GetMappedRangeValueClamped(FVector2f(30.f, 99.f), FVector2f(T.ClimbAt30, T.ClimbAt99), X);
}

UKnightMovement::UKnightMovement()
{
	SetMoveResponseDataContainer(KnightResponse);
}

AKnightCharacter* UKnightMovement::Knight() const { return Cast<AKnightCharacter>(CharacterOwner); }

FVector UKnightMovement::WallUp(const FVector& N)
{
	const FVector U = (FVector::UpVector - N * N.Z).GetSafeNormal();
	return U.IsNearlyZero() ? FVector::UpVector : U;
}

FVector UKnightMovement::WallRight(const FVector& N)
{
	return FVector::CrossProduct(WallUp(N), -N).GetSafeNormal();
}

bool UKnightMovement::Climbable(const FHitResult& H) const
{
	if (!H.bBlockingHit || H.bStartPenetrating) return false;
	const UPrimitiveComponent* C = H.GetComponent();
	if (!JeebClimb::IsClimbGround(C)) return false;
	return H.ImpactNormal.Z < GetWalkableFloorZ() && H.ImpactNormal.Z >= -0.26f;   // too steep to walk, and no more than 15 degrees overhanging
}

bool UKnightMovement::ProbeWall(const FVector& From, const FVector& Dir, float Dist, FHitResult& Hit) const
{
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebClimb), true, CharacterOwner);
	return GetWorld()->SweepSingleByObjectType(Hit, From, From + Dir * Dist, FQuat::Identity, WorldObjects(), FCollisionShape::MakeSphere(20.f), Q);
}

float UKnightMovement::DrainPerSecond() const
{
	const AKnightCharacter* K = Knight();
	const int32 L = (K && K->GetSkills()) ? K->GetSkills()->GetLevel(ESkillType::Climbing) : 1;
	return 1.f / FMath::Max(AKnightCharacter::ClimbSeconds(L), 0.5f);
}

// (test switch: 0 brings back the old per-frame speed and start-up push, set by the knight's Tick, to count corrections before and after; both machines)
static TAutoConsoleVariable<int32> CVarSpeedInMove(TEXT("jeeb.Knight.SpeedInMove"), 1, TEXT("1: his speed and start-up push are worked out inside each move (client and server agree). 0: the old per-frame values, to compare. Set on the server and the client."), ECVF_Default);

float UKnightMovement::GetMaxSpeed() const
{
	if (MovementMode == MOVE_Custom)
	{
		const JeebTune::FValues& T = JeebTune::V();
		if (InWater()) return T.SwimFastSpeed;
		return FMath::Max3(T.ClimbUpSpeed, T.ClimbSideSpeed, T.ClimbDownSpeed);
	}
	const bool bRun = CVarSpeedInMove.GetValueOnGameThread() != 0 && SpeedRule.bByMove && (MovementMode == MOVE_Falling || ((MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking) && !IsCrouching()));
	const float S = bRun ? MoveCap(Acceleration) : Super::GetMaxSpeed();   // (this move's own push: KnightMovement.h, FSpeedRule)
	return MovementMode == MOVE_Walking && WaterDepth > 30.f ? S * WadeMul(WaterDepth) : S;   // (wading: slower the deeper)
}

float UKnightMovement::MoveCap(const FVector& Dir) const
{
	const FSpeedRule& R = SpeedRule;
	float StrafeMul = 1.f;   // strafe mode: full speed ahead, 90% sideways, 75% straight back (AKnightCharacter::TickGroundSpeed explains)
	if (R.bStrafe && UpdatedComponent)
	{
		const FVector D = Dir.GetSafeNormal2D();
		if (!D.IsNearlyZero())
		{
			const float Fw = FVector::DotProduct(D, UpdatedComponent->GetForwardVector().GetSafeNormal2D());
			StrafeMul = Fw >= 0.f ? FMath::Lerp(0.9f, 1.f, Fw) : FMath::Lerp(0.9f, 0.75f, -Fw);
		}
	}
	const float Run = (bWantsFast && R.bSprintOk) ? FMath::Lerp(R.Walk, R.Sprint, MoveStrafeSprint) : R.Walk;
	return Run * StrafeMul * R.K * MoveAirMul;
}

// No wind-up starting to walk: on the ground (or on the rock) and below walking speed he gets there at once; above it (walk to sprint)
// the usual acceleration keeps the sprint's ramp. From the velocity at the move's start, so the client's scaling of his push and the
// server's check of it (MoveAutonomous) use the same number; the direction for strafe mode's shares is the way he is already going.
float UKnightMovement::AccelRuleNow() const
{
	const JeebTune::FValues& T = JeebTune::V();
	// (owner 2026-11-21: the troll is as fast as the knight but takes a few seconds to get going: one slow push on the ground, worked out
	// from the move's own state like the rest, so the server and the client agree; braking stays the usual quick one)
	// (owner 2026-11-22: "no acceleration ... to the walking speed, just to the sprint": up to running speed he gets there at once like the
	// knight; only the climb from running to a full sprint takes the slow push)
	if (const AKnightCharacter* K = Knight(); K && K->IsTroll() && !IsFalling() && !InWater())
	{
		const float WalkCap = SpeedRule.bAccelRule ? FMath::Min(SpeedRule.bByMove ? MoveCap(Velocity) : MaxWalkSpeed, SpeedRule.WalkCapBase * MoveAirMul) : K->WalkNow() * MoveAirMul;
		return Velocity.Size2D() < WalkCap - 5.f ? T.WalkAccel : T.TrollAccel;
	}
	if (!SpeedRule.bAccelRule || IsFalling()) return T.SprintAccel;
	const float Cap = SpeedRule.bByMove ? MoveCap(Velocity) : MaxWalkSpeed;
	const float WalkCap = FMath::Min(Cap, SpeedRule.WalkCapBase * MoveAirMul);
	return Velocity.Size2D() < WalkCap - 5.f ? T.WalkAccel : T.SprintAccel;
}

float UKnightMovement::GetMaxAcceleration() const
{
	if (CVarSpeedInMove.GetValueOnGameThread() == 0) return Super::GetMaxAcceleration();   // (the old way, per frame: to compare)
	return bAccelFrozen ? FrozenAccel : AccelRuleNow();
}

void UKnightMovement::ControlledCharacterMove(const FVector& InputVector, float DeltaSeconds)
{
	FrozenAccel = AccelRuleNow();
	bAccelFrozen = true;   // (held through the move: a merged move is refused if its parts had different ones, FSavedMove_Knight::CanCombineWith)
	Super::ControlledCharacterMove(InputVector, DeltaSeconds);
	bAccelFrozen = false;
}

void UKnightMovement::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	// (the server running a client's move, and the client replaying its moves after a correction)
	FrozenAccel = AccelRuleNow();
	bAccelFrozen = true;
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
	bAccelFrozen = false;
}

void UKnightMovement::PhysicsRotation(float DeltaTime)
{
	if (MovementMode == MOVE_Custom) return;   // on the wall and in scripted moves the climb turns him itself
	Super::PhysicsRotation(DeltaTime);
}

void UKnightMovement::OnMovementModeChanged(EMovementMode PrevMode, uint8 PrevCustom)
{
	Super::OnMovementModeChanged(PrevMode, PrevCustom);
	// (by mode: the swim is a custom mode too, and none of the climbing's bookkeeping belongs to it)
	auto IsClimbMode = [](EMovementMode M, uint8 C) { return M == MOVE_Custom && C >= JeebClimb::Climb && C <= JeebClimb::MantleOnto; };
	auto IsWaterMode = [](EMovementMode M, uint8 C) { return M == MOVE_Custom && (C == JeebClimb::Swim || C == JeebClimb::Drown); };
	const bool bWasClimb = IsClimbMode(PrevMode, PrevCustom), bIsClimb = IsClimbMode(MovementMode, CustomMovementMode);
	if (bIsClimb && !bWasClimb)
	{
		ClimbBestZ = UpdatedComponent ? float(UpdatedComponent->GetComponentLocation().Z) : 0.f;
		XPCarry = 0.f;
	}
	if (bWasClimb && !bIsClimb) { RefillWait = JeebTune::V().ClimbRefillDelay; FlushClimbXP(); }
	const bool bWasWater = IsWaterMode(PrevMode, PrevCustom), bIsWater = IsWaterMode(MovementMode, CustomMovementMode);
	if (bIsWater && !bWasWater && UpdatedComponent) { SwimLastXY = FVector2D(UpdatedComponent->GetComponentLocation()); }
	if (bWasWater && !bIsWater) { SwimRefillWait = JeebTune::V().SwimRefillDelay; SwimXP(true); DrownT = 0.f; bHeadUnder = false; }
	if (MovementMode == MOVE_Walking) bNoRegrab = false;
}

void UKnightMovement::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsLetGo = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsFast = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	bWantsRise = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
}

FNetworkPredictionData_Client* UKnightMovement::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UKnightMovement* Self = const_cast<UKnightMovement*>(this);
		Self->ClientPredictionData = new FNetworkPredictionData_Client_Knight(*this);
	}
	return ClientPredictionData;
}

// ---------------------------------------------------------------- each move, before it runs

void UKnightMovement::UpdateCharacterStateBeforeMovement(float Dt)
{
	Super::UpdateCharacterStateBeforeMovement(Dt);
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) return;   // others' machines only show what the server sends
	const JeebTune::FValues& T = JeebTune::V();
	// The running speed's eased shares, per move (KnightMovement.h, FSpeedRule): the sprint fades in over a fraction of a second as he turns
	// toward straight ahead (none at 60 degrees off, all of it within about 20), and after landing he speeds back up from the air's share.
	{
		float WantSprint = 1.f;
		const FVector A = Acceleration.GetSafeNormal2D();
		if (SpeedRule.bStrafe && UpdatedComponent && !A.IsNearlyZero())
			WantSprint = FMath::SmoothStep(0.5f, 0.94f, float(FVector::DotProduct(A, UpdatedComponent->GetForwardVector().GetSafeNormal2D())));
		MoveStrafeSprint = FMath::FInterpTo(MoveStrafeSprint, WantSprint, Dt, 5.f);
		MoveAirMul = IsFalling() ? SpeedRule.AirMul : FMath::FInterpTo(MoveAirMul, 1.f, Dt, 6.f);
	}
	if (!IsCustom(JeebClimb::Climb)) bJumpQueued = false;
	bTouchMantle = false;   // (left over from a move the player's machine threw away or merged: never acted on)

	// The water at his feet (JeebWater: the same answer on every machine). Deep enough, walking, falling or climbing down into it becomes the swim.
	SenseWater();
	if (!InWater() && WaterDepth > T.SwimEnterDepth && (IsMovingOnGround() || IsFalling() || (IsCustom(JeebClimb::Climb) && WaterDepth > T.SwimEnterDepth + 20.f)))
	{
		bJumpAsked = false;
		bTouchRock = bTouchMantle = false;
		StartSwim();
		return;
	}
	// The swimming bar refills only on dry ground, after a short wait; the breath comes back whenever his head is out (PhysSwim does it in the water)
	if (IsMovingOnGround() && WaterDepth < T.SwimExitDepth)
	{
		SwimRefillWait = FMath::Max(0.f, SwimRefillWait - Dt);
		if (SwimRefillWait <= 0.f && SwimStamina < 1.f) SwimStamina = FMath::Min(1.f, SwimStamina + T.SwimRefillPerSec * Dt);
		Breath = FMath::Min(1.f, Breath + T.BreathRefillPerSec * Dt);
	}

	// Space on the ground near a fence: the mantle if it fits, else the normal jump (decided here, where this move's keys are known)
	if (bJumpAsked)
	{
		bJumpAsked = false;
		if (!(IsMovingOnGround() && TryMantle(false)))
		{
			UE_LOG(LogTemp, Log, TEXT("JEEBMANTLE %s space: no mantle, a jump: %s"), CharacterOwner->HasAuthority() ? TEXT("server") : TEXT("client"), *MantleWhy);
			if (AKnightCharacter* K = Knight()) K->JumpFromMove(Dt);
		}
	}

	// Refill on the ground, after a short wait; an emptied bar has to fill right up before he can grab again
	if (IsMovingOnGround())
	{
		RefillWait = FMath::Max(0.f, RefillWait - Dt);
		if (RefillWait <= 0.f && ClimbStamina < 1.f) ClimbStamina = FMath::Min(1.f, ClimbStamina + T.ClimbRefillPerSec * Dt);
		if (ClimbStamina >= 1.f) bClimbExhausted = false;
	}

	bTouchRock = bTouchMantle = false;   // (a bump counts only within the move that made it: OnMovementUpdated grabs or mantles there)
}

// The end of each move: walking or running into something he may mantle, over it or onto it by itself (owner 2026-11-13: automatic, no Space), unless Ctrl
// is held (crouched, he just stops at it); in the air moving at one, the mantle catches it. Decided in the same move as the bump (2026-11-16): a bump
// carried over to the next move went wrong when the player's machine merged or replayed moves, and the server corrected him at every vault.
void UKnightMovement::OnMovementUpdated(float Dt, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(Dt, OldLocation, OldVelocity);
	if (TryHorseJump()) { bTouchMantle = bTouchRock = false; return; }   // (riding: the horse's own jump, KnightMovementHorse.cpp)
	const bool bFence = bTouchMantle, bRock = bTouchRock;
	bTouchMantle = bTouchRock = false;
	// The grab, in the same move as the bump into steep rock or ground (2026-11-20: it used to wait for the next move, which a merged or replayed
	// move could change; now server and client decide it from the same move, like the mantle). Only after running into it: no probes for anyone else.
	if (bRock && CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy && MovementMode != MOVE_Custom && (IsMovingOnGround() || IsFalling()) && TryGrab()) return;
	if (!bFence || !CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || MovementMode == MOVE_Custom || Acceleration.SizeSquared2D() <= 1.f) return;
	if (IsMovingOnGround() && JeebTune::V().MantleAuto > 0.5f && !bWantsLetGo) TryMantle(false);   // (MantleAuto 0 in Tuning.ini: only Space by one)
	else if (IsFalling() && MantleNearby()) TryMantle(true);
}

bool UKnightMovement::TryGrab()
{
	AKnightCharacter* K = Knight();
	if (K && K->IsTroll()) return false;   // (owner 2026-11-21: trolls don't climb rock faces; same check on server and client)
	if (!K || bNoRegrab || bClimbExhausted || ClimbStamina <= 0.02f || bWantsLetGo) return false;
	if (K->bClimbing || K->bSitting || K->bMounted || K->bRolling || K->bSliding || K->bKnockedDown || K->StunLeft > 0.f) return false;
	const FVector Dir = Flat(Acceleration);
	if (Acceleration.SizeSquared2D() < 1.f || Dir.IsNearlyZero()) return false;
	const float R = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector C = UpdatedComponent->GetComponentLocation();
	// at the chest and at the knee (a slope that leans back meets the knee first)
	for (const float Up : { 25.f, -55.f })
	{
		FHitResult H;
		if (!ProbeWall(C + FVector(0.f, 0.f, Up), Dir, R + 15.f, H) || !Climbable(H)) continue;
		const FVector NF = Flat(H.ImpactNormal);
		if (NF.IsNearlyZero() || FVector::DotProduct(-NF, Dir) < 0.643f) continue;   // pushing at it, within 50 degrees
		// a bank too low to be worth a climb (a pad's edge, a small rise): the ground just past where he touched must stand GrabMinRise over his feet
		if (IsMovingOnGround())
		{
			const float Feet = float(C.Z) - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector Ahead = FVector(C.X, C.Y, 0.f) + Dir * (R + 100.f);   // (a metre past his front: the bank, not the bit his knee touched)
			FHitResult G;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebGrabRise), true, CharacterOwner);
			const bool bG = GetWorld()->LineTraceSingleByObjectType(G, FVector(Ahead.X, Ahead.Y, Feet + 400.f), FVector(Ahead.X, Ahead.Y, Feet - 20.f), WorldObjects(), Q);
			if (bG && !G.bStartPenetrating && float(G.ImpactPoint.Z) - Feet < JeebClimb::GrabMinRise)
			{
				UE_LOG(LogTemp, Log, TEXT("JEEBCLIMB no grab: the bank rises only %.0f cm"), float(G.ImpactPoint.Z) - Feet);
				continue;
			}
		}
		StartClimb(H.ImpactNormal);
		return true;
	}
	{
		static double LastLog = 0.0;   // (tests: why a bump into climbable ground did not grab, once a second)
		const double Now = FPlatformTime::Seconds();
		static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("BankClimb")) || FParse::Param(FCommandLine::Get(), TEXT("BankClimbNet"));
		if (bTest && Now - LastLog > 1.0) { LastLog = Now; FHitResult H1, H2; const bool b1 = ProbeWall(C + FVector(0.f, 0.f, 25.f), Dir, R + 15.f, H1), b2 = ProbeWall(C + FVector(0.f, 0.f, -55.f), Dir, R + 15.f, H2);
			UE_LOG(LogTemp, Log, TEXT("JEEBCLIMB no grab: chest %d nz %.2f, knee %d nz %.2f"), b1 ? 1 : 0, b1 ? float(H1.ImpactNormal.Z) : 0.f, b2 ? 1 : 0, b2 ? float(H2.ImpactNormal.Z) : 0.f); }
	}
	return false;
}

void UKnightMovement::StartClimb(const FVector& N)
{
	WallNormal = N.GetSafeNormal();
	Velocity = FVector::ZeroVector;
	SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
	SetMovementMode(MOVE_Custom, JeebClimb::Climb);
	++Grabs;
	LogClimb(TEXT("grab"));
}

void UKnightMovement::DropOff(bool bExhausted, float Push)
{
	Velocity = Flat(WallNormal) * Push;
	bNoRegrab = true;
	if (bExhausted) { bClimbExhausted = true; ClimbStamina = 0.f; }
	SetMovementMode(MOVE_Falling);
}

void UKnightMovement::ForceDrop(bool bAll)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority()) return;
	if (InWater())
	{
		// (a hit or a knockdown doesn't take him out of the water; death does: he wakes far away with full bars)
		if (!bAll) return;
		Velocity = FVector::ZeroVector;
		SetMovementMode(MOVE_Falling);   // (gives the Swimming XP he had earned: OnMovementModeChanged)
		ResetWater();
		LogSwim(TEXT("died"));
		if (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
			if (FNetworkPredictionData_Server_Character* SD = GetPredictionData_Server_Character()) SD->bForceClientUpdate = true;
		return;
	}
	if (!InClimbMove() || (InMantle() && !bAll)) return;
	DropOff(false, 40.f);
	LogClimb(TEXT("knocked off"));
	if (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
		if (FNetworkPredictionData_Server_Character* SD = GetPredictionData_Server_Character()) SD->bForceClientUpdate = true;
}

// ---------------------------------------------------------------- physics

void UKnightMovement::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || DeltaTime < MIN_TICK_TIME) return;
	switch (CustomMovementMode)
	{
	case JeebClimb::Climb: PhysClimb(DeltaTime); break;
	case JeebClimb::Jump: PhysClimbJump(DeltaTime); break;
	case JeebClimb::ClimbUp: case JeebClimb::MantleOver: case JeebClimb::MantleOnto: PhysScripted(DeltaTime); break;
	case JeebClimb::Swim: PhysSwim(DeltaTime); break;
	case JeebClimb::Drown: PhysDrown(DeltaTime); break;
	default: SetMovementMode(MOVE_Falling); break;
	}
}

void UKnightMovement::PhysClimb(float Dt)
{
	const JeebTune::FValues& T = JeebTune::V();
	if (bWantsLetGo) { DropOff(false, 60.f); LogClimb(TEXT("let go")); return; }
	if (ClimbStamina <= 0.f) { DropOff(true, 60.f); LogClimb(TEXT("out of stamina")); return; }
	const FVector N = WallNormal, Up = WallUp(N), Right = WallRight(N);
	// The keys, in the wall's own directions (the server works them out against its own wall: only their direction is the player's)
	const FVector A = FVector::VectorPlaneProject(Acceleration, N) / FMath::Max(GetMaxAcceleration(), 1.f);
	float InUp = FVector::DotProduct(A, Up), InRight = FVector::DotProduct(A, Right);
	const float Mag = FMath::Sqrt(InUp * InUp + InRight * InRight);
	if (Mag > 1.f) { InUp /= Mag; InRight /= Mag; }
	const float M = FMath::Min(Mag, 1.f);

	// Space: the climb jump, the way the keys point (up when none), for a chunk of the bar
	if (bJumpQueued)
	{
		bJumpQueued = false;
		if (!bClimbExhausted && ClimbStamina >= T.ClimbJumpCost)   // (with less left than it costs, Space does nothing)
		{
			ClimbStamina = FMath::Max(0.f, ClimbStamina - T.ClimbJumpCost);
			ActFrom = UpdatedComponent->GetComponentLocation();
			ActDir = M > 0.2f ? (Up * InUp + Right * InRight).GetSafeNormal() : Up;
			ActT = 0.f; ActDur = FMath::Max(T.ClimbJumpTime, 0.2f); ActTop = 0.f; ActCarry = 0.f; ActHold = 0.f;
			Velocity = FVector::ZeroVector;
			SetMovementMode(MOVE_Custom, JeebClimb::Jump);
			LogClimb(TEXT("jump"));
			return;
		}
	}

	// The speed: by the clips' own pace (Tuning.ini), slower in the last fifth of the bar (tired), eased in over about 0.1 s
	const float Tired = ClimbStamina < 0.2f ? FMath::Lerp(0.7f, 1.f, ClimbStamina / 0.2f) : 1.f;
	// A bank that lies back (2026-11-20, steep ground such as a riverbank: 45 to 60 degrees) is a scramble: up to 1.6x as fast and 0.4x the drain at 45, none of it from 70 degrees up
	const float Lean = FMath::SmoothStep(0.34f, 0.70f, float(N.Z));
	const FVector Want = (Up * InUp * (InUp > 0.f ? T.ClimbUpSpeed : T.ClimbDownSpeed) + Right * InRight * T.ClimbSideSpeed) * Tired * (1.f + 0.6f * Lean);
	const FVector VW = FVector::VectorPlaneProject(Velocity, N);
	Velocity = VW + (Want - VW) * FMath::Min(1.f, Dt / 0.1f);

	// The bar (owner): hanging still drains a fifth as fast as climbing up, sideways 2/3, down ClimbDownShare; a diagonal mixes its parts
	const float U = FMath::Max(InUp, 0.f), D = FMath::Max(-InUp, 0.f), S = FMath::Abs(InRight), Sum = U + D + S;
	const float DirShare = Sum > KINDA_SMALL_NUMBER ? (U + S * T.ClimbSideShare + D * T.ClimbDownShare) / Sum : T.ClimbHangShare;
	ClimbStamina = FMath::Max(0.f, ClimbStamina - FMath::Lerp(T.ClimbHangShare, DirShare, M) * DrainPerSecond() * (1.f - 0.6f * Lean) * Dt);

	const FVector OldLoc = UpdatedComponent->GetComponentLocation();
	const FVector Delta = Velocity * Dt;
	if (!Delta.IsNearlyZero())
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
		if (Hit.IsValidBlockingHit())
		{
			if (Climbable(Hit)) WallNormal = (WallNormal + (Hit.ImpactNormal - WallNormal) * 0.5f).GetSafeNormal();   // an inside corner: turn onto it
			SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
		}
	}
	if (!HugWall(Dt, InUp, OldLoc)) return;
	FaceWall(Dt);
	// Climbing down onto ground he can stand on ends the climb
	if (InUp < -0.3f)
	{
		FFindFloorResult F;
		FindFloor(UpdatedComponent->GetComponentLocation(), F, false);
		if (F.IsWalkableFloor() && F.FloorDist < 8.f) { SetMovementMode(MOVE_Walking); LogClimb(TEXT("down")); return; }
	}
	GiveXP();
	LogClimb(nullptr);
}

// Keeps him the gap off the rock along its normal, follows the slope as it bends, and handles the wall running out:
// a ledge above (climb up), ground he can stand on (step off onto it), or the edge of the climbable part (he stops there).
bool UKnightMovement::HugWall(float Dt, float InUp, const FVector& OldLoc)
{
	const float R = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector C = UpdatedComponent->GetComponentLocation();
	const FVector Dir = -WallNormal;
	FHitResult H;
	bool bOk = false;
	// Going up, the rock has to reach his hands (about 90 cm over the capsule's middle in the climbing clip): once it ends below them, he climbs up
	// over the edge, or with no room up there he stops. (Owner 2026-11-13: he used to climb on until the rock ended below his waist and then
	// his knees, hands and head over thin air, "an invisible wall past the ledge". The ledge clip expects the top about 90 cm up, too.)
	if (InUp > 0.3f)
	{
		constexpr float HandUp = 105.f;   // (the probe's 20 cm ball misses once the top is under about 85 cm up: right at his reaching hands, measured in pictures)
		FHitResult Hand;
		const bool bHit = ProbeWall(C + FVector(0.f, 0.f, HandUp), Dir, R + 60.f + HandUp * FMath::Max(0.f, float(WallNormal.Z)), Hand);   // (starting inside rock, under an overhang, counts as rock)
		const bool bRockEnds = !bHit || (!Hand.bStartPenetrating && JeebClimb::IsClimbGround(Hand.GetComponent()) && Hand.ImpactNormal.Z >= GetWalkableFloorZ());   // (or it lies back to ground he can stand on)
		if (bRockEnds)
		{
			if (TryLedge()) return false;
			// no room over the top: no further up (along the wall and down still work)
			FHitResult Back;
			SafeMoveUpdatedComponent(OldLoc - C, UpdatedComponent->GetComponentQuat(), true, Back);
			Velocity = FVector::ZeroVector;
			C = UpdatedComponent->GetComponentLocation();
		}
	}
	bOk = ProbeWall(C, Dir, R + 40.f, H) && Climbable(H);
	if (!bOk && InUp <= 0.3f) bOk = ProbeWall(C + FVector(0.f, 0.f, -55.f), Dir, R + 40.f, H) && Climbable(H);   // (never going up: that climbed thin air)
	if (!bOk)
	{
		if (InUp > 0.3f && TryLedge()) return false;
		FHitResult W;
		if (ProbeWall(C, Dir, R + 40.f, W) && !W.bStartPenetrating && JeebClimb::IsClimbGround(W.GetComponent())
			&& W.ImpactNormal.Z >= GetWalkableFloorZ())
		{
			// the slope lies back enough to walk on: step off onto it
			Velocity = Dir * 50.f;
			bNoRegrab = true;   // (no grabbing the bank again until he has landed on ground he can walk on, 2026-11-20)
			SetMovementMode(MOVE_Falling);
			LogClimb(TEXT("onto walkable"));
			return false;
		}
		// the climbable part ends this way: undo the step
		FHitResult Back;
		SafeMoveUpdatedComponent(OldLoc - C, UpdatedComponent->GetComponentQuat(), true, Back);
		Velocity = FVector::ZeroVector;
		return true;
	}
	// follow the slope (eased so an 8 m grid's creases don't snap him round)
	WallNormal = (WallNormal + (H.ImpactNormal - WallNormal) * FMath::Min(1.f, 8.f * Dt)).GetSafeNormal();
	const FVector Nh = H.ImpactNormal;
	const float Dist = FVector::DotProduct(C - H.ImpactPoint, Nh);
	WallDist = Dist;
	const float Want = R + (HH - R) * FMath::Abs(float(Nh.Z)) + JeebClimb::WallGap;   // an upright capsule's nearest point to that plane, plus the gap
	const float Err = Dist - Want;
	if (FMath::Abs(Err) > 0.5f)
	{
		FHitResult Hug;
		const float Step = FMath::Clamp(Err * FMath::Min(1.f, 12.f * Dt), -40.f * Dt - 2.f, 200.f * Dt + 2.f);
		SafeMoveUpdatedComponent(-Nh * Step, UpdatedComponent->GetComponentQuat(), true, Hug);
		WallDist = Dist - Step;
	}
	return true;
}

void UKnightMovement::FaceWall(float Dt)
{
	const FVector F = Flat(-WallNormal);
	if (F.IsNearlyZero()) return;
	const FRotator Cur = UpdatedComponent->GetComponentRotation();
	const FRotator Want(0.f, F.Rotation().Yaw, 0.f);
	const FRotator New = Dt > 0.f ? FMath::RInterpTo(FRotator(0.f, Cur.Yaw, 0.f), Want, Dt, 10.f) : Want;
	MoveUpdatedComponent(FVector::ZeroVector, New.Quaternion(), false);
}

void UKnightMovement::PhysClimbJump(float Dt)
{
	const JeebTune::FValues& T = JeebTune::V();
	bJumpQueued = false;
	const float T0 = ActT;
	ActT = FMath::Min(ActT + Dt, ActDur);
	// a short load (0.1 s), then eased out and caught
	auto Share = [&](float X) { return Ease((X - 0.1f) / FMath::Max(ActDur - 0.1f, 0.01f)); };
	const float Step = (Share(ActT) - Share(T0)) * T.ClimbJumpDist;
	FVector Dir = FVector::VectorPlaneProject(ActDir, WallNormal).GetSafeNormal();
	if (Dir.IsNearlyZero()) Dir = WallUp(WallNormal);
	const FVector OldLoc = UpdatedComponent->GetComponentLocation();
	const FVector Delta = Dir * Step;
	bool bBlocked = false;
	if (!Delta.IsNearlyZero())
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
		if (Hit.IsValidBlockingHit() && !Climbable(Hit)) bBlocked = true;
	}
	Velocity = (UpdatedComponent->GetComponentLocation() - OldLoc) / Dt;
	const bool bUp = FVector::DotProduct(Dir, WallUp(WallNormal)) > 0.5f;
	if (!HugWall(Dt, bUp ? 1.f : 0.f, OldLoc)) return;   // over a ledge, or onto ground he can walk
	FaceWall(Dt);
	GiveXP();
	LogClimb(nullptr);
	if (bBlocked || ActT >= ActDur || (ActT > 0.2f && Velocity.IsNearlyZero()))   // (not during the load, when he hasn't moved yet)
	{
		Velocity = FVector::ZeroVector;
		SetMovementMode(MOVE_Custom, JeebClimb::Climb);
	}
}

// ---------------------------------------------------------------- the ledge climb-up and the mantle (scripted paths, checked first)

bool UKnightMovement::RoomFor(const FVector& Center) const
{
	const UCapsuleComponent* Cap = CharacterOwner->GetCapsuleComponent();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebClimbRoom), false, CharacterOwner);
	FCollisionResponseParams RP;
	Cap->InitSweepCollisionParams(Q, RP);
	return !GetWorld()->OverlapBlockingTestByChannel(Center, FQuat::Identity, Cap->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Cap->GetScaledCapsuleRadius() - 2.f, Cap->GetScaledCapsuleHalfHeight() - 2.f), Q, RP);
}

bool UKnightMovement::PlanLedge(const FVector& C, const FVector& N, FVector& OutTo, float& OutTop) const
{
	const FVector Fwd = Flat(-N);
	if (Fwd.IsNearlyZero()) return false;
	const float R = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebLedge), true, CharacterOwner);
	// a few spots past the edge, and a little higher over a top that slopes up (a capsule set 3 cm over a sloping top touches the slope behind)
	// (a bank that lies back, 2026-11-20: its walkable top can start well in from where his hands are, so the search follows the slope up,
	// every 30 cm out to 3 m, to the first ground he can stand on; the normal ledge climb-up then lifts him onto it)
	const bool bBank = N.Z > 0.3f;
	const float Reach = bBank ? 300.f : 70.f;
	TArray<float, TInlineAllocator<12>> Aheads = { R + 35.f, R + 20.f, R + 55.f };
	if (bBank) for (float A = R + 80.f; A <= R + 300.f; A += 30.f) Aheads.Add(A);
	for (const float Ahead : Aheads)
	{
		const FVector P = C + Fwd * Ahead;
		FHitResult D;
		if (!GetWorld()->LineTraceSingleByObjectType(D, FVector(P.X, P.Y, C.Z + HH + Reach), FVector(P.X, P.Y, C.Z - HH + 10.f), WorldObjects(), Q) || D.bStartPenetrating) continue;
		if (D.ImpactNormal.Z < GetWalkableFloorZ()) continue;   // the top must be ground he can stand on
		for (const float Lift : { 3.f, 20.f, 40.f })
		{
			const FVector To(P.X, P.Y, D.ImpactPoint.Z + HH + Lift);
			if (!RoomFor(To)) continue;
			// the way there: straight up beside the wall, then over the edge
			FHitResult B;
			const FVector UpPt(C.X, C.Y, To.Z);
			if (GetWorld()->SweepSingleByObjectType(B, C, UpPt, FQuat::Identity, WorldObjects(), FCollisionShape::MakeSphere(20.f), Q)) continue;
			if (GetWorld()->SweepSingleByObjectType(B, UpPt, To, FQuat::Identity, WorldObjects(), FCollisionShape::MakeSphere(20.f), Q)) continue;
			OutTop = D.ImpactPoint.Z;
			OutTo = To;
			return true;
		}
	}
	return false;
}

bool UKnightMovement::TryLedge()
{
	FVector To; float Top;
	if (!PlanLedge(UpdatedComponent->GetComponentLocation(), WallNormal, To, Top)) return false;
	StartScripted(JeebClimb::ClimbUp, To, JeebTune::V().ClimbUpTime + JeebTune::V().MantleStandTime, Top, Flat(-WallNormal));
	LogClimb(TEXT("ledge"));
	return true;
}

bool UKnightMovement::MantleNearby() const
{
	if (!CharacterOwner) return false;
	const float R = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	TArray<FOverlapResult> Hits;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebMantleNear), false, CharacterOwner);
	const float FeetZ = UpdatedComponent->GetComponentLocation().Z - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	GetWorld()->OverlapMultiByObjectType(Hits, UpdatedComponent->GetComponentLocation(), FQuat::Identity, WorldObjects(), FCollisionShape::MakeSphere(R + 70.f), Q);
	for (const FOverlapResult& O : Hits)
		if (MantleCandidate(O.GetComponent(), O.ItemIndex, FeetZ, true)) return true;
	return false;
}

// The general rule (2026-11-16): from the solid's own collision and size, the same on every machine (the world's solids are built the same
// everywhere from fixed seeds and data). No traces here: PlanMantle measures the real top, depth and landing once he tries.
bool UKnightMovement::MantleCandidate(const UPrimitiveComponent* C, int32 Item, float FeetZ, bool bFace) const
{
	if (!C || C->ComponentHasTag(JeebClimb::NoMantleTag) || C->ComponentHasTag(JeebClimb::ClimbTag)) return false;   // (opted out; climbable rock and hills climb instead)
	const AActor* A = C->GetOwner();
	// never people, animals or the horse, nor anything the server moves and sends (creatures, foes, dropped things): only the world every machine builds
	// for itself from the same seeds, so the server and the player's machine always agree
	if (A && (A->IsA<APawn>() || A->GetIsReplicated() || A->ActorHasTag(JeebClimb::NoMantleTag))) return false;
	if (C->IsSimulatingPhysics() || C->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) return false;   // nothing moving, nothing he walks through
	if (C->ComponentHasTag(JeebClimb::MantleTag)) return true;   // marked by hand (MoveLab's test fences and block)
	FBox B(ForceInit);
	if (const UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(C))
	{
		// one instance of a set (the town fill's solids are thousands of boxes in one component): that instance's own box
		FTransform X;
		if (Item < 0 || !I->GetStaticMesh() || !I->GetInstanceTransform(Item, X, true)) return false;
		B = I->GetStaticMesh()->GetBounds().GetBox().TransformBy(X);
	}
	else B = C->Bounds.GetBox();
	const JeebTune::FValues& T = JeebTune::V();
	const FVector S = B.GetSize();
	const float Long = FMath::Max(S.X, S.Y);
	if (Long < T.MantleMinWidth || Long > 2500.f) return false;   // a post, a pole or a sign; or the ground, a castle, a whole street
	if (B.Max.Z > FeetZ + T.MantleMax + 30.f) return false;        // taller than he gets over: buildings, trees, palisades, lamp posts, wells with roofs
	if (bFace && B.Min.Z > FeetZ + MaxStepHeight + 10.f) return false;   // the side he meets stands on his ground (not a beam, a deck or a balcony)
	return true;
}

bool UKnightMovement::PlanMantle(const FVector& C, const FVector& Fwd, bool bInAir, uint8& OutMode, FVector& OutTo, float& OutTop, FVector& OutFace, FVector* OutEdge, float* OutFar) const
{
	const JeebTune::FValues& T = JeebTune::V();
	if (OutFar) *OutFar = 0.f;
	const float R = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float FeetZ = C.Z - HH;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebMantle), true, CharacterOwner);
	auto No = [this](const TCHAR* Why, const UPrimitiveComponent* What = nullptr, float H = 0.f)   // (why not, for the log when Space finds nothing)
	{
		MantleWhy = FString::Printf(TEXT("%s (%s%s%.0f cm)"), Why, What && What->GetOwner() ? *What->GetOwner()->GetName() : TEXT("-"), TEXT(" "), H);
		return false;
	};
	auto Down = [&](const FVector& At, float From, float To, FHitResult& H)
	{
		return GetWorld()->LineTraceSingleByObjectType(H, FVector(At.X, At.Y, From), FVector(At.X, At.Y, To), WorldObjects(), Q) && !H.bStartPenetrating;
	};
	// its face: in front of him, knee to chest high. The first thing there decides: it must be something he may mantle
	FHitResult F;
	bool bFace = false;
	for (const float H : { 40.f, 80.f, 120.f })
	{
		if (!GetWorld()->LineTraceSingleByObjectType(F, FVector(C.X, C.Y, FeetZ + H), FVector(C.X, C.Y, FeetZ + H) + Fwd * (R + 50.f), WorldObjects(), Q)) continue;
		if (F.bStartPenetrating || !MantleCandidate(F.GetComponent(), F.Item, FeetZ, true)) return No(TEXT("not mantleable"), F.GetComponent());
		bFace = true;
		break;
	}
	if (!bFace) return No(TEXT("nothing ahead"));
	OutFace = Flat(-F.ImpactNormal);
	if (OutFace.IsNearlyZero() || FVector::DotProduct(OutFace, Fwd) < 0.6f || FMath::Abs(float(F.ImpactNormal.Z)) > 0.4f) return No(TEXT("not square on"), F.GetComponent());   // square enough to it, and a side, not a slope
	// its top, just past the face: flat enough, and itself something he may mantle (a crate on a crate counts: the stack's top)
	const FVector P = F.ImpactPoint + OutFace * 6.f;
	FHitResult D;
	if (!Down(P, FeetZ + T.MantleMax + 40.f, FeetZ, D) || D.ImpactNormal.Z < 0.5f || !MantleCandidate(D.GetComponent(), D.Item, FeetZ, false)) return No(TEXT("no flat top"), D.GetComponent() ? D.GetComponent() : F.GetComponent());
	OutTop = D.ImpactPoint.Z;
	if (OutEdge) *OutEdge = FVector(F.ImpactPoint.X, F.ImpactPoint.Y, OutTop);
	const float Height = OutTop - FeetZ;
	if (Height > T.MantleMax || Height < (bInAir ? 10.f : T.MantleMin)) return No(Height > T.MantleMax ? TEXT("too high") : TEXT("too low"), F.GetComponent(), Height);
	if (bInAir)
	{
		// in the air the limit still counts from the ground at the obstacle's foot, so a jump can't carry him onto something taller than MantleMax
		// (a 1.56 m round bale, a 1.6 m crate stack: 2026-11-16 test)
		FHitResult Gr;
		const FVector Foot = F.ImpactPoint - OutFace * (R + 5.f);
		if (Down(Foot, FeetZ + 5.f, FeetZ - 400.f, Gr) && OutTop - Gr.ImpactPoint.Z > T.MantleMax) return No(TEXT("too high from the ground"), F.GetComponent(), OutTop - float(Gr.ImpactPoint.Z));
	}
	// nothing in the way above it (a roof, a beam)
	FHitResult B;
	if (GetWorld()->LineTraceSingleByObjectType(B, FVector(C.X, C.Y, OutTop + 60.f), FVector(P.X, P.Y, OutTop + 60.f) + OutFace * (R + 40.f), WorldObjects(), Q)) return No(TEXT("blocked above"), B.GetComponent());
	// how far its top runs on: samples along it, until it drops away
	const float Need = 2.f * R + 10.f;
	float Depth = -1.f;   // (-1: deep enough to stand on)
	for (float Dd = 15.f; Dd <= Need + 0.5f; Dd += 15.f)
	{
		FHitResult S;
		if (!Down(P + OutFace * Dd, OutTop + 40.f, OutTop - 25.f, S) || FMath::Abs(float(S.ImpactPoint.Z) - OutTop) > 15.f || S.ImpactNormal.Z < GetWalkableFloorZ())
		{
			Depth = Dd - 8.f;
			break;
		}
	}
	// deep enough to stand on (a crate, a stack, a low wall's top, a fountain's rim): onto it
	if (Depth < 0.f)
	{
		const FVector On = P + OutFace * (R + 10.f);
		OutTo = FVector(On.X, On.Y, OutTop + HH + 3.f);
		OutMode = JeebClimb::MantleOnto;
		return RoomFor(OutTo);
	}
	// else over it (a fence, a thin wall, a barrel) to the ground just past its far side; never as close as the old fixed spot, so the vault keeps its stride
	for (const float Extra : { 12.f, 35.f, 60.f })
	{
		const FVector Land = P + OutFace * FMath::Max(Depth + R + Extra, 2.f * R + 30.f);
		FHitResult G;
		if (!Down(Land, OutTop + 40.f, FeetZ - 150.f, G) || G.ImpactNormal.Z < GetWalkableFloorZ()) continue;
		if (G.ImpactPoint.Z > OutTop - 30.f) continue;   // (onto something nearly as high beyond: not a landing)
		const FVector To(Land.X, Land.Y, G.ImpactPoint.Z + HH + 3.f);
		if (!RoomFor(To)) continue;
		// the way over, clear above the top to the landing (the vault moves without sweeping)
		if (GetWorld()->LineTraceSingleByObjectType(B, FVector(C.X, C.Y, OutTop + 50.f), FVector(To.X, To.Y, OutTop + 50.f), WorldObjects(), Q)) return No(TEXT("way over blocked"), B.GetComponent());
		OutTo = To;
		OutMode = JeebClimb::MantleOver;
		if (OutFar) *OutFar = float(FVector::DotProduct(FVector(P.X - C.X, P.Y - C.Y, 0.f), OutFace)) + Depth;   // (the far edge, from where he starts)
		return true;
	}
	return No(TEXT("no landing"), F.GetComponent(), Height);
}

bool UKnightMovement::TryMantle(bool bInAir)
{
	AKnightCharacter* K = Knight();
	if (!K || K->bClimbing || K->bSitting || K->bMounted || K->bKnockedDown || K->StunLeft > 0.f || K->bRolling || K->bSliding) return false;
	FVector Fwd = Flat(Acceleration);
	if (Fwd.IsNearlyZero()) { if (bInAir) return false; Fwd = Flat(UpdatedComponent->GetForwardVector()); }
	uint8 Mode; FVector To, Face; float Top;
	FVector Edge; float Far = 0.f;
	if (!PlanMantle(UpdatedComponent->GetComponentLocation(), Fwd, bInAir, Mode, To, Top, Face, &Edge, &Far)) return false;
	ActEdge = Edge; ActFar = Far;
	// the speed he carries out of a vault: from his keys (how hard and how square he pushes at it, at his top speed), not his velocity, which the bump
	// itself has just changed and which the two machines can see a hair apart (2026-11-16: the server corrected every online vault)
	const float Push = FMath::Clamp(float(Acceleration.Size2D()) / FMath::Max(GetMaxAcceleration(), 1.f), 0.f, 1.f) * FMath::Max(0.f, float(FVector::DotProduct(Flat(Acceleration), Face)));
	const float Speed = Push * 500.f;   // (his running speed; not GetMaxSpeed, which each machine eases frame by frame outside the moves)
	const JeebTune::FValues& T = JeebTune::V();
	StartScripted(Mode, To, Mode == JeebClimb::MantleOver ? T.MantleOverTime : T.MantleOntoTime + T.MantleStandTime, Top, Face);   // (onto a block he stands up on top before walking on: MantleStandTime)
	ActCarry = Speed * T.MantleCarry;
	LogClimb(Mode == JeebClimb::MantleOver ? TEXT("mantle over") : TEXT("mantle onto"));
	UE_LOG(LogTemp, Log, TEXT("JEEBMANTLE %s plan %s: top %.0f cm over his feet, from (%.0f,%.0f,%.0f) to (%.0f,%.0f,%.0f), %.0f cm on, speed %.0f%s"),
		CharacterOwner->HasAuthority() ? TEXT("server") : TEXT("client"), Mode == JeebClimb::MantleOver ? TEXT("over") : TEXT("onto"),
		Top - (ActFrom.Z - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), ActFrom.X, ActFrom.Y, ActFrom.Z, To.X, To.Y, To.Z,
		FVector::Dist2D(ActFrom, To), Speed, bInAir ? TEXT(" (in the air)") : TEXT(""));
	return true;
}

bool UKnightMovement::FindMantleEdge(const FVector& C, const FVector& Fwd, FVector& OutEdge, float* OutFar, FVector* OutFace) const
{
	uint8 Mode; FVector To, Face; float Top, Far = 0.f;
	const bool bOk = PlanMantle(C, Fwd, false, Mode, To, Top, Face, &OutEdge, &Far) || PlanMantle(C, Fwd, true, Mode, To, Top, Face, &OutEdge, &Far);
	if (bOk && OutFar) *OutFar = MantleFarPast(C, OutEdge, Face, Mode, Far);
	if (bOk && OutFace) *OutFace = Face;
	return bOk;
}

// How far past the near top edge the top runs (a vault: to its far edge; climbing onto: as good as all the way)
float UKnightMovement::MantleFarPast(const FVector& From, const FVector& Edge, const FVector& Face, uint8 Mode, float Far)
{
	if (Mode != JeebClimb::MantleOver) return 500.f;
	return FMath::Max(0.f, Far - float(FVector::DotProduct(FVector(Edge.X - From.X, Edge.Y - From.Y, 0.f), Face)));
}

float UKnightMovement::ScriptedHold() const
{
	return (IsCustom(JeebClimb::ClimbUp) || IsCustom(JeebClimb::MantleOnto)) ? JeebTune::V().MantleStandTime : 0.f;
}

void UKnightMovement::StartScripted(uint8 Mode, const FVector& To, float Dur, float Top, const FVector& Dir)
{
	ActFrom = UpdatedComponent->GetComponentLocation();
	ActTo = To; ActDir = Dir; ActT = 0.f; ActDur = FMath::Max(Dur, 0.1f); ActTop = Top; ActCarry = 0.f;
	ActHold = (Mode == JeebClimb::ClimbUp || Mode == JeebClimb::MantleOnto) ? JeebTune::V().MantleStandTime : 0.f;
	Velocity = FVector::ZeroVector;
	SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
	SetMovementMode(MOVE_Custom, Mode);
}

// A checked path (its end was tested free first), so it moves without sweeping: up beside the edge then over, or an arc over a fence
void UKnightMovement::PhysScripted(float Dt)
{
	const float HH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	ActT = FMath::Min(ActT + Dt, ActDur);
	const float U = FMath::Min(ActT / FMath::Max(ActDur - ActHold, 0.05f), 1.f);   // (the path ends early; the rest of the time he stands up on the spot)
	FVector P;
	// (2026-11-18 night, owner: "it looks like he's stepping up slightly and moving through the object". The vault's arc peaked at half the
	// height + 15 cm, so on a 70-80 cm bollard or parapet his feet passed 20-30 cm through its top, and the climb onto moved him over the edge
	// before he was up. Now the path is laid out from the obstacle itself: he rises until his feet are MantleClear over its top by the time his
	// leading foot (MantleFoot ahead of the capsule's middle) reaches its near edge, stays up there until his trailing foot is past its far edge,
	// then drops to the landing. Every number comes from the plan both machines make (ActEdge, ActFar, ActTop), so it predicts and corrects as before.)
	constexpr float MantleClear = 12.f, MantleFoot = 28.f;
	const FVector Dir2(ActDir.X, ActDir.Y, 0.f);
	const float D = float(FVector::DotProduct(FVector(ActTo.X - ActFrom.X, ActTo.Y - ActFrom.Y, 0.f), Dir2));   // (along the way he faces)
	const FVector Side = FVector(ActTo.X - ActFrom.X, ActTo.Y - ActFrom.Y, 0.f) - Dir2 * D;                       // (any small sideways part, spread evenly)
	const float NearX = FMath::Clamp(float(FVector::DotProduct(FVector(ActEdge.X - ActFrom.X, ActEdge.Y - ActFrom.Y, 0.f), Dir2)), 0.f, FMath::Max(D, 0.f));
	const float Up = FMath::Max(0.f, float(ActTop - (ActFrom.Z - HH))) + MantleClear;   // capsule rise that puts his feet clear over the top
	auto Place = [&](float X, float Z, float SideU) { return FVector(ActFrom.X, ActFrom.Y, ActFrom.Z) + Dir2 * X + Side * SideU + FVector(0.f, 0.f, Z); };
	if (IsCustom(JeebClimb::MantleOver))
	{
		// rise (to U1), over the top (to U2), down to the landing
		constexpr float U1 = 0.38f, U2 = 0.62f;
		const float X1 = FMath::Max(NearX - MantleFoot, 0.f);
		const float X2 = FMath::Clamp(ActFar + MantleFoot, X1, D);
		const float LandZ = float(ActTo.Z - ActFrom.Z);
		float X, Z;
		if (U <= U1) { const float A = U / U1; X = X1 * A; Z = Up * Ease(A); }
		else if (U <= U2) { const float A = (U - U1) / (U2 - U1); X = FMath::Lerp(X1, X2, A); Z = Up + 6.f * FMath::Sin(PI * A); }
		else { const float A = (U - U2) / (1.f - U2); X = FMath::Lerp(X2, D, A); Z = FMath::Lerp(Up, LandZ, Ease(A)); }
		P = Place(X, Z, D > 1.f ? X / D : U);
	}
	else if (IsCustom(JeebClimb::MantleOnto))
	{
		// up beside the edge first (hands planted on the top), then over onto it once his feet are clear, then settle to standing height
		constexpr float U1 = 0.5f;
		const float X1 = FMath::Max(NearX - MantleFoot, 0.f);
		const float EndZ = float(ActTo.Z - ActFrom.Z);
		const float Peak = FMath::Max(Up, EndZ);
		float X, Z;
		if (U <= U1) { const float A = U / U1; X = X1 * Ease(A); Z = Peak * Ease(A); }
		else { const float A = (U - U1) / (1.f - U1); X = FMath::Lerp(X1, D, Ease(A)); Z = FMath::Lerp(Peak, EndZ, Ease(A)); }
		P = Place(X, Z, D > 1.f ? X / D : U);
	}
	else
	{
		const bool bUp = IsCustom(JeebClimb::ClimbUp);
		const float Rise = Ease(U / (bUp ? 0.6f : 0.55f));
		const float Over = Ease((U - (bUp ? 0.45f : 0.35f)) / (bUp ? 0.55f : 0.65f));
		P = FVector(FMath::Lerp(ActFrom.X, ActTo.X, Over), FMath::Lerp(ActFrom.Y, ActTo.Y, Over), FMath::Lerp(ActFrom.Z, ActTo.Z, Rise));
	}
	const FVector Cur = UpdatedComponent->GetComponentLocation();
	const FRotator Want(0.f, ActDir.Rotation().Yaw, 0.f);
	const FRotator Rot = FMath::RInterpTo(FRotator(0.f, UpdatedComponent->GetComponentRotation().Yaw, 0.f), Want, Dt, 12.f);
	MoveUpdatedComponent(P - Cur, Rot.Quaternion(), false);
	Velocity = (P - Cur) / Dt;
	if (IsCustom(JeebClimb::ClimbUp)) GiveXP();
	LogClimb(nullptr);
	if (ActT >= ActDur)
	{
		Velocity = IsCustom(JeebClimb::MantleOver) ? ActDir * ActCarry : FVector::ZeroVector;
		SetMovementMode(MOVE_Walking);
		LogClimb(TEXT("done"));
	}
}

// Climbing XP (server only): for new height above the best he has reached on this grab, ClimbXPPerMetre a metre
void UKnightMovement::GiveXP()
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority()) return;
	const float Z = UpdatedComponent->GetComponentLocation().Z;
	if (Z <= ClimbBestZ) return;
	XPCarry += (Z - ClimbBestZ) / 100.f * JeebTune::V().ClimbXPPerMetre;
	ClimbBestZ = Z;
	if (XPCarry >= 1.f)
	{
		const int32 N = FMath::FloorToInt(XPCarry);
		XPCarry -= N;
		ClimbXPPending += N;   // (given in one lump when the climb ends: FlushClimbXP. A drop every 25 cm kept the skill popup sliding in, owner 2026-11-13)
	}
}

void UKnightMovement::FlushClimbXP()
{
	if (ClimbXPPending <= 0) return;
	const int32 N = ClimbXPPending;
	ClimbXPPending = 0;
	if (!CharacterOwner || !CharacterOwner->HasAuthority()) return;
	if (AKnightCharacter* K = Knight()) if (USkillComponent* S = K->GetSkills()) S->AddXP(ESkillType::Climbing, N);
}

void UKnightMovement::LogClimb(const TCHAR* What) const
{
	if (!CharacterOwner || !UpdatedComponent) return;
	UKnightMovement* Self = const_cast<UKnightMovement*>(this);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!What && Now - Self->LogT < 0.5) return;   // while moving: twice a second
	Self->LogT = float(Now);
	const FVector L = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const TCHAR* Role = CharacterOwner && CharacterOwner->HasAuthority() ? TEXT("server") : TEXT("client");
	const bool bMantle = InMantle() || (What && FCString::Strstr(What, TEXT("mantle")));
	UE_LOG(LogTemp, Log, TEXT("%s %s %s mode=%d stam=%.2f%s N=(%.2f,%.2f,%.2f) gap=%.1f v=%.0f at=(%.0f,%.0f,%.0f) act=%.2f"),
		bMantle ? TEXT("JEEBMANTLE") : TEXT("JEEBCLIMB"), Role, What ? What : TEXT("-"), int32(ClimbMode()), ClimbStamina, bClimbExhausted ? TEXT(" tired") : TEXT(""),
		WallNormal.X, WallNormal.Y, WallNormal.Z, WallDist - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius(), Velocity.Size(), L.X, L.Y, L.Z, ActionShare());
}

// ---------------------------------------------------------------- online: corrections carry the climbing state

void FKnightMoveResponse::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(CharacterMovement, PendingAdjustment);
	const UKnightMovement& K = static_cast<const UKnightMovement&>(CharacterMovement);
	Stamina = K.ClimbStamina; RefillWait = K.RefillWait; bExhausted = K.bClimbExhausted; bNoRegrab = K.bNoRegrab; WallNormal = K.WallNormal;
	ActFrom = K.ActFrom; ActTo = K.ActTo; ActDir = K.ActDir; ActT = K.ActT; ActDur = K.ActDur; ActTop = K.ActTop; ActCarry = K.ActCarry; ActHold = K.ActHold;
	SwimStamina = K.SwimStamina; Breath = K.Breath; SwimRefillWait = K.SwimRefillWait; DrownT = K.DrownT; bHeadUnder = K.bHeadUnder;
	StrafeSprint = K.MoveStrafeSprint; AirMul = K.MoveAirMul; ActEdge = K.ActEdge; ActFar = K.ActFar;
	if (K.MovementMode == MOVE_Custom) bHasRotation = true;   // on the wall his facing is part of the state
}

bool FKnightMoveResponse::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!FCharacterMoveResponseDataContainer::Serialize(CharacterMovement, Ar, PackageMap)) return false;
	if (IsCorrection())
	{
		bool bOk = true;
		Ar << Stamina;
		Ar.SerializeBits(&bExhausted, 1);
		Ar.SerializeBits(&bNoRegrab, 1);
		Ar << RefillWait;
		FVector3f V[4] = { FVector3f(WallNormal), FVector3f(ActFrom), FVector3f(ActTo), FVector3f(ActDir) };   // (single precision: plenty at the world's size)
		for (FVector3f& X : V) Ar << X;
		if (Ar.IsLoading()) { WallNormal = FVector(V[0]); ActFrom = FVector(V[1]); ActTo = FVector(V[2]); ActDir = FVector(V[3]); }
		Ar << ActT << ActDur << ActTop << ActCarry << ActHold;
		Ar << SwimStamina << Breath << SwimRefillWait << DrownT;
		Ar.SerializeBits(&bHeadUnder, 1);
		Ar << StrafeSprint << AirMul << ActFar;
		FVector3f E(ActEdge);
		Ar << E;
		if (Ar.IsLoading()) ActEdge = FVector(E);
		if (!bOk) return false;
	}
	return !Ar.IsError();
}

void UKnightMovement::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	Super::ClientHandleMoveResponse(MoveResponse);   // (may change the movement mode; the replay runs later, from what is set below)
	if (!MoveResponse.IsCorrection()) return;
	++NetFixes;
	UE_LOG(LogTemp, Log, TEXT("JEEBNETFIX client correction %d mode %d at (%.0f,%.0f,%.0f)"), NetFixes, int32(ClimbMode()), MoveResponse.ClientAdjustment.NewLoc.X, MoveResponse.ClientAdjustment.NewLoc.Y, MoveResponse.ClientAdjustment.NewLoc.Z);
	const FKnightMoveResponse& R = static_cast<const FKnightMoveResponse&>(MoveResponse);
	ClimbStamina = R.Stamina; RefillWait = R.RefillWait; bClimbExhausted = R.bExhausted; bNoRegrab = R.bNoRegrab; WallNormal = R.WallNormal;
	ActFrom = R.ActFrom; ActTo = R.ActTo; ActDir = R.ActDir; ActT = R.ActT; ActDur = R.ActDur; ActTop = R.ActTop; ActCarry = R.ActCarry; ActHold = R.ActHold;
	SwimStamina = R.SwimStamina; Breath = R.Breath; SwimRefillWait = R.SwimRefillWait; DrownT = R.DrownT; bHeadUnder = R.bHeadUnder;
	MoveStrafeSprint = R.StrafeSprint; MoveAirMul = R.AirMul; ActEdge = R.ActEdge; ActFar = R.ActFar;
}

void FSavedMove_Knight::Clear()
{
	Super::Clear();
	bSavedLetGo = false; SavedMode = 0; bSavedExhausted = false; bSavedRefilling = false; bSavedFast = false; bSavedRise = false;
	SavedStrafeSprint = 1.f; SavedAirMul = 1.f; SavedAccel = 0.f;
}

// A merged move is run again on the client from the older move's start (the engine puts back where he was and how fast he went): the
// eased speed shares go back with it, so the merged move advances them once over the whole time, exactly as the server will
void FSavedMove_Knight::CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation)
{
	Super::CombineWith(OldMove, InCharacter, PC, OldStartLocation);
	const FSavedMove_Knight* O = static_cast<const FSavedMove_Knight*>(OldMove);
	if (UKnightMovement* K = Cast<UKnightMovement>(InCharacter->GetCharacterMovement()))
	{
		K->MoveStrafeSprint = O->SavedStrafeSprint;
		K->MoveAirMul = O->SavedAirMul;
	}
}

uint8 FSavedMove_Knight::GetCompressedFlags() const
{
	return uint8(Super::GetCompressedFlags() | (bSavedLetGo ? uint8(FLAG_Custom_0) : uint8(0)) | (bSavedFast ? uint8(FLAG_Custom_1) : uint8(0))
		| (bSavedRise ? uint8(FLAG_Custom_2) : uint8(0)));
}

bool FSavedMove_Knight::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Knight* O = static_cast<const FSavedMove_Knight*>(NewMove.Get());
	if (O->bSavedFast != bSavedFast || O->bSavedRise != bSavedRise) return false;
	if (!FMath::IsNearlyEqual(O->SavedAccel, SavedAccel)) return false;   // (the push was scaled with a different start-up rule: the server checks the merged move with the older one's)
	if (O->bSavedLetGo != bSavedLetGo || O->SavedMode != SavedMode || O->bSavedExhausted != bSavedExhausted || SavedMode != 0 || bSavedRefilling || O->bSavedRefilling) return false;   // (the bar refilling on the ground runs per move: never merged)
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Knight::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	if (const UKnightMovement* K = Cast<UKnightMovement>(C->GetCharacterMovement()))
	{
		bSavedLetGo = K->bWantsLetGo;
		SavedMode = K->ClimbMode();
		bSavedExhausted = K->bClimbExhausted;
		bSavedRefilling = K->ClimbStamina < 1.f || K->RefillWait > 0.f || K->SwimStamina < 1.f || K->SwimRefillWait > 0.f || K->Breath < 1.f;
		bSavedFast = K->bWantsFast;
		bSavedRise = K->bWantsRise;
		SavedAccel = K->GetMaxAcceleration();   // (frozen for this move: UKnightMovement::ControlledCharacterMove)
	}
}

// (also after a merge, from the older move's start: the engine calls it again then)
void FSavedMove_Knight::SetInitialPosition(ACharacter* C)
{
	Super::SetInitialPosition(C);
	if (const UKnightMovement* K = Cast<UKnightMovement>(C->GetCharacterMovement()))
	{
		SavedStrafeSprint = K->MoveStrafeSprint;
		SavedAirMul = K->MoveAirMul;
	}
}

void FSavedMove_Knight::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (UKnightMovement* K = Cast<UKnightMovement>(C->GetCharacterMovement())) { K->bWantsLetGo = bSavedLetGo; K->bWantsFast = bSavedFast; K->bWantsRise = bSavedRise; }
}

// What the last move ran into: the grab and the air mantle only look when it was rock or a fence (cheap for a server with many players)
void UKnightMovement::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	if (const UPrimitiveComponent* P = Hit.GetComponent())
	{
		if (JeebClimb::IsClimbGround(P)) bTouchRock = true;   // (any touch: where the capsule meets a crease its hit normal can read walkable; TryGrab probes the real face)
		else if (!bTouchMantle && CharacterOwner && UpdatedComponent
			&& MantleCandidate(P, Hit.Item, UpdatedComponent->GetComponentLocation().Z - CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), true)) bTouchMantle = true;   // (no traces: its own size)
	}
}
