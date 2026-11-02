#include "KnightCharacter.h"
#include "KnightDesign.h"
#include "WeaponDesign.h"
#include "Messages.h"
#include "SkillComponent.h"
#include "Inventory.h"
#include "Projectile.h"
#include "HitBoxes.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"

// ---------------- The bow (2026-10-31, Breath of the Wild style) ----------------
// Press: the string is drawn (StartDraw); the draw fills over BowDrawTime and holds full for BowHoldMax, then the arrow goes by
// itself. Let go: the arrow flies (ReleaseDraw) toward what is under the reticle, faster and harder the fuller the draw, and
// the bow comes down after BowRecover. The player's machine decides; the server checks against its own timer and fires.

int32 AKnightCharacter::ArrowSlotId() const
{
	if (!Skills) return 0;
	if (JeebItems::IsArrow(ArrowPick) && Skills->CountItem(ArrowPick) > 0) return ArrowPick;
	const int32 T = Skills->BestArrowTier();   // (nothing picked, or the picked ones ran out: the best you have, then fire arrows)
	if (T >= 0) return JeebItems::ArrowId(T);
	return Skills->CountItem(JeebItems::IdFireArrow) > 0 ? JeebItems::IdFireArrow : 0;
}

void AKnightCharacter::CycleArrows()
{
	if (!Skills || IsUIOpen()) return;
	const int32 N = JeebItems::NumArrowKinds();
	const int32 Now = ArrowSlotId();
	int32 At = 0;
	for (int32 i = 0; i < N; ++i) if (JeebItems::ArrowKindId(i) == Now) At = i;
	for (int32 s = 1; s <= N; ++s)
	{
		const int32 Id = JeebItems::ArrowKindId((At + s) % N);
		if (Skills->CountItem(Id) > 0) { ArrowPick = Id; return; }
	}
}

void AKnightCharacter::GetShieldParts(TArray<UPrimitiveComponent*>& Out) const
{
	for (const FArmorPart& P : ArmorParts)
		if (P.Slot == KnightDesign::SlotShield && P.Comp && P.Comp->IsVisible()) Out.Add(P.Comp);
}

FVector AKnightCharacter::ArrowStart() const
{
	// in front of his chest by the bow, at shoulder height (worked out from the knight himself, so every machine agrees)
	return GetActorTransform().TransformPosition(FVector(55.f, -8.f, 32.f));
}

// The camera sits over his right shoulder, so an arrow from the bow toward a far point crosses the reticle's line only out there:
// anything just left of the reticle on the way got hit (owner, 2026-11-01). The arrow leaves from just left of and below the
// middle of the screen, level with the bow, and flies in one straight line toward the reticle.
FVector AKnightCharacter::ShotStart() const
{
	const FVector BowAt = ArrowStart();
	if (ShotSideOn() || !IsLocallyControlled()) return BowAt;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->PlayerCameraManager) return BowAt;
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector CamDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
	const FRotationMatrix CamM(PC->PlayerCameraManager->GetCameraRotation());
	return CheckShotStart(CamLoc + CamDir * FMath::Max(0.f, FVector::DotProduct(BowAt - CamLoc, CamDir))
		- CamM.GetUnitAxis(EAxis::Y) * ShotLeft - CamM.GetUnitAxis(EAxis::Z) * ShotDown);
}

FVector AKnightCharacter::CheckShotStart(const FVector& Asked) const
{
	const FVector BowAt = ArrowStart();
	if (FVector::Dist(Asked, BowAt) > 200.f) return BowAt;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(BowStart), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, BowAt, Asked, ECC_Visibility, Q)) return BowAt;   // (a wall at his right shoulder)
	return Asked;
}

bool AKnightCharacter::ShotSideOn() const
{
	return bShotFace && GetController() && FMath::Abs(FMath::FindDeltaAngleDegrees(ShotFaceYaw, GetController()->GetControlRotation().Yaw)) > 20.f;
}

FVector AKnightCharacter::BowAimDir(FVector* OutTarget) const
{
	const FVector Start = ShotStart();
	if (ShotSideOn()) return GetActorForwardVector();
	if (IsLocallyControlled())
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
			if (PC->PlayerCameraManager)
			{
				// what is under the reticle (the middle of the screen): the arrow flies from the bow toward it
				const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
				const FVector CamDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
				FHitResult Hit;
				FCollisionQueryParams Q(SCENE_QUERY_STAT(BowAim), false, this);
				// (looking from abreast of the bow on: anything between the camera and the knight, such as a dummy he walks past, is
				// behind the arrow and must not catch the aim)
				const FVector From = CamLoc + CamDir * FMath::Max(150.f, FVector::DotProduct(Start - CamLoc, CamDir) + 20.f);
				FVector Target = CamLoc + CamDir * 9000.f;
				if (GetWorld()->LineTraceSingleByChannel(Hit, From, Target, ECC_Visibility, Q)) Target = Hit.ImpactPoint;
				const float Body = AProjectile::FirstBodyAlong(GetWorld(), From, Target, this);   // (a knight or foe under the reticle: the trace sees through them)
				if (Body <= 1.f) Target = From + (Target - From) * Body;
				if (OutTarget) *OutTarget = Target;
				const FVector D = Target - Start;
				if (D.Size() > 200.f && FVector::DotProduct(D.GetSafeNormal(), CamDir) > 0.3f) return D.GetSafeNormal();
				return (CamLoc + CamDir * 3000.f - Start).GetSafeNormal();   // (something right in front of the bow: still cross the reticle's line, 30 m out)
			}
	return GetBaseAimRotation().Vector();   // (others: where he looks)
}

void AKnightCharacter::SetBow(EBow P)
{
	Bow = P;
	BowT = 0.f;
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBBOW phase %d at %.2f"), int32(P), GetWorld()->GetTimeSeconds());
	if (P == EBow::Drawing) BowCharge = 0.f;
}

void AKnightCharacter::StartDraw()
{
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBBOW start: steel %d fire %d kind %d bow %d arrows %d atk %d bar %d %d %d %d %d sel %d"), Skills ? Skills->CountItem(JeebItems::ArrowId(JeebItems::T_Steel)) : -1, Skills ? Skills->CountItem(JeebItems::IdFireArrow) : -1, SelectedKind(), int32(Bow), ArrowSlotId(), bAttacking ? 1 : 0, Skills ? Skills->Hotbar[0] : -1, Skills ? Skills->Hotbar[1] : -1, Skills ? Skills->Hotbar[2] : -1, Skills ? Skills->Hotbar[3] : -1, Skills ? Skills->Hotbar[4] : -1, Skills ? Skills->Selected : -1);
	if (!Skills || SelectedKind() != JeebItems::K_Bow || Bow != EBow::None) return;
	if (bAttacking || bKnockedDown || StunLeft > 0.f || bRolling || bSliding || bClimbing || bSitting || bMounted) return;
	if (ArrowSlotId() == 0) { if (IsLocallyControlled()) JeebMsg::Post(2.f, FColor::Orange, TEXT("You have no arrows.")); return; }
	SetBow(EBow::Drawing);
	if (GetNetMode() == NM_Standalone) return;
	if (!HasAuthority()) ServerBowDraw();
	else MulticastBow(1);
}

void AKnightCharacter::ReleaseDraw()
{
	if (Bow != EBow::Drawing) return;
	FVector Target = FVector::ZeroVector;
	const FVector Dir = BowAimDir(&Target);
	const FVector From = ShotStart();
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBAIM start %s dir %s target %s speed %s"), *From.ToCompactString(), *Dir.ToCompactString(), *Target.ToCompactString(), *GetVelocity().ToCompactString());
	const float C = BowCharge;
	const int32 Id = ArrowSlotId();
	SetBow(EBow::Recover);
	if (HasAuthority()) { FireArrow(Dir, C, Id, From); if (GetNetMode() != NM_Standalone) MulticastBow(2); }
	else ServerBowShoot(Dir, C, Id, From);
}

void AKnightCharacter::CancelDraw()
{
	if (Bow == EBow::None) return;
	const bool bWasDrawing = Bow == EBow::Drawing;
	SetBow(EBow::None);
	if (!bWasDrawing || GetNetMode() == NM_Standalone) return;
	if (IsLocallyControlled() && !HasAuthority()) ServerBowCancel();
	else if (HasAuthority()) MulticastBow(0);
}

void AKnightCharacter::ServerBowDraw_Implementation()
{
	if (Bow == EBow::Recover) SetBow(EBow::None);   // (the player's machine may be a moment ahead of us)
	StartDraw();
}

void AKnightCharacter::ServerBowShoot_Implementation(FVector_NetQuantizeNormal Dir, float Charge, int32 ArrowId, FVector_NetQuantize10 From)
{
	if (Bow != EBow::Drawing || SelectedKind() != JeebItems::K_Bow) return;
	const float C = FMath::Clamp(FMath::Min(Charge, BowT / BowDrawTime + 0.15f), 0.f, 1.f);   // (never more draw than we have seen)
	FVector D = FVector(Dir).GetSafeNormal();
	if (FVector::DotProduct(D, GetActorForwardVector()) < 0.2f) D = GetBaseAimRotation().Vector();   // (only ahead of him)
	SetBow(EBow::Recover);
	FireArrow(D, C, ArrowId, CheckShotStart(From));
	MulticastBow(2);
}

void AKnightCharacter::ServerBowCancel_Implementation()
{
	CancelDraw();
}

void AKnightCharacter::MulticastBow_Implementation(uint8 Phase)
{
	if (HasAuthority() || IsLocallyControlled() || Phase > 2) return;   // (they already know)
	SetBow(static_cast<EBow>(Phase));
}

// TESTING: [Jeebsland.Testing] UnlimitedArrows=True in DefaultGame.ini: arrows are never used up, and everyone is kept at 99
// steel and 99 fire arrows (owner, 2026-11-01)
static bool UnlimitedArrows()
{
	static const bool b = []() { bool v = false; if (GConfig) GConfig->GetBool(TEXT("Jeebsland.Testing"), TEXT("UnlimitedArrows"), v, GGameIni); return v; }();
	return b;
}

// (server) One arrow from the pack, out of the bow
void AKnightCharacter::FireArrow(const FVector& Dir, float Charge, int32 ArrowId, const FVector& From)
{
	if (!Skills || !HasAuthority()) return;
	int32 Id = (JeebItems::IsArrow(ArrowId) && Skills->CountItem(ArrowId) > 0) ? ArrowId : 0;
	if (Id == 0) { const int32 Saved = ArrowPick; ArrowPick = 0; Id = ArrowSlotId(); ArrowPick = Saved; }
	if (Id == 0) { Skills->Say(2.f, FColor::Orange, TEXT("You have no arrows.")); return; }
	if (!UnlimitedArrows()) Skills->RemoveItems(Id, 1);
	const int32 BowId = Skills->SelectedItem();
	const int32 BowTier = JeebItems::IsGear(BowId) ? JeebItems::VariantOf(BowId) : 0;
	const float C = FMath::Clamp(Charge, 0.f, 1.f);
	bool bCrit = false;
	const int32 Dmg = JeebItems::RollDamage(Skills, JeebItems::K_Bow, BowTier, JeebItems::ArrowTier(Id), bCrit, FMath::Lerp(BowDamageMin, BowDamageMax, FMath::Pow(C, 1.5f)));
	auto ByDraw = [C](float Min, float Mid, float Max) { return C < 0.5f ? FMath::Lerp(Min, Mid, C * 2.f) : FMath::Lerp(Mid, Max, C * 2.f - 1.f); };
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AProjectile* P = GetWorld()->SpawnActor<AProjectile>(AProjectile::StaticClass(), From, Dir.Rotation(), SP))
	{
		const bool bFull = C >= 1.f;
		P->Launch(this, Dir, ByDraw(BowSpeedMin, BowSpeedMid, BowSpeedMax), 0.f, Dmg, bCrit, ESkillType::Archery, false, JeebItems::ArrowTier(Id), Id,
			bFull ? BowSagFull : ByDraw(BowSagMin, BowSagMid, BowSagMax), bFull ? BowStraight : 0.f);
	}
}

void AKnightCharacter::TickBow(float Dt)
{
	const bool bBowIn = SelectedKind() == JeebItems::K_Bow;
	// let down without shooting: another weapon, a roll, a kick or a swing, hitstun, knocked down, climbing, sitting ...
	if (Bow != EBow::None && (!bBowIn || bAttacking || bKnockedDown || StunLeft > 0.f || bRolling || bSliding || bClimbing || bSitting || bMounted || Health <= 0.f
		|| (IsLocallyControlled() && IsUIOpen())))
		CancelDraw();
	BowT += Dt;
	if (HasAuthority() && Skills && UnlimitedArrows())   // (testing: topped up to a full stack of each)
		for (const int32 Id : { JeebItems::ArrowId(JeebItems::T_Steel), JeebItems::IdFireArrow })
			if (const int32 Have = Skills->CountItem(Id); Have < USkillComponent::StartSteelArrows) Skills->AddInvItem(Id, USkillComponent::StartSteelArrows - Have);
	if (bShotFace && ShotStrafe != 0.f) AddMovementInput(GetActorRightVector(), ShotStrafe);   // (test pictures)
	if (bShotFace && bShotWalk) AddMovementInput(FRotator(0.f, ShotFaceYaw, 0.f).Vector(), 1.f);
	if (ShotBowAt > 0.f && GetWorld()->GetTimeSeconds() >= ShotBowAt) { ShotBowAt = -1.f; ReleaseDraw(); }   // (test pictures)
	if (Bow == EBow::Drawing)
	{
		BowCharge = FMath::Min(1.f, BowT / BowDrawTime);
		if (IsLocallyControlled() && BowT >= BowDrawTime + BowHoldMax) ReleaseDraw();   // held too long: it goes by itself
		else if (HasAuthority() && !IsLocallyControlled() && BowT >= BowDrawTime + BowHoldMax + 1.5f) CancelDraw();   // (the player's machine never said)
	}
	else if (Bow == EBow::Recover && BowT >= BowRecover) SetBow(EBow::None);

	// The camera closes in over his right shoulder while aiming (the player's own machine)
	if (IsLocallyControlled() && !ShotSideOn())
	{
		const bool bAimCam = Bow == EBow::Drawing;   // (it pulls back out as soon as the arrow is away: owner, 2026-11-01)
		BowCamAlpha = FMath::FInterpTo(BowCamAlpha, bAimCam ? 1.f : 0.f, Dt, bAimCam ? 7.f : 5.f);
		if (USpringArmComponent* Boom = GetCameraBoom())
		{
			const float A = BowCamAlpha * BowCamAlpha * (3.f - 2.f * BowCamAlpha);
			Boom->CameraLagSpeed = FMath::Lerp(8.f, 80.f, A);   // aiming: the camera keeps up with him (its lag made arrows seem to curve as he moved)
			Boom->TargetArmLength = FMath::Lerp(540.f, BowCamLength, A);
			Boom->SocketOffset = FMath::Lerp(FVector(0.f, 0.f, 60.f), FVector(0.f, 75.f, 42.f), A);
		}
	}
	if (!IsNetMode(NM_DedicatedServer)) { UpdateBowBits(); UpdateQuiver(); }
}

// The bow's pose numbers. Test pictures can try others without a rebuild:
// -BowTune=AnchorUp_AnchorSide_Chest_ArrowRestMm_RestTilt_RestTip_RestSplay (cm, cm, degrees, mm, degrees, degrees, degrees)
// Up, Side: the full draw's anchor, this far above his neck joint and out to his right (beside the helm's cheek); Chest: how far he
// turns side-on; Rest: the arrow rests this far above the middle of the fist (on top of it, not through it); Tilt, Tip: BowRestQuat.
struct FBowTune { float Up = 11.f, Side = 22.f, Chest = 70.f, Rest = 12.f, Tilt = 40.f, Tip = 10.f, Splay = 10.f; };   // (measured clear with LogClearance, 2026-11-01)
static const FBowTune& BowTune()
{
	static const FBowTune T = []
	{
		FBowTune R;
		FString S;
		if (FParse::Value(FCommandLine::Get(), TEXT("BowTune="), S))
		{
			TArray<FString> P;
			S.ParseIntoArray(P, TEXT("_"));
			float* F[7] = { &R.Up, &R.Side, &R.Chest, &R.Rest, &R.Tilt, &R.Tip, &R.Splay };
			for (int32 i = 0; i < FMath::Min(P.Num(), 7); ++i) *F[i] = FCString::Atof(*P[i]) * (i == 3 ? 0.1f : 1.f);
		}
		return R;
	}();
	return T;
}

// At ease (owner's drawing, 2026-11-01): the limbs run forward and back through the fist (the front one tipped up RestTip),
// the bow's curve below and the string above it, the string's side tipped RestTilt in from straight up so the string runs
// between the elbow and his body, not outside the arm.
FQuat AKnightCharacter::BowRestQuat() const
{
	using namespace KnightDesign;
	const FVector A0 = (GripPos(true) - PivotPos(P_ArmL)).GetSafeNormal();   // (the straight arm hanging, shoulder to fist)
	const FVector B0 = (FVector::ForwardVector - A0 * FVector::DotProduct(FVector::ForwardVector, A0)).GetSafeNormal();
	const float Tip = FMath::DegreesToRadians(BowTune().Tip), Tilt = FMath::DegreesToRadians(BowTune().Tilt);
	FVector In = FVector(0.f, 1.f, 0.f);                                                // (from the left arm in toward his body)
	In = (In - A0 * FVector::DotProduct(In, A0)); In = (In - B0 * FVector::DotProduct(In, B0)).GetSafeNormal();
	// the limbs: forward, the front one tipped up; splayed so the back one stays out clear of his hip and back as the arm swings
	const float Splay = FMath::DegreesToRadians(BowTune().Splay);
	const FVector Fw = B0 * FMath::Cos(Splay) + In * FMath::Sin(Splay);
	const FVector X = (Fw * FMath::Cos(Tip) - A0 * FMath::Sin(Tip)).GetSafeNormal();
	FVector Z = -A0 * FMath::Cos(Tilt) + In * FMath::Sin(Tilt);                         // the string's side: up the forearm, tipped in
	Z = (Z - X * FVector::DotProduct(Z, X)).GetSafeNormal();
	return FQuat(FMatrix(X, FVector::CrossProduct(Z, X), Z, FVector::ZeroVector));
}

// ---------------- The pose (called at the end of PoseArms) ----------------
// A straight left arm points the bow at the target, the fist turned so the bow stands up (rows of the arm's turn worked out
// in closed form from where it rests); the right hand draws the string back toward his face (a two-bone reach, the elbow out
// to the right and back); the chest turns side-on and the head looks back round at the target.
void AKnightCharacter::PoseBow(float Dt)
{
	const bool bBowIn = SelectedKind() == JeebItems::K_Bow && !bStowed && BowHold;
	BowAimAlpha = FMath::FInterpTo(BowAimAlpha, (bBowIn && Bow != EBow::None) ? 1.f : 0.f, Dt, Bow == EBow::Drawing ? 12.f : 8.f);
	BowEaseAlpha = FMath::FInterpTo(BowEaseAlpha, bBowIn ? 1.f : 0.f, Dt, 10.f);
	if (!ArmPivotL || !ElbowL || !ArmPivot || !ElbowR || !UpperPivot) return;
	using namespace KnightDesign;
	const float A = BowAimAlpha * BowAimAlpha * (3.f - 2.f * BowAimAlpha);
	auto Frame = [](const FVector& X, const FVector& Y) { return FQuat(FMatrix(X, Y, FVector::CrossProduct(X, Y), FVector::ZeroVector)); };
	auto Perp = [](const FVector& V, const FVector& To) { return (V - To * FVector::DotProduct(V, To)).GetSafeNormal(); };

	// at ease the bow keeps most of its line while the arm swings (as if the wrist eased it), so it never stands up on end or
	// swings across the arm as he walks (2026-11-01)
	FQuat RestRel = BowHoldRest;
	if (BodyRoot && BowHold)
	{
		const FQuat Fore = ElbowL->GetComponentQuat();
		RestRel = Fore.Inverse() * FQuat::Slerp(Fore * BowHoldRest, BodyRoot->GetComponentQuat() * BowHoldRest, 0.6f);
	}
	if (A <= 0.001f) { if (BowHold) BowHold->SetRelativeRotation(RestRel); return; }

	const FVector Aim = BowAimDir();
	const FVector Up = FVector::UpVector;
	const FQuat Chest = UpperPivot->GetComponentQuat();

	// The arrow runs from the bow back to the anchor, beside his right cheek, so the string hand stays clear of his body and helm
	// (an archer's draw: the bow arm reaches out across, the arrow passes beside the face)
	FVector Right = FVector::CrossProduct(Up, Aim).GetSafeNormal();
	if (Right.IsNearlyZero()) Right = GetActorRightVector();
	const FVector UpA = FVector::CrossProduct(Aim, Right);
	const FVector Anchor = HeadPivot ? HeadPivot->GetComponentLocation() + UpA * BowTune().Up + Right * BowTune().Side : ArrowStart();
	// Left arm: straight, the fist on the arrow's line where the arm just reaches it, the bow upright
	FVector GripWant;
	{
		static const FVector A0 = (GripPos(true) - PivotPos(P_ArmL)).GetSafeNormal();
		static const FVector B0 = (FVector::ForwardVector - A0 * FVector::DotProduct(FVector::ForwardVector, A0)).GetSafeNormal();
		static const float Reach = (GripPos(true) - PivotPos(P_ArmL)).Size();
		const FVector S = ArmPivotL->GetComponentLocation();
		const FVector D = Anchor - UpA * BowTune().Rest - S;   // (|D + Aim*L| = Reach, the far answer; the fist just under the arrow's line)
		const float B = FVector::DotProduct(D, Aim);
		const float L = -B + FMath::Sqrt(FMath::Max(B * B - D.SizeSquared() + Reach * Reach, 0.f));
		GripWant = Anchor - UpA * BowTune().Rest + Aim * FMath::Max(L, 30.f);
		const FVector ArmDir = (GripWant - S).GetSafeNormal();
		FVector Bw = Perp(Up, ArmDir);   // (the fist's grip, and the bow in it, then runs up and down, the thumb on top)
		if (Bw.IsNearlyZero()) Bw = GetActorRightVector();
		const FQuat Want = Chest.Inverse() * Frame(ArmDir, Bw) * Frame(A0, B0).Inverse();
		ArmPivotL->SetRelativeRotation(FQuat::Slerp(ArmPivotL->GetRelativeRotation().Quaternion(), Want, A));
		ElbowL->SetRelativeRotation(FQuat::Slerp(ElbowL->GetRelativeRotation().Quaternion(), FQuat(FRotator(3.f, 0.f, 0.f)), A));
		// the bow turns in the fist to stand upright, its string straight back toward him along the arrow (in front of his face,
		// not leaning across toward his helm: owner, 2026-11-01)
		const FVector BX = Perp(Up, Aim), BZ = -Aim;
		const FQuat BowWorld(FMatrix(BX, FVector::CrossProduct(BZ, BX), BZ, FVector::ZeroVector));
		BowHold->SetRelativeRotation(FQuat::Slerp(RestRel, ElbowL->GetComponentQuat().Inverse() * BowWorld, A));
	}
	// Right arm: the fist on the string, drawn back from where the arrow rests on the bow toward his face as the draw fills
	{
		const FVector Grip = BowHold->GetComponentTransform().TransformPosition(FVector(BowTune().Rest, 0.f, 0.f));
		const FVector StringRest = BowHold->GetComponentTransform().TransformPosition(FVector(BowTune().Rest, 0.f, WeaponDesign::BowBrace));
		const float Draw = (Bow == EBow::Drawing) ? BowCharge : (Bow == EBow::Recover ? 0.f : BowCharge);
		const FVector Back = Anchor - Grip;   // (from the bow to the anchor: full draw puts the fist there)
		const float Pull = FMath::Lerp(FMath::Min(32.f, Back.Size()), Back.Size(), FMath::Sqrt(FMath::Clamp(Draw, 0.f, 1.f)));
		FVector Hand = Grip + Back.GetSafeNormal() * Pull;
		if (Bow == EBow::Recover) Hand = FMath::Lerp(Anchor, StringRest, FMath::Clamp(BowT / BowRecover, 0.f, 1.f));   // (the string snaps forward; the hand follows)
		static const float Lu = (PivotPos(P_ElbowR) - PivotPos(P_ArmR)).Size();
		static const float Lf = (GripPos(false) - PivotPos(P_ElbowR)).Size();
		const FVector S = ArmPivot->GetComponentLocation();
		const FVector D = Hand - S;
		const float Dist = FMath::Clamp(D.Size(), 6.f, Lu + Lf - 0.5f);
		const FVector N = D.GetSafeNormal();
		const FVector Pole = Perp(-Aim + Up * 0.15f + Right * 0.2f, N);   // (the drawing elbow behind the hand, in line with the arrow, as an archer's: owner, 2026-11-01)
		const float CosA = FMath::Clamp((Lu * Lu + Dist * Dist - Lf * Lf) / (2.f * Lu * Dist), -1.f, 1.f);
		const FVector Au = (N * CosA + Pole * FMath::Sqrt(1.f - CosA * CosA)).GetSafeNormal();
		const FVector Elbow = S + Au * Lu;
		const FVector F = (S + N * Dist - Elbow).GetSafeNormal();
		FVector Bend = Perp(F, Au);
		if (Bend.IsNearlyZero()) Bend = Perp(Aim, Au);
		static const FVector A0 = (PivotPos(P_ElbowR) - PivotPos(P_ArmR)).GetSafeNormal();
		static const FVector B0 = (FVector::ForwardVector - A0 * FVector::DotProduct(FVector::ForwardVector, A0)).GetSafeNormal();
		const FQuat World = Frame(Au, Bend) * Frame(A0, B0).Inverse();
		ArmPivot->SetRelativeRotation(FQuat::Slerp(ArmPivot->GetRelativeRotation().Quaternion(), Chest.Inverse() * World, A));
		// the elbow: a bend about its own pitch axis that carries the resting forearm onto F
		static const FVector F0 = (GripPos(false) - PivotPos(P_ElbowR)).GetSafeNormal();
		const FVector Fl = World.Inverse().RotateVector(F);
		auto Phi = [](const FVector& V) { return FMath::RadiansToDegrees(FMath::Atan2(V.X, -V.Z)); };   // (0 hanging, + forward)
		const float Bendp = Phi(Fl) - Phi(F0);
		ElbowR->SetRelativeRotation(FQuat::Slerp(ElbowR->GetRelativeRotation().Quaternion(), FQuat(FRotator(Bendp, 0.f, 0.f)), A));
		if (WristR) WristR->SetRelativeRotation(FQuat::Slerp(WristR->GetRelativeRotation().Quaternion(), FQuat::Identity, A));
	}
	// side-on: the chest turned to his right (PoseBody turns it, next frame; the head already turns back against it to look
	// along the arrow)
	SwingBodyWant += BowTune().Chest * A;   // (more side-on: the bow arm out in front, the string before his face)
}

// ---------------- The drawn string, the nocked arrow and the quiver ----------------
static UStaticMeshComponent* MakeBit(AActor* Owner, USceneComponent* Attach, const TCHAR* Mesh, const FLinearColor& Col)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetupAttachment(Attach);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->RegisterComponent();
	if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/M_Color.M_Color"))))
		M->SetVectorParameterValue(TEXT("Color"), Col);
	return C;
}

// A thin rod from P to Q (the engine's cylinder: 100 cm tall, 50 cm round, standing along Z)
static void Rod(UStaticMeshComponent* C, const FVector& P, const FVector& Q, float Radius)
{
	const FVector D = Q - P;
	C->SetWorldLocationAndRotation((P + Q) * 0.5f, FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator());
	C->SetWorldScale3D(FVector(Radius / 50.f, Radius / 50.f, FMath::Max(D.Size(), 0.1f) / 100.f));
}

void AKnightCharacter::UpdateBowBits()
{
	const bool bShow = BowHold && BowAimAlpha > 0.02f && !bStowed;
	if (BowString) BowString->SetVisibility(!bShow);
	if (!bShow) { for (UStaticMeshComponent* C : BowBits) if (C) C->SetVisibility(false); return; }
	if (BowBits.Num() == 0)
	{
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.9f, 0.88f, 0.78f)));   // string, upper half
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.9f, 0.88f, 0.78f)));   // lower half
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.5f, 0.34f, 0.17f)));   // the nocked arrow
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cone.Cone"), FLinearColor(0.6f, 0.6f, 0.7f)));             // its head
		for (UStaticMeshComponent* C : BowBits) { C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true); }
	}
	const FTransform B = BowHold->GetComponentTransform();
	const FVector TipA = B.TransformPosition(FVector(-48.f, 0.f, WeaponDesign::BowBrace)), TipB = B.TransformPosition(FVector(48.f, 0.f, WeaponDesign::BowBrace));
	const FVector Rest = B.TransformPosition(FVector(BowTune().Rest, 0.f, WeaponDesign::BowBrace));   // (the nocking point: level with the arrow rest, just above the fist)
	// the nock: in the drawing fist while drawn, back on the bow as it is let go
	FVector Nock = Rest;
	if (WristR) Nock = FMath::Lerp(Rest, WristR->GetComponentTransform().TransformPosition(KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR)), FMath::Clamp(BowAimAlpha * 1.4f - 0.2f, 0.f, 1.f));
	if (Bow == EBow::Recover) Nock = FMath::Lerp(Nock, Rest, FMath::Clamp(BowT / 0.06f, 0.f, 1.f));
	for (int32 i = 0; i < 2; ++i) { BowBits[i]->SetVisibility(true); Rod(BowBits[i], i == 0 ? TipA : TipB, Nock, 0.3f); }
	const bool bArrow = Bow == EBow::Drawing && ArrowSlotId() != 0;
	BowBits[2]->SetVisibility(bArrow);
	BowBits[3]->SetVisibility(bArrow);
	if (bArrow)
	{
		const FVector Along = (B.TransformPosition(FVector(BowTune().Rest, 0.f, 0.f)) - Nock).GetSafeNormal();   // (over the top of the fist)
		const FVector Tip = Nock + Along * 74.f;
		Rod(BowBits[2], Nock, Tip, 0.6f);
		BowBits[3]->SetWorldLocationAndRotation(Tip + Along * 2.f, FRotationMatrix::MakeFromZ(Along).Rotator());
		BowBits[3]->SetWorldScale3D(FVector(0.028f, 0.028f, 0.065f));
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(BowBits[3]->GetMaterial(0)))
			M->SetVectorParameterValue(TEXT("Color"), JeebItems::IsFireArrow(ArrowSlotId()) ? FLinearColor(4.f, 1.4f, 0.25f) : FLinearColor(0.6f, 0.6f, 0.7f));
	}
}

// The quiver: a leather tube hung on his right hip like a sword's sheath (clear of the cape; owner, 2026-10-31), its mouth
// tipped forward with fletched arrows showing, while the bow is his weapon
void AKnightCharacter::UpdateQuiver()
{
	const bool bShow = SelectedKind() == JeebItems::K_Bow && BodyRoot;
	if (!bShow) { for (UStaticMeshComponent* C : QuiverParts) if (C) C->SetVisibility(false); return; }
	if (QuiverParts.Num() == 0)
	{
		// (in the hips' frame: at the back of the right hip, behind where the arm hangs and swings, as Kingdom Come: Deliverance's
		// archers wear it (2026-11-01: the arm used to pass through it at the side); pitch -: the mouth leans forward; roll -:
		// the bottom swings out from the leg)
		USceneComponent* Q = NewObject<USceneComponent>(this);
		Q->SetupAttachment(BodyRoot);
		FVector QAt(-26.f, 28.f, -29.f); FRotator QTurn(-5.f, 0.f, -6.f);   // (found by -QuiverSearch: clear of the armour, arms, legs and cape standing, walking and aiming; hangs nearly straight)
		{
			FString S; TArray<FString> P;   // (test pictures: -QuiverTune=X_Y_Z_Pitch_Roll tries another spot without a rebuild)
			if (FParse::Value(FCommandLine::Get(), TEXT("QuiverTune="), S) && S.ParseIntoArray(P, TEXT("_")) == 5)
			{ QAt = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])); QTurn = FRotator(FCString::Atof(*P[3]), 0.f, FCString::Atof(*P[4])); }
		}
		Q->SetRelativeLocationAndRotation(QAt, QTurn);
		Q->RegisterComponent();
		auto Bit = [&](const TCHAR* MeshPath, const FLinearColor& Col, const FVector& At, const FRotator& Rot, const FVector& Scale)
		{
			UStaticMeshComponent* C = MakeBit(this, Q, MeshPath, Col);
			C->SetRelativeLocationAndRotation(At, Rot);
			C->SetRelativeScale3D(Scale);
			QuiverParts.Add(C);
		};
		const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
		const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
		Bit(Cyl, FLinearColor(0.32f, 0.19f, 0.09f), FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.13f, 0.13f, 0.44f));               // the tube
		Bit(Cyl, FLinearColor(0.2f, 0.12f, 0.06f), FVector(0.f, 0.f, 21.f), FRotator::ZeroRotator, FVector(0.145f, 0.145f, 0.04f));          // the rim
		Bit(Cyl, FLinearColor(0.2f, 0.12f, 0.06f), FVector(0.f, 0.f, -20.f), FRotator::ZeroRotator, FVector(0.14f, 0.14f, 0.04f));           // the bottom
		static const FVector2D Spots[5] = { { 2.5f, 0.f }, { -2.5f, 2.f }, { -1.f, -3.f }, { 0.f, 3.5f }, { 3.f, -2.5f } };
		for (int32 i = 0; i < 5; ++i)
		{
			const FVector P(Spots[i].X, Spots[i].Y, 29.f + 2.f * (i % 3));
			Bit(Cyl, FLinearColor(0.5f, 0.34f, 0.17f), P - FVector(0.f, 0.f, 8.f), FRotator::ZeroRotator, FVector(0.012f, 0.012f, 0.3f));      // shaft
			Bit(Cube, FLinearColor(0.92f, 0.9f, 0.86f), P + FVector(0.f, 0.f, 3.f), FRotator(0.f, i * 37.f, 0.f), FVector(0.05f, 0.004f, 0.09f));   // fletching
		}
	}
	// the arrows in it only while he has some (the tube, rim and bottom are the first three parts)
	const bool bArrows = (IsLocallyControlled() || GetNetMode() == NM_Standalone || HasAuthority()) ? ArrowSlotId() != 0 : LookArrows != 0;
	for (int32 i = 0; i < QuiverParts.Num(); ++i) if (QuiverParts[i]) QuiverParts[i]->SetVisibility(i < 3 || bArrows);
}

// ---------------- Test pictures: measured clipping (log JEEBCLEAR) ----------------
// Every test picture also logs where the bow, its string, the nocked arrow and the quiver go INTO his own armour or cape
// (segment against the model's triangles), so clipping is measured instead of guessed from pictures. The bow's middle
// 6 cm either side of the grip is left out: it runs through the fist on purpose.
void AKnightCharacter::LogClearance(int32 Frame)
{
	if (!BowHold || SelectedKind() != JeebItems::K_Bow) return;
	struct FTri { FVector A, B, C; FName Part; };
	TArray<FTri> Tris;
	auto AddMesh = [&](UProceduralMeshComponent* M)
	{
		if (!M || !M->IsVisible()) return;
		const FTransform X = M->GetComponentTransform();
		const FName Part = M->GetAttachParent() ? M->GetAttachParent()->GetFName() : M->GetFName();
		for (int32 s = 0; s < M->GetNumSections(); ++s)
			if (const FProcMeshSection* S = M->GetProcMeshSection(s))
				for (int32 t = 0; t + 2 < S->ProcIndexBuffer.Num(); t += 6)   // (each triangle is stored twice, once per winding)
					Tris.Add({ X.TransformPosition(S->ProcVertexBuffer[S->ProcIndexBuffer[t]].Position), X.TransformPosition(S->ProcVertexBuffer[S->ProcIndexBuffer[t + 1]].Position),
						X.TransformPosition(S->ProcVertexBuffer[S->ProcIndexBuffer[t + 2]].Position), Part });
	};
	for (const FArmorPart& P : ArmorParts) AddMesh(Cast<UProceduralMeshComponent>(P.Comp));
	AddMesh(CapeCloth);
	// the left fist's top along the bow (where the arrow should rest): the glove's furthest point up the bow, close to its line
	float FistTop = -1000.f;
	{
		const FTransform Bi = BowHold->GetComponentTransform();
		for (const FTri& T : Tris)
			if (T.Part == ElbowL->GetFName())
				for (const FVector& W : { T.A, T.B, T.C })
				{
					const FVector L = Bi.InverseTransformPosition(W);
					if (FMath::Square(L.Y) + FMath::Square(L.Z) < 25.f && FMath::Abs(L.X) < 20.f) FistTop = FMath::Max(FistTop, L.X);
				}
	}

	// a segment against every triangle: each crossing as "item at <cm along it> into <part>"
	TMap<FString, FString> Hits;
	auto Test = [&](const FString& Item, const FVector& P, const FVector& Q, float From)
	{
		const FVector D = Q - P;
		for (const FTri& T : Tris)
		{
			const FVector E1 = T.B - T.A, E2 = T.C - T.A, H = FVector::CrossProduct(D, E2);
			const float Det = FVector::DotProduct(E1, H);
			if (FMath::Abs(Det) < 1e-6f) continue;
			const FVector S = P - T.A;
			const float U = FVector::DotProduct(S, H) / Det;
			if (U < 0.f || U > 1.f) continue;
			const FVector Qv = FVector::CrossProduct(S, E1);
			const float V = FVector::DotProduct(D, Qv) / Det;
			if (V < 0.f || U + V > 1.f) continue;
			const float W = FVector::DotProduct(E2, Qv) / Det;
			if (W < 0.f || W > 1.f) continue;
			FString& L = Hits.FindOrAdd(Item + TEXT(" -> ") + T.Part.ToString());
			L += FString::Printf(TEXT(" %.0f"), From + W * D.Size());
		}
	};
	const FTransform B = BowHold->GetComponentTransform();
	// the bow's limbs (as WeaponDesign builds them: the belly curves toward the string side), outside the fist
	for (float X = -48.f; X < 48.f; X += 4.f)
	{
		if (X + 4.f > -6.f && X < 6.f) continue;
		auto Pt = [&](float x) { return B.TransformPosition(FVector(x, 0.f, WeaponDesign::BowBrace * x * x / (48.f * 48.f))); };
		Test(X < 0.f ? TEXT("bow back/low limb") : TEXT("bow front/top limb"), Pt(X), Pt(X + 4.f), FMath::Abs(X));
	}
	// the string (straight at rest; to the nock while drawn) and the nocked arrow, from the rods UpdateBowBits places
	auto RodEnds = [](UStaticMeshComponent* C, FVector& P, FVector& Q)
	{
		const FVector Mid = C->GetComponentLocation(), Ax = C->GetComponentQuat().GetAxisZ() * C->GetComponentScale().Z * 50.f;
		P = Mid - Ax; Q = Mid + Ax;
	};
	if (BowBits.Num() >= 3 && BowBits[0] && BowBits[0]->IsVisible())
	{
		FVector P, Q;
		RodEnds(BowBits[0], P, Q); Test(TEXT("string upper"), P, Q, 0.f);
		RodEnds(BowBits[1], P, Q); Test(TEXT("string lower"), P, Q, 0.f);
		if (BowBits[2]->IsVisible()) { RodEnds(BowBits[2], P, Q); Test(TEXT("arrow"), P, Q, 0.f); }
	}
	else Test(TEXT("string"), B.TransformPosition(FVector(-48.f, 0.f, WeaponDesign::BowBrace)), B.TransformPosition(FVector(48.f, 0.f, WeaponDesign::BowBrace)), 0.f);
	// the quiver: its tube's four sides and middle, bottom to rim
	if (QuiverParts.Num() > 0 && QuiverParts[0] && QuiverParts[0]->IsVisible())
	{
		const FTransform Qt = QuiverParts[0]->GetAttachParent()->GetComponentTransform();
		for (int32 k = 0; k < 5; ++k)
		{
			const FVector Off = k == 4 ? FVector::ZeroVector : FVector(FMath::Cos(k * UE_HALF_PI), FMath::Sin(k * UE_HALF_PI), 0.f) * 6.5f;
			Test(FString::Printf(TEXT("quiver side %d"), k), Qt.TransformPosition(Off + FVector(0.f, 0.f, -22.f)), Qt.TransformPosition(Off + FVector(0.f, 0.f, 34.f)), 0.f);
		}
	}
	// -QuiverSearch: try a grid of spots for the quiver on every picture of the move; at the end log the spots it never touched
	// anything from, the snuggest first (the gap: how far the tube's side stands off the nearest armour, worst frame)
	static const bool bSearch = FParse::Param(FCommandLine::Get(), TEXT("QuiverSearch"));
	if (bSearch && BodyRoot)
	{
		struct FCand { FVector At; FRotator Turn; int32 Hits = 0; float Gap = 0.f; };
		static TArray<FCand> Cands;
		if (Cands.Num() == 0)
			for (float x = -30.f; x <= -10.f; x += 4.f) for (float y = 16.f; y <= 34.f; y += 3.f) for (float z = -44.f; z <= -24.f; z += 5.f)
				for (float p = -40.f; p <= -5.f; p += 7.f) for (float r = -24.f; r <= 0.f; r += 6.f)
					Cands.Add({ FVector(x, y, z), FRotator(p, 0.f, r) });
		const FTransform Root = BodyRoot->GetComponentTransform();
		const FVector Mid = Root.TransformPosition(FVector(-20.f, 25.f, -34.f));
		TArray<const FTri*> Near;
		for (const FTri& T : Tris) if (FVector::DistSquared(T.A, Mid) < FMath::Square(75.f)) Near.Add(&T);
		auto Crosses = [&](const FVector& P, const FVector& Q)
		{
			const FVector D = Q - P;
			for (const FTri* T : Near)
			{
				const FVector E1 = T->B - T->A, E2 = T->C - T->A, H = FVector::CrossProduct(D, E2);
				const float Det = FVector::DotProduct(E1, H);
				if (FMath::Abs(Det) < 1e-6f) continue;
				const FVector S = P - T->A;
				const float U = FVector::DotProduct(S, H) / Det;
				if (U < 0.f || U > 1.f) continue;
				const FVector Qv = FVector::CrossProduct(S, E1);
				const float V = FVector::DotProduct(D, Qv) / Det;
				if (V < 0.f || U + V > 1.f) continue;
				const float W = FVector::DotProduct(E2, Qv) / Det;
				if (W >= 0.f && W <= 1.f) return true;
			}
			return false;
		};
		for (FCand& C : Cands)
		{
			const FTransform Qt = FTransform(C.Turn, C.At) * Root;
			const FVector Bot = Qt.TransformPosition(FVector(0.f, 0.f, -22.f)), Top = Qt.TransformPosition(FVector(0.f, 0.f, 34.f));
			if (C.Hits == 0)
				for (int32 k = 0; k < 5 && C.Hits == 0; ++k)
				{
					const FVector Off = k == 4 ? FVector::ZeroVector : FVector(FMath::Cos(k * UE_HALF_PI), FMath::Sin(k * UE_HALF_PI), 0.f) * 6.5f;
					if (Crosses(Qt.TransformPosition(Off + FVector(0.f, 0.f, -22.f)), Qt.TransformPosition(Off + FVector(0.f, 0.f, 34.f)))) C.Hits++;
				}
			float Best = 1e9f;
			for (const FTri* T : Near) Best = FMath::Min(Best, FMath::PointDistToSegment(T->A, Bot, Top));
			C.Gap = FMath::Max(C.Gap, Best - 6.5f);
		}
		if (Frame == 38)
		{
			TArray<FCand> Ok = Cands.FilterByPredicate([](const FCand& C) { return C.Hits == 0; });
			Ok.Sort([](const FCand& A, const FCand& B) { return A.Gap < B.Gap; });
			UE_LOG(LogTemp, Log, TEXT("JEEBQUIVER %d of %d spots clear"), Ok.Num(), Cands.Num());
			for (int32 i = 0; i < FMath::Min(Ok.Num(), 600); ++i)
				UE_LOG(LogTemp, Log, TEXT("JEEBQUIVER gap %.1f  %.0f_%.0f_%.0f_%.0f_%.0f"), Ok[i].Gap, Ok[i].At.X, Ok[i].At.Y, Ok[i].At.Z, Ok[i].Turn.Pitch, Ok[i].Turn.Roll);
		}
	}
	FString Out;
	for (const TPair<FString, FString>& H : Hits) Out += FString::Printf(TEXT(" | %s at cm%s"), *H.Key, *H.Value);
	UE_LOG(LogTemp, Log, TEXT("JEEBCLEAR %d fisttop %.1f%s"), Frame, FistTop, Out.IsEmpty() ? TEXT(" clear") : *Out);
}
