#include "KnightCharacter.h"
#include "KnightMovement.h"
#include "KnightAnimator.h"
#include "SkillComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Terrain.h"
#include "JeebWater.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

// The mantle in the real map (2026-11-16; not in Shipping):
//   -MantleAt=<x>_<y>_<yaw>_<back>_<share>   (test pictures) as the pictures start he stands <back> cm short of (x, y) facing <yaw>, on the ground there, and
//                                            0.6 s later walks that way at <share> hundredths of full speed (100 runs) for 3 s; [_<side>_<up>]: a still camera side-on, <side> cm to his right (negative: left; default 550), <up> cm up (150).
//   -NetMantle=<x>_<y>_<yaw>_<back>_<share>  (online, on the server and the client): the server stands the player there 4 s after he arrives; his machine walks
//                                            him that way from 8 s to 11 s and logs JEEBNETFIX, the corrections it took while walking (0: the two agreed).
void AKnightCharacter::TickMantleTests(float Dt)
{
#if !UE_BUILD_SHIPPING
	static FString AtS, NetS;
	// -BankClimb (standalone pictures) / -BankClimbNet (server and client), 2026-11-20: finds a steep, tall bank near Oak Hill's plaza (dry at its
	// foot) from the ground's own heights, the same on every machine, and runs the -MantleAt / -NetMantle test there, pushing uphill for 9 s.
	static const bool bBankNet = FParse::Param(FCommandLine::Get(), TEXT("BankClimbNet"));
	static const bool bBank = bBankNet || FParse::Param(FCommandLine::Get(), TEXT("BankClimb"));
	static bool bBankDone = false;
	static float BankTopZ = 0.f;   // (the ground 3.5 m up the slope from its foot: he has to end standing at least about this high)
	if (bBank && !bBankDone)
	{
		bBankDone = true;
		float Best = 0.f; FString Spot; float MaxRise = 0.f; int32 NSteep = 0, NTall = 0;
		const FVector2D Plaza(-3550.f, 18290.f);
		for (float X = Plaza.X - 15000.f; X <= Plaza.X + 15000.f; X += 150.f)
			for (float Y = Plaza.Y - 15000.f; Y <= Plaza.Y + 15000.f; Y += 150.f)
				for (int32 a = 0; a < 16; ++a)
				{
					const FVector2D D = FVector2D(FRotator(0.f, a * 22.5f, 0.f).Vector());
					const float Z0 = JeebTerrain::HeightAt(X, Y);
					const FVector2D F = FVector2D(X, Y) + D * 100.f;
					const float Z1 = JeebTerrain::HeightAt(F.X, F.Y);
					MaxRise = FMath::Max(MaxRise, Z1 - Z0); if (Z1 - Z0 < 108.f) continue;   // steep: over 47 degrees (walkable stops at about 45)
					++NSteep;
					const float Z3 = JeebTerrain::HeightAt(X + D.X * 350.f, Y + D.Y * 350.f);
					const float Zb = JeebTerrain::HeightAt(X - D.X * 200.f, Y - D.Y * 200.f);
					if (Z3 - Z0 < 160.f || Zb - Z0 > 40.f || FVector2D::Distance(FVector2D(X, Y), FVector2D(-4450.f, 13040.f)) < 2500.f) continue;   // tall ahead, not in a dip (and not the stone bridge's abutment)
					++NTall; float S;
					if (JeebWater::DepthAt(FVector(X, Y, Z0 + 1.f), S) > 0.f || JeebWater::DepthAt(FVector(X - D.X * 150.f, Y - D.Y * 150.f, Zb + 1.f), S) > 40.f) continue;   // (wading at most, not swimming)
					const float Score = (Z3 - Z0) - FVector2D::Distance(FVector2D(X, Y), Plaza) * 0.02f;
					if (Score > Best - 60.f) UE_LOG(LogTemp, Log, TEXT("JEEBCLIMB bank candidate %.0f,%.0f yaw %.0f rise %.0f score %.0f"), X, Y, a * 22.5f, Z3 - Z0, Score);
					if (Score > Best)
					{
						// (straight up the slope, from the ground's own gradient where he meets it, not the 22.5-degree step the search used)
						const FVector2D Gr(JeebTerrain::HeightAt(F.X + 50.f, F.Y) - JeebTerrain::HeightAt(F.X - 50.f, F.Y), JeebTerrain::HeightAt(F.X, F.Y + 50.f) - JeebTerrain::HeightAt(F.X, F.Y - 50.f));
						const float UpYaw = Gr.IsNearlyZero() ? a * 22.5f : FMath::RadiansToDegrees(FMath::Atan2(Gr.Y, Gr.X));
						Best = Score; BankTopZ = Z3; Spot = FString::Printf(TEXT("%.0f_%.0f_%.0f_150_100_-750_160"), F.X, F.Y, UpYaw);
					}
				}
		UE_LOG(LogTemp, Log, TEXT("JEEBCLIMB bank test spot: %s (score %.0f; steepest rise %.0f cm per m, %d steep, %d tall)"), Spot.IsEmpty() ? TEXT("none") : *Spot, Best, MaxRise, NSteep, NTall);
		(bBankNet ? NetS : AtS) = Spot;
	}
	static const bool bAt = bBank ? (!bBankNet && !AtS.IsEmpty()) : FParse::Value(FCommandLine::Get(), TEXT("MantleAt="), AtS);
	static const bool bNet = bBank ? (bBankNet && !NetS.IsEmpty()) : FParse::Value(FCommandLine::Get(), TEXT("NetMantle="), NetS);
	const float WalkFor = bBank ? 9.f : 3.f;
	// (the bank test's verdict, 2026-11-20: standing on walkable ground on top of the bank, and only the one grab, no regrab)
	auto BankVerdict = [&](const TCHAR* Who)
	{
		const UKnightMovement* M = KnightMove();
		if (!M) return;
		const float Feet = float(GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const bool bFloor = M->IsMovingOnGround() && M->CurrentFloor.IsWalkableFloor();
		const bool bPass = bFloor && Feet >= BankTopZ - 60.f && M->Grabs == 1;
		UE_LOG(LogTemp, Log, TEXT("JEEBBANKTEST %s %s: feet %.0f vs top %.0f, on walkable ground %d, grabs %d, mode %d at (%.0f,%.0f)"), Who, bPass ? TEXT("PASS") : TEXT("FAIL"), Feet, BankTopZ, bFloor ? 1 : 0, M->Grabs, int32(M->MovementMode), GetActorLocation().X, GetActorLocation().Y);
	};
	const bool bUpTop = bBank && KnightMove() && KnightMove()->Grabs > 0 && KnightMove()->IsMovingOnGround();   // (over the top: stop pushing)
	if (!bAt && !bNet) return;
	// (-MantleAt takes several, joined by 'x': one every 5 s, each filmed by a still camera side-on to it, <side> cm off (a 6th field, default 450;
	// negative: from the other side). The pictures' frame k*75 is where the k-th starts, at 15 pictures a second)
	static float AtT = -1.f;
	TArray<FString> Segs;
	(bNet ? NetS : AtS).ParseIntoArray(Segs, TEXT("x"));
	const int32 Seg = bNet ? 0 : FMath::Clamp(int32(FMath::Max(AtT, 0.f) / 5.f), 0, FMath::Max(Segs.Num() - 1, 0));
	TArray<FString> P;
	if (Segs.IsValidIndex(Seg)) Segs[Seg].ParseIntoArray(P, TEXT("_"));
	if (P.Num() < 5) return;
	const FVector2D Spot(FCString::Atof(*P[0]), FCString::Atof(*P[1]));
	const float Yaw = FCString::Atof(*P[2]), Back = FCString::Atof(*P[3]), Share = FCString::Atoi(*P[4]) / 100.f;
	const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
	auto Place = [&]()
	{
		const FVector2D At = Spot - FVector2D(Dir) * Back;
		const float HH = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebMantleTest), true, this);
		const float Top = JeebTerrain::HeightAt(At.X, At.Y) + 400.f;
		const float Z = GetWorld()->LineTraceSingleByChannel(H, FVector(At.X, At.Y, Top), FVector(At.X, At.Y, Top - 1500.f), ECC_Pawn, Q) ? float(H.ImpactPoint.Z) : Top - 400.f;
		SetActorLocationAndRotation(FVector(At.X, At.Y, Z + HH + 3.f), FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->Velocity = FVector::ZeroVector;
		ShotFaceYaw = Yaw;
		UE_LOG(LogTemp, Log, TEXT("JEEBMANTLE test placed at (%.0f,%.0f,%.0f) facing %.0f, %.0f cm short of (%.0f,%.0f)"), At.X, At.Y, Z, Yaw, Back, Spot.X, Spot.Y);
	};
	if (bAt)
	{
		if (!bShotFace || !IsLocallyControlled() || GetNetMode() != NM_Standalone) return;
		static int32 Placed = -1, Filmed = -1;
		if (AtT < 0.f) AtT = 0.f;
		if (Placed != Seg) { Placed = Seg; Place(); }
		if (Filmed != Seg && AtT - Seg * 5.f >= 0.55f)   // (the camera once he has settled on the ground: placed from a height, he drops first)
		{
			Filmed = Seg;
			const float Side = P.Num() >= 6 ? FCString::Atof(*P[5]) : 550.f;
			const float Up = P.Num() >= 7 ? FCString::Atof(*P[6]) : 150.f;   // (a 7th field: the camera's height over his ground)
			const float G = float(GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const FVector Look(Spot.X, Spot.Y, G + (bBank ? 170.f : 80.f));   // (the bank: halfway up it)
			const FVector Eye = bBank ? FVector(Spot.X, Spot.Y, G + 260.f) - FVector(Dir) * 700.f + FRotator(0.f, Yaw + 90.f, 0.f).Vector() * 350.f   // (the bank: from the river, a little to one side)
				: FVector(Spot.X, Spot.Y, G + Up) - FVector(Dir) * 40.f + FRotator(0.f, Yaw + 90.f, 0.f).Vector() * Side;
			if (APlayerController* PC = Cast<APlayerController>(Controller))
				if (ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(Eye, (Look - Eye).Rotation()))
				{
					Cam->GetCameraComponent()->SetFieldOfView(55.f);
					Cam->GetCameraComponent()->bConstrainAspectRatio = false;
					PC->SetViewTarget(Cam);
				}
			UE_LOG(LogTemp, Log, TEXT("JEEBMANTLE test %d starts at %.2f s (picture %d)"), Seg, AtT, FMath::RoundToInt(AtT * 15.f));
		}
		const float T = AtT - Seg * 5.f;
		AtT += Dt;
		if (bBank && T >= 1.f + WalkFor && T - Dt < 1.f + WalkFor) BankVerdict(TEXT("standalone"));
		if (T > 0.6f && T < 0.6f + WalkFor && !bUpTop) AddMovementInput(bBank && KnightMove() && KnightMove()->OnWall() ? UKnightMovement::WallUp(KnightMove()->WallNormal) : Dir, Share);   // (on the bank: straight up it, as W does)
		return;
	}
	if (!Controller || !Controller->IsA<APlayerController>()) return;
	NetMantleT += Dt;
	const UKnightMovement* KM = KnightMove();
	if (HasAuthority() && !IsLocallyControlled())
	{
		if (NetMantleT >= 4.f && NetMantleT - Dt < 4.f) Place();
		{
			// (the server: once he is back on the ground after a grab, 1 s settled, or at 60 s; its clock starts well before the client walks)
			static bool bSaid = false; static float OnGround = 0.f;
			OnGround = (KM && KM->Grabs > 0 && KM->IsMovingOnGround()) ? OnGround + Dt : 0.f;
			if (bBank && !bSaid && (OnGround > 1.f || NetMantleT > 60.f)) { bSaid = true; BankVerdict(TEXT("server")); }
		}
	}
	else if (IsLocallyControlled() && GetNetMode() == NM_Client && KM)
	{
		static int32 FixesAtStart = 0;
		if (NetMantleT >= 8.f && NetMantleT - Dt < 8.f)
		{
			FixesAtStart = KM->NetFixes;
			UE_LOG(LogTemp, Log, TEXT("JEEBNETFIX client walks from (%.0f,%.0f,%.0f), %d corrections so far"), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, KM->NetFixes);
		}
		if (NetMantleT >= 8.f && NetMantleT < 8.f + WalkFor && !bUpTop) AddMovementInput(bBank && KM->OnWall() ? UKnightMovement::WallUp(KM->WallNormal) : Dir, Share);
		if (NetMantleT >= 9.f + WalkFor && NetMantleT - Dt < 9.f + WalkFor)
		{
			if (bBank) BankVerdict(TEXT("client"));
			UE_LOG(LogTemp, Log, TEXT("JEEBNETFIX client done at (%.0f,%.0f,%.0f): %d corrections while walking and mantling"), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, KM->NetFixes - FixesAtStart);
		}
	}
#endif
}

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
	TickWaterWalk(Dt);
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
	TickMantleTests(Dt);
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
		if (GetLocalRole() != ROLE_SimulatedProxy)
		{
			MantleEdge = KM->ActEdge;
			MantleAlong = FVector(KM->ActDir.X, KM->ActDir.Y, 0.f).GetSafeNormal();
			MantleFar = UKnightMovement::MantleFarPast(KM->ActFrom, KM->ActEdge, MantleAlong, In.WallMode, KM->ActFar);
		}
		else if (MantleEdgeMode != In.WallMode)
		{
			MantleAlong = GetActorForwardVector().GetSafeNormal2D();
			if (!KM->FindMantleEdge(C, GetActorForwardVector(), MantleEdge, &MantleFar, &MantleAlong)) MantleEdge = FVector::ZeroVector;
		}
		In.WallPoint = MantleEdge;
		In.bMantleEdge = !MantleEdge.IsZero();
		In.MantleAlong = MantleAlong;
		In.MantleFar = MantleFar;
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
