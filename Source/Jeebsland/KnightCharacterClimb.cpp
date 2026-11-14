#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "KnightAnimator.h"
#include "SkillComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Free climbing and the mantle (2026-11-12): the knight's side. The movement itself (grabbing, the climb, its stamina, the climb jump,
// the ledge climb-up, the mantle) is UKnightMovement (KnightMovement.cpp), inside Unreal's movement prediction; the pose is
// KnightAnimatorClimb.cpp. The ladder (KnightCharacterMovement.cpp) is separate and unchanged.

UKnightMovement* AKnightCharacter::KnightMove() const { return Cast<UKnightMovement>(GetCharacterMovement()); }

bool AKnightCharacter::InClimbMove() const
{
	const UKnightMovement* KM = KnightMove();
	return KM && (KM->InClimbMove() || KM->InWater());   // (swimming too: no swings, guard, bow, roll, slide, drinking or interacting in the water)
}

bool AKnightCharacter::ClimbStowed() const
{
	const UKnightMovement* KM = KnightMove();
	return KM && (KM->InClimbMove() || KM->InWater());   // (the mantles too: his hands go on the fence or the block, not round a sword; and swimming)
}

void AKnightCharacter::DropFromWall(bool bAll)
{
	if (UKnightMovement* KM = KnightMove()) KM->ForceDrop(bAll);
}

void AKnightCharacter::JumpFromMove(float Dt)
{
	UCharacterMovementComponent* M = GetCharacterMovement();
	if (M && M->DoJump(bClientUpdating, Dt))
	{
		JumpCurrentCount++;
		OnJumped();
	}
	else bPressedJump = false;
}

// Every frame: nothing else runs on a rock face or in a mantle (no crouch, guard, bow draw or spear charge); and the test switches
void AKnightCharacter::TickWallClimb(float Dt)
{
	TickClimbTests(Dt);
	TickSwimTests(Dt);
	if (UKnightMovement* KM = KnightMove(); KM && IsLocallyControlled()) KM->bWantsFast = bSprinting;   // (Shift: the fast swim; carried by his moves, FLAG_Custom_1)
	// Drowning (server only): the bar or the breath ran out (KnightMovementSwim.cpp), he has sunk for DrownSinkTime, and he dies like any death
	if (UKnightMovement* KM = KnightMove(); KM && HasAuthority())
	{
		KM->ServerWaterBackstop(Dt);   // (also resets its clocks out of the water)
		if (KM->InWater() && KM->DrownedOut() && Health > 0.f)
		{
			if (Skills) Skills->Say(5.f, FColor::Red, TEXT("You drowned."));
			Die();
			return;
		}
	}
	if (!InClimbMove()) return;
	bCrouched = false;
	bBlocking = false;
	bCharging = false;
	if (Bow != EBow::None) CancelDraw();
}

// Test pictures (standalone only). On the rock the keys are the rock's own directions, so a plain -SuitWalk can't climb:
//   -ClimbKeys=<up>_<right>_<from>_<for>[x...]   up and right in hundredths of full (-100..100), times in hundredths of a second from the start
//   -ClimbDo=jump<at>[x...]  letgo<at>[x...]  space<at>   (hundredths): a climb jump (Space on the rock), Ctrl for 0.3 s, or Space (a mantle by a fence)
void AKnightCharacter::TickClimbTests(float Dt)
{
	if (!bShotFace || !IsLocallyControlled() || GetNetMode() != NM_Standalone) return;
	static FString Keys, Do;
	static bool bRead = false;
	static float T = 0.f;
	static TSet<int32> Done;
	if (!bRead)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ClimbKeys="), Keys);
		FParse::Value(FCommandLine::Get(), TEXT("ClimbDo="), Do);
		bRead = true;
	}
	T += Dt;
	UKnightMovement* KM = KnightMove();
	if (!KM) return;
	TArray<FString> Segs;
	Keys.ParseIntoArray(Segs, TEXT("x"));
	for (const FString& S : Segs)
	{
		TArray<FString> P;
		if (S.ParseIntoArray(P, TEXT("_")) != 4) continue;
		const float From = FCString::Atoi(*P[2]) / 100.f, For = FCString::Atoi(*P[3]) / 100.f;
		if (T < From || T > From + For || !KM->OnWall()) continue;
		AddMovementInput(UKnightMovement::WallUp(KM->WallNormal), FCString::Atoi(*P[0]) / 100.f);
		AddMovementInput(UKnightMovement::WallRight(KM->WallNormal), FCString::Atoi(*P[1]) / 100.f);
	}
	Do.ParseIntoArray(Segs, TEXT("x"));
	for (int32 i = 0; i < Segs.Num(); ++i)
	{
		const FString& S = Segs[i];
		const int32 Num = S.StartsWith(TEXT("letgo")) ? 5 : S.StartsWith(TEXT("space")) || S.StartsWith(TEXT("jump")) ? (S.StartsWith(TEXT("jump")) ? 4 : 5) : -1;
		if (Num < 0 || Done.Contains(i) || T < FCString::Atoi(*S.Mid(Num)) / 100.f) continue;
		Done.Add(i);
		UE_LOG(LogTemp, Log, TEXT("JEEBCLIMB test %s at %.2f mode %d"), *S, T, int32(KM->ClimbMode()));
		if (S.StartsWith(TEXT("letgo")))
		{
			StartCrouch();
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]() { StopCrouch(); }), 0.3f, false);
		}
		else
		{
			Jump();
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]() { StopJumping(); }), 0.15f, false);
		}
	}
}

// What the animator needs of the climb, from what every machine knows: the movement mode (replicated), the speed, and the rock
// (the server's and his own machine's movement have it; others' machines find it with their own trace)
void AKnightCharacter::FillWallInput(JeebAnim::FInput& In) const
{
	FillSwimInput(In);   // (KnightMovementSwim.cpp)
	const UKnightMovement* KM = KnightMove();
	if (!KM || !KM->InClimbMove()) { MantleEdgeMode = 0; return; }
	const JeebTune::FValues& T = JeebTune::V();
	In.WallMode = KM->ClimbMode();
	In.WallStamina = GetLocalRole() == ROLE_SimulatedProxy ? 1.f : KM->ClimbStamina;
	In.ActDur = In.WallMode == JeebClimb::Jump ? T.ClimbJumpTime : In.WallMode == JeebClimb::ClimbUp ? T.ClimbUpTime
		: In.WallMode == JeebClimb::MantleOver ? T.MantleOverTime : In.WallMode == JeebClimb::MantleOnto ? T.MantleOntoTime : 1.f;
	In.ActHold = GetLocalRole() == ROLE_SimulatedProxy ? KM->ScriptedHold() : KM->ActHold;   // (others' machines don't run the move: tuning)
	const FVector C = GetActorLocation();
	FVector N = KM->WallNormal, P = C - KM->WallNormal * KM->WallDist;
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		FHitResult H;
		const FVector F = GetActorForwardVector();
		if (KM->ProbeWall(C, F, 110.f, H) && H.bBlockingHit && !H.bStartPenetrating) { N = H.ImpactNormal; P = H.ImpactPoint; }
		else { N = -F; P = C + F * 46.f; }
	}
	In.WallN = N;
	In.WallPoint = P;
	if (In.WallMode == JeebClimb::MantleOver || In.WallMode == JeebClimb::MantleOnto)
	{
		// (the fence's top edge, where the hands go: the moving machines kept it from the move's plan, others find it once as the mantle starts)
		if (GetLocalRole() != ROLE_SimulatedProxy) MantleEdge = KM->ActEdge;
		else if (MantleEdgeMode != In.WallMode && !KM->FindMantleEdge(C, GetActorForwardVector(), MantleEdge)) MantleEdge = FVector::ZeroVector;
		In.WallPoint = MantleEdge;
		In.bMantleEdge = !MantleEdge.IsZero();
	}
	MantleEdgeMode = In.WallMode;
	// the surface under each hand and boot (not on a dedicated server: there the rock's plane does)
	if ((In.WallMode == JeebClimb::Climb || In.WallMode == JeebClimb::Jump) && Anim.IsValid() && !IsNetMode(NM_DedicatedServer))
	{
		static const int32 Ends[4] = { JeebAnim::LHand, JeebAnim::RHand, JeebAnim::LFoot, JeebAnim::RFoot };
		FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebLimb), false, this);
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector At = Anim->BoneWorld(Ends[i]).GetLocation();
			FHitResult H;
			if (GetWorld()->LineTraceSingleByObjectType(H, At + N * 30.f, At - N * 45.f, FCollisionObjectQueryParams(ECC_WorldStatic), Q) && !H.bStartPenetrating && H.ImpactNormal.Z < 0.75f)
			{ In.LimbP[i] = H.ImpactPoint; In.LimbN[i] = H.ImpactNormal; In.bLimbHit[i] = true; }
		}
	}
	const FVector Up = UKnightMovement::WallUp(N), Rt = UKnightMovement::WallRight(N);
	In.WallUpV = FVector::DotProduct(In.Vel, Up);
	In.WallRightV = FVector::DotProduct(In.Vel, Rt);
	if (In.WallMode == JeebClimb::Jump)
	{
		const FVector D = GetLocalRole() == ROLE_SimulatedProxy ? In.Vel : KM->ActDir;
		const FVector2D J(FVector::DotProduct(D, Rt), FVector::DotProduct(D, Up));
		In.WallJump = J.Size() > 0.05f ? J.GetSafeNormal() : FVector2D(0.f, 1.f);
	}
}
