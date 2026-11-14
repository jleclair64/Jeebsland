#include "KnightMovement.h"
#include "KnightCharacter.h"
#include "KnightAnimator.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "JeebTuning.h"
#include "JeebWater.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Camera/CameraTypes.h"

// Swimming (2026-11-13, owner: "a swimming animation, a swimming skill ... with swimming stamina, and make the user drown and respawn if they go
// underwater for too long or they run out of swimming stamina"). Custom movement mode Swim (6) of UKnightMovement, inside Unreal's movement
// prediction like the climb: the player's machine predicts, the server runs the same moves from the same keys (direction, Shift, Space, Ctrl)
// and corrects position, mode, the bar and the breath (FKnightMoveResponse). Where the water is comes only from JeebWater (shared constants),
// never from the drawn water, which has no collision and exists only on players' machines.
//   - Deep water (SwimEnterDepth over his feet) starts it; he floats with his shoulders at the surface (SwimFloat), WASD swims, Shift swims
//     fast for more of the bar, Ctrl dives, Space swims up. Standing where it is shallow (SwimExitDepth) ends it.
//   - The swimming bar lasts AKnightCharacter::SwimSeconds(Swimming level) of plain swimming; treading water drains SwimIdleShare as fast,
//     the fast swim SwimFastShare. It refills only on dry ground. The breath lasts BreathSeconds(level) with his head under.
//   - Either running out is drowning (mode Drown): he sinks, and the SERVER alone ends it with Die() after DrownSinkTime (KnightCharacterClimb.cpp).
//   - Swimming XP: SwimXPPerMetre for each metre swum (server, from its own positions), given in lumps of SwimXPLump and on leaving the water.
// Every number is Tuning.ini [Swim].

namespace
{
	FVector FlatV(const FVector& V) { return FVector(V.X, V.Y, 0.f); }
	float Lines(int32 L, float A1, float A10, float A30, float A99)
	{
		const float X = float(FMath::Clamp(L, 1, 99));
		return X <= 10.f ? FMath::GetMappedRangeValueClamped(FVector2f(1.f, 10.f), FVector2f(A1, A10), X)
			: X <= 30.f ? FMath::GetMappedRangeValueClamped(FVector2f(10.f, 30.f), FVector2f(A10, A30), X)
			: FMath::GetMappedRangeValueClamped(FVector2f(30.f, 99.f), FVector2f(A30, A99), X);
	}
	constexpr float HeadUp = 72.f;   // his eyes over the capsule's middle
	FCollisionObjectQueryParams Ground() { FCollisionObjectQueryParams P; P.AddObjectTypesToQuery(ECC_WorldStatic); P.AddObjectTypesToQuery(ECC_WorldDynamic); return P; }
}

float AKnightCharacter::SwimSeconds(int32 L)
{
	const JeebTune::FValues& T = JeebTune::V();
	return Lines(L, T.SwimAt1, T.SwimAt10, T.SwimAt30, T.SwimAt99);
}

float AKnightCharacter::BreathSeconds(int32 L)
{
	const JeebTune::FValues& T = JeebTune::V();
	return FMath::GetMappedRangeValueClamped(FVector2f(1.f, 99.f), FVector2f(T.BreathAt1, T.BreathAt99), float(FMath::Clamp(L, 1, 99)));
}

float UKnightMovement::WadeMul(float Depth)
{
	const JeebTune::FValues& T = JeebTune::V();
	return FMath::Lerp(1.f, 1.f - T.WadeSlow, FMath::Clamp(Depth / FMath::Max(T.SwimEnterDepth, 1.f), 0.f, 1.f));
}

void UKnightMovement::SenseWater()
{
	WaterDepth = 0.f; WaterSurface = -1.0e9f;
	if (!UpdatedComponent || !CharacterOwner) return;
	const FVector Feet = UpdatedComponent->GetComponentLocation() - FVector(0.f, 0.f, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	WaterDepth = JeebWater::DepthAt(Feet, WaterSurface);
}

void UKnightMovement::ResetWater()
{
	SwimStamina = 1.f; Breath = 1.f; SwimRefillWait = 0.f; DrownT = 0.f; bHeadUnder = false; SwimMetres = 0.f;
}

void UKnightMovement::StartSwim()
{
	Velocity.Z = FMath::Max(float(Velocity.Z), -900.f);   // (a dive from a height: braked in the water, PhysSwim)
	SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
	bNoRegrab = false;   // (fell off a rock into the water: he may grab it again from here)
	SetMovementMode(MOVE_Custom, JeebClimb::Swim);
	LogSwim(TEXT("in"));
}

void UKnightMovement::StartDrown(const TCHAR* Why)
{
	DrownT = 0.f;
	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Custom, JeebClimb::Drown);
	LogSwim(Why);
}

void UKnightMovement::SwimDrain(float Dt, float Share)
{
	const AKnightCharacter* K = Knight();
	const int32 L = (K && K->GetSkills()) ? K->GetSkills()->GetLevel(ESkillType::Swimming) : 1;
	SwimStamina = FMath::Max(0.f, SwimStamina - Share * Dt / FMath::Max(AKnightCharacter::SwimSeconds(L), 1.f));
	if (bHeadUnder) Breath = FMath::Max(0.f, Breath - Dt / FMath::Max(AKnightCharacter::BreathSeconds(L), 1.f));
	else Breath = FMath::Min(1.f, Breath + JeebTune::V().BreathRefillPerSec * Dt);
}

bool UKnightMovement::LeaveWaterIfStanding(float FloorZ, bool bFloorOk)
{
	if (!bFloorOk || !CharacterOwner) return false;
	const float HH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector C = UpdatedComponent->GetComponentLocation();
	float S;
	if (JeebWater::SurfaceAt(C.X, C.Y, S) && S - FloorZ >= JeebTune::V().SwimExitDepth) return false;   // still too deep to stand
	if (C.Z - HH - FloorZ > 10.f) return false;   // not down on it yet
	if (bWantsLetGo) return false;   // (diving)
	SetMovementMode(MOVE_Walking);
	LogSwim(TEXT("out"));
	return true;
}

void UKnightMovement::PhysSwim(float Dt)
{
	const JeebTune::FValues& T = JeebTune::V();
	AKnightCharacter* K = Knight();
	const float HH = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector C = UpdatedComponent->GetComponentLocation();
	float S;
	if (!JeebWater::SurfaceAt(C.X, C.Y, S) || S < C.Z - HH) { SetMovementMode(MOVE_Falling); LogSwim(TEXT("no water")); return; }

	// the bottom under him
	FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebSwimFloor), false, CharacterOwner);
	FHitResult F;
	const bool bFloor = GetWorld()->LineTraceSingleByObjectType(F, C, C - FVector(0.f, 0.f, HH + 250.f), Ground(), Q) && F.ImpactNormal.Z > 0.3f;
	const float FloorZ = bFloor ? float(F.ImpactPoint.Z) : -1.0e9f;

	// The keys: a share of full, across the water
	const FVector A = FlatV(Acceleration) / FMath::Max(GetMaxAcceleration(), 1.f);
	const float M = FMath::Min(float(A.Size()), 1.f);
	const bool bFast = bWantsFast && SwimStamina > 0.05f && M > 0.2f;
	const bool bDive = bWantsLetGo;
	const float Tired = SwimStamina < 0.2f ? FMath::Lerp(0.7f, 1.f, SwimStamina / 0.2f) : 1.f;
	const float Burden = (K && K->GetSkills()) ? JeebItems::WeightSpeedMul(K->GetSkills()) : 1.f;
	const float Top = (bFast ? T.SwimFastSpeed : T.SwimSpeed) * (bHeadUnder ? 0.8f : 1.f) * Tired * Burden;
	const FVector WantH = (M > KINDA_SMALL_NUMBER ? A.GetSafeNormal() * M : FVector::ZeroVector) * Top;
	FVector VH = FlatV(Velocity);
	VH = FMath::VInterpConstantTo(VH, WantH, Dt, T.SwimAccel);

	// Up and down: float with the shoulders at the surface (never below the bottom), Ctrl dives, Space swims up; a plunge is braked hard
	const float Target = FMath::Max(S - T.SwimFloat, FloorZ + HH + 1.f);
	float VzWant;
	if (bDive) VzWant = -T.DiveSpeed;
	else if (bWantsRise && C.Z < Target - 5.f) VzWant = T.SwimRiseSpeed;
	else VzWant = FMath::Clamp((Target - float(C.Z)) * 3.f, -T.SwimFloatSpeed, T.SwimFloatSpeed);
	float Vz = Velocity.Z;
	Vz = FMath::FInterpConstantTo(Vz, VzWant, Dt, Vz < VzWant - 200.f ? 2500.f : 900.f);
	if (bDive && C.Z + HeadUp > S + 20.f) Vz = FMath::Min(Vz, -T.DiveSpeed);   // (going under from the surface: no slow start with his head still out)
	Velocity = FVector(VH.X, VH.Y, Vz);

	// Facing: the way he swims, turned smoothly (a slow drift doesn't turn him)
	FRotator Rot(0.f, UpdatedComponent->GetComponentRotation().Yaw, 0.f);
	if (VH.SizeSquared() > 40.f * 40.f) Rot = FMath::RInterpTo(Rot, FRotator(0.f, VH.Rotation().Yaw, 0.f), Dt, 5.f);

	const FVector Delta = Velocity * Dt;
	FHitResult Hit;
	SafeMoveUpdatedComponent(Delta, Rot.Quaternion(), true, Hit);
	if (Hit.IsValidBlockingHit())
	{
		// swimming into a rock face he can climb: onto it (as on land: square on, pushing at it, with climbing stamina)
		const FVector NF = FlatV(Hit.ImpactNormal).GetSafeNormal();
		if (Climbable(Hit) && K && !bWantsLetGo && !bClimbExhausted && ClimbStamina > 0.02f && M > 0.2f && !NF.IsNearlyZero()
			&& FVector::DotProduct(-NF, A.GetSafeNormal()) > 0.643f)
		{
			StartClimb(Hit.ImpactNormal);
			return;
		}
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}
	C = UpdatedComponent->GetComponentLocation();

	// Head under or out (a little hysteresis so the surface's line doesn't flicker it), then the bar and the breath
	if (!bHeadUnder && C.Z + HeadUp < S - 5.f) bHeadUnder = true;
	else if (bHeadUnder && C.Z + HeadUp > S + 5.f) bHeadUnder = false;
	const float MoveShare = bFast ? T.SwimFastShare : 1.f;
	SwimDrain(Dt, FMath::Lerp(T.SwimIdleShare, MoveShare, M));
	if (CharacterOwner->HasAuthority()) ServerWaterSim += Dt;
	SwimXP(false);
	if (SwimStamina <= 0.f) { StartDrown(TEXT("drown: bar empty")); return; }
	if (Breath <= 0.f) { StartDrown(TEXT("drown: out of breath")); return; }

	// Shallow enough to stand: out of the water
	FHitResult F2;
	const bool bFloor2 = GetWorld()->LineTraceSingleByObjectType(F2, C, C - FVector(0.f, 0.f, HH + 30.f), Ground(), Q) && F2.ImpactNormal.Z >= GetWalkableFloorZ();
	if (LeaveWaterIfStanding(bFloor2 ? float(F2.ImpactPoint.Z) : -1.0e9f, bFloor2)) return;
	LogSwim(nullptr);
}

// Drowning: he sinks slowly; the server ends it with Die() (AKnightCharacter::TickWallClimb). Nothing the keys do changes it.
void UKnightMovement::PhysDrown(float Dt)
{
	const JeebTune::FValues& T = JeebTune::V();
	DrownT += Dt;
	bHeadUnder = true;
	Velocity = FVector(0.f, 0.f, -T.DrownSinkSpeed);
	FHitResult Hit;
	SafeMoveUpdatedComponent(Velocity * Dt, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (CharacterOwner->HasAuthority()) ServerWaterSim += Dt;
	LogSwim(nullptr);
}

bool UKnightMovement::DrownedOut() const
{
	return IsCustom(JeebClimb::Drown) && DrownT >= JeebTune::V().DrownSinkTime;
}

// (server, every frame) The server's own clock against the time his moves have covered in the water: whatever his moves fall behind by more than
// half a second (moves stopped, or sent sparsely with tiny steps) is drained as treading water (or sunk, drowning) and counted as covered, so
// cutting the connection or slowing his moves never saves him from drowning. Out of the water both counts start again.
void UKnightMovement::ServerWaterBackstop(float Dt)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority()) return;
	if (!InWater()) { ServerWaterWall = ServerWaterSim = 0.f; return; }
	ServerWaterWall += Dt;
	ServerWaterSim = FMath::Min(ServerWaterSim, ServerWaterWall + 1.f);   // (moves running ahead of the clock bank nothing)
	const float Lag = ServerWaterWall - ServerWaterSim;
	if (Lag <= 0.5f) return;
	const float Extra = Lag - 0.25f;
	ServerWaterSim += Extra;
	if (IsCustom(JeebClimb::Drown)) { DrownT += Extra; return; }
	SwimDrain(Extra, JeebTune::V().SwimIdleShare);
	if (SwimStamina <= 0.f || Breath <= 0.f) StartDrown(TEXT("drown: moves behind the clock"));
}

// Swimming XP (server): metres swum from its own positions (a step can't count for more than the fast swim could cover), in lumps
void UKnightMovement::SwimXP(bool bFlush)
{
	if (!CharacterOwner || !UpdatedComponent) return;
	const FVector2D Here(UpdatedComponent->GetComponentLocation());
	if (!CharacterOwner->HasAuthority()) { SwimLastXY = Here; return; }
	const JeebTune::FValues& T = JeebTune::V();
	if (!bFlush)
	{
		const float Step = FMath::Min(float(FVector2D::Distance(Here, SwimLastXY)), T.SwimFastSpeed * 0.25f);   // (a quarter second's worth at most per move)
		SwimMetres += Step / 100.f;
	}
	SwimLastXY = Here;
	const float PerMetre = FMath::Max(T.SwimXPPerMetre, 0.f);
	const float Lump = FMath::Max(T.SwimXPLump, 1.f);
	int32 Give = 0;
	if (bFlush) { Give = FMath::FloorToInt(SwimMetres * PerMetre); SwimMetres = 0.f; }
	else if (SwimMetres * PerMetre >= Lump) { Give = FMath::FloorToInt(Lump); SwimMetres -= Lump / FMath::Max(PerMetre, 0.001f); }
	if (Give > 0)
		if (AKnightCharacter* K = Knight()) if (USkillComponent* Sk = K->GetSkills()) Sk->AddXP(ESkillType::Swimming, Give);
}

void UKnightMovement::LogSwim(const TCHAR* What, float Extra) const
{
	if (!CharacterOwner || !UpdatedComponent || !GetWorld()) return;
	UKnightMovement* Self = const_cast<UKnightMovement*>(this);
	const float Now = float(GetWorld()->GetTimeSeconds());
	if (!What && Now - Self->SwimLogT < 0.5f) return;
	Self->SwimLogT = Now;
	const FVector L = UpdatedComponent->GetComponentLocation();
	float S = 0.f;
	const bool bWater = JeebWater::SurfaceAt(L.X, L.Y, S);
	UE_LOG(LogTemp, Log, TEXT("JEEBSWIM %s %s mode=%d stam=%.2f breath=%.2f%s under=%.0f v=%.0f vz=%.0f at=(%.0f,%.0f,%.0f) drown=%.2f %.1f"),
		CharacterOwner->HasAuthority() ? TEXT("server") : TEXT("client"), What ? What : TEXT("-"), int32(ClimbMode()), SwimStamina, Breath,
		bHeadUnder ? TEXT(" head-under") : TEXT(""), bWater ? S - float(L.Z) : 0.f, Velocity.Size2D(), Velocity.Z, L.X, L.Y, L.Z, DrownT, Extra);
}

// ---------------------------------------------------------------- the knight's side

// What the animator needs of the swim, from what every machine knows (the movement mode and velocity replicate; the water is JeebWater)
void AKnightCharacter::FillSwimInput(JeebAnim::FInput& In) const
{
	const UKnightMovement* KM = KnightMove();
	if (!KM || !KM->InWater()) return;
	In.SwimMode = KM->ClimbMode();
	const FVector C = GetActorLocation();
	float S;
	In.SwimDepth = JeebWater::SurfaceAt(C.X, C.Y, S) ? S - float(C.Z) : JeebTune::V().SwimFloat;
	In.SwimSpeed = GetVelocity().Size2D();
	In.SwimVz = GetVelocity().Z;
	In.bSwimUnder = GetLocalRole() == ROLE_SimulatedProxy ? In.SwimDepth > HeadUp + 5.f : KM->bHeadUnder;
}

// The camera never goes under the water (owner 2026-11-14): wherever water lies under the camera, its height is held CamOverWater over the
// surface. Done on the view itself, after the spring arm, so it never fights the boom; one lookup a frame (none above the highest water).
void AKnightCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	const FVector P = OutResult.Location;
	const float Over = JeebTune::V().CamOverWater;
	if (P.Z >= JeebWater::TopLevel() + Over) return;
	float S;
	if (!JeebWater::SurfaceAt(P.X, P.Y, S)) return;
	OutResult.Location.Z = FMath::Max(float(P.Z), S + Over);
}

// Test pictures (standalone only), times in hundredths of a second from the start:
//   -SwimKeys=<ahead>_<right>_<from>_<for>[x...]   keys in hundredths of full, along the way he faced when the test began
//   -SwimDo=fast<from>_<for>|dive<from>_<for>|rise<from>_<for>[x...]   Shift, Ctrl or Space held for that stretch
void AKnightCharacter::TickSwimTests(float Dt)
{
	if (!bShotFace || !IsLocallyControlled() || GetNetMode() != NM_Standalone) return;
	static FString Keys, Do;
	static bool bRead = false;
	static float T = 0.f, Yaw = 0.f;
	if (!bRead)
	{
		FParse::Value(FCommandLine::Get(), TEXT("SwimKeys="), Keys);
		FParse::Value(FCommandLine::Get(), TEXT("SwimDo="), Do);
		Yaw = GetActorRotation().Yaw;
		bRead = true;
	}
	// (-SwimProbe: once a second, everything solid straight under him, for "what is he standing on in the water")
	static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("SwimProbe"));
	static float ProbeT = 0.f;
	if (bProbe && (ProbeT += Dt) > 1.f)
	{
		ProbeT = 0.f;
		const FVector C = GetActorLocation();
		TArray<FHitResult> Hits;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(JeebSwimProbe), false, this);
		GetWorld()->LineTraceMultiByChannel(Hits, C + FVector(0.f, 0.f, 50.f), C - FVector(0.f, 0.f, 400.f), ECC_Visibility, Q);
		FCollisionObjectQueryParams O(FCollisionObjectQueryParams::AllObjects);
		TArray<FOverlapResult> Over;
		GetWorld()->OverlapMultiByObjectType(Over, C - FVector(0.f, 0.f, 100.f), FQuat::Identity, O, FCollisionShape::MakeSphere(30.f), Q);
		FString S;
		for (const FHitResult& H : Hits) S += FString::Printf(TEXT(" [%s.%s z %.0f]"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"), H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("-"), H.ImpactPoint.Z);
		for (const FOverlapResult& Ov : Over) S += FString::Printf(TEXT(" {%s.%s}"), Ov.GetActor() ? *Ov.GetActor()->GetName() : TEXT("-"), Ov.GetComponent() ? *Ov.GetComponent()->GetName() : TEXT("-"));
		const UKnightMovement* KM0 = KnightMove();
		UE_LOG(LogTemp, Log, TEXT("JEEBSWIMPROBE at %s mode %d floor %s:%s"), *C.ToCompactString(), KM0 ? int32(KM0->MovementMode) : -1,
			KM0 && KM0->CurrentFloor.HitResult.GetComponent() ? *KM0->CurrentFloor.HitResult.GetComponent()->GetName() : TEXT("-"), *S);
	}
	// JEEBSWIMHAND: each second's highest wrist against the water (owner 2026-11-14: hands flush at the top of the stroke), and once, standing,
	// how high his hips (waist) are over his feet
	if (Anim.IsValid())
	{
		static float Top = -1.0e9f, TopT = 0.f;
		static bool bWaist = false;
		const UKnightMovement* KM0 = KnightMove();
		const FVector C = GetActorLocation();
		const float Feet = float(C.Z) - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (!bWaist && KM0 && KM0->IsMovingOnGround())
		{
			bWaist = true;
			UE_LOG(LogTemp, Log, TEXT("JEEBSWIMHAND waist (hips over feet) %.0f cm"), float(Anim->BoneWorld(JeebAnim::Hips).GetLocation().Z) - Feet);
		}
		float S;
		if (KM0 && KM0->IsCustom(JeebClimb::Swim) && JeebWater::SurfaceAt(C.X, C.Y, S))
		{
			const float H = float(FMath::Max(Anim->BoneWorld(JeebAnim::LHand).GetLocation().Z, Anim->BoneWorld(JeebAnim::RHand).GetLocation().Z)) - S;
			Top = FMath::Max(Top, H);
			if ((TopT += Dt) > 1.0f)
			{
				UE_LOG(LogTemp, Log, TEXT("JEEBSWIMHAND top wrist over water %.1f cm (speed %.0f)"), Top, GetVelocity().Size2D());
				Top = -1.0e9f; TopT = 0.f;
			}
		}
	}
	if (Keys.IsEmpty() && Do.IsEmpty()) return;
	T += Dt;
	UKnightMovement* KM = KnightMove();
	if (!KM) return;
	const FRotator R(0.f, Yaw, 0.f);
	TArray<FString> Segs;
	Keys.ParseIntoArray(Segs, TEXT("x"));
	for (const FString& S : Segs)
	{
		TArray<FString> P;
		if (S.ParseIntoArray(P, TEXT("_")) != 4) continue;
		const float From = FCString::Atoi(*P[2]) / 100.f, For = FCString::Atoi(*P[3]) / 100.f;
		if (T < From || T > From + For) continue;
		AddMovementInput(R.Vector(), FCString::Atoi(*P[0]) / 100.f);
		AddMovementInput(FRotationMatrix(R).GetUnitAxis(EAxis::Y), FCString::Atoi(*P[1]) / 100.f);
	}
	bool bFast = false, bDive = false, bRise = false;
	Do.ParseIntoArray(Segs, TEXT("x"));
	for (const FString& S : Segs)
	{
		const int32 N = S.StartsWith(TEXT("fast")) || S.StartsWith(TEXT("dive")) || S.StartsWith(TEXT("rise")) ? 4 : -1;
		if (N < 0) continue;
		TArray<FString> P;
		if (S.Mid(N).ParseIntoArray(P, TEXT("_")) != 2) continue;
		const float From = FCString::Atoi(*P[0]) / 100.f, For = FCString::Atoi(*P[1]) / 100.f;
		if (T < From || T > From + For) continue;
		if (S.StartsWith(TEXT("fast"))) bFast = true;
		else if (S.StartsWith(TEXT("dive"))) bDive = true;
		else bRise = true;
	}
	if (Do.IsEmpty()) return;
	bSprinting = bFast;
	KM->bWantsLetGo = bDive;
	KM->bWantsRise = bRise;
}
