#include "KnightCharacter.h"
#include "JeebProps.h"
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
#include "Components/SkinnedMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "KnightShieldRest.h"
#include "Engine/World.h"

// ---------------- The bow (2026-10-31, Breath of the Wild style) ----------------
// A bow (owner, 2026-11-06): the left or right button held draws the string (StartDraw) and the camera closes in; the draw fills over BowDrawTime and stays full for as long as it is
// held. Letting go of the last button looses the arrow (ReleaseDraw) toward what is under the reticle, faster and harder the fuller the draw.
// The crossbow: the right button brings it up, a left click shoots, and letting go of the right button lowers it without shooting.
// The player's machine decides; the server checks against its own timer and fires.

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
		if (Skills->CountItem(Id) > 0) { PickArrows(Id); return; }
	}
}

void AKnightCharacter::PickArrows(int32 Id)
{
	ArrowPick = Id;
	if (!HasAuthority()) Inv(EInvOp::ChooseArrows, Id);
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
	static const bool bTurnTest = FCString::Strifind(FCommandLine::Get(), TEXT("ShotTurn=")) != nullptr;   // (a camera swinging round on purpose: he is to follow it)
	return bShotFace && !bTurnTest && GetController() && FMath::Abs(FMath::FindDeltaAngleDegrees(ShotFaceYaw, GetController()->GetControlRotation().Yaw)) > 20.f;
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
	bCrossQueued = false;
}

float AKnightCharacter::DrawTimeNow() const
{
	const int32 K = SelectedKind();
	return K == JeebItems::K_Crossbow ? JeebTune::V().CrossRaise : K == JeebItems::K_Longbow ? JeebTune::V().LongDrawTime : BowDrawTime;
}

float AKnightCharacter::BowWalkShare() const
{
	const int32 K = SelectedKind();
	if (K == JeebItems::K_Crossbow) return Bow == EBow::Reload ? JeebTune::V().CrossReloadWalk : JeebTune::V().CrossAimWalk;
	return K == JeebItems::K_Longbow ? JeebTune::V().LongWalk : 0.5f;
}

void AKnightCharacter::RightDown()
{
	if (IsUIOpen() || bPanelCursor) return;   // (owner 2026-11-06: with the cursor out, a click on the HUD never aims, draws or blocks)
	bAimHeld = true;
	StartBlock();
	if (JeebItems::IsShooter(SelectedKind()) && !IsUIOpen() && !bPanelCursor) StartDraw();
}

void AKnightCharacter::RightUp()
{
	bAimHeld = false;
	StopBlock();
	if (Bow == EBow::Drawing)
	{
		if (IsUIOpen() || bPanelCursor || SelectedKind() == JeebItems::K_Crossbow) CancelDraw();   // (the crossbow is lowered without shooting)
		else if (!bAttackDown) ReleaseDraw();   // (let go, it shoots; the left button still down keeps it drawn)
	}
}

void AKnightCharacter::StartDraw()
{
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBBOW start: steel %d fire %d kind %d bow %d arrows %d atk %d bar %d %d %d %d %d sel %d"), Skills ? Skills->CountItem(JeebItems::ArrowId(JeebItems::T_Iron)) : -1, Skills ? Skills->CountItem(JeebItems::IdFireArrow) : -1, SelectedKind(), int32(Bow), ArrowSlotId(), bAttacking ? 1 : 0, Skills ? Skills->Hotbar[0] : -1, Skills ? Skills->Hotbar[1] : -1, Skills ? Skills->Hotbar[2] : -1, Skills ? Skills->Hotbar[3] : -1, Skills ? Skills->Hotbar[4] : -1, Skills ? Skills->Selected : -1);
	if (!Skills || !JeebItems::IsShooter(SelectedKind()) || Bow != EBow::None) return;
	if (SelectedKind() == JeebItems::K_Crossbow && CrossReloadLeft > 0.f) return;   // (not loaded yet)
	if (bAttacking || bKnockedDown || StunLeft > 0.f || bRolling || bSliding || bClimbing || bSitting || bMounted || bDrinking) return;
	if (ArrowSlotId() == 0) { if (IsLocallyControlled()) JeebMsg::Post(2.f, FColor::Orange, TEXT("You have no arrows.")); return; }
	BowKindDrawn = SelectedKind();
	SetBow(EBow::Drawing);
	if (GetNetMode() == NM_Standalone) return;
	if (!HasAuthority()) ServerBowDraw();
	else MulticastBow(1);
}

void AKnightCharacter::ReleaseDraw()
{
	if (Bow != EBow::Drawing) return;
	const bool bCross = SelectedKind() == JeebItems::K_Crossbow;
	if (bCross && BowT < JeebTune::V().CrossRaise) { bCrossQueued = true; return; }   // (no half shots: it goes as soon as it is up)
	FVector Target = FVector::ZeroVector;
	const FVector Dir = BowAimDir(&Target);
	const FVector From = ShotStart();
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBAIM start %s dir %s target %s speed %s"), *From.ToCompactString(), *Dir.ToCompactString(), *Target.ToCompactString(), *GetVelocity().ToCompactString());
	const float C = BowCharge;
	const int32 Id = ArrowSlotId();
	if (bCross) CrossReloadLeft = JeebTune::V().CrossReload;
	SetBow(bCross ? EBow::Reload : EBow::Recover);
	if (HasAuthority()) { FireArrow(Dir, C, Id, From); if (GetNetMode() != NM_Standalone) MulticastBow(2); }
	else ServerBowShoot(Dir, C, Id, From);
}

void AKnightCharacter::CancelDraw()
{
	if (Bow == EBow::None) return;
	const bool bWasDrawing = Bow == EBow::Drawing, bWasLoading = Bow == EBow::Reload;
	SetBow(EBow::None);
	if ((!bWasDrawing && !bWasLoading) || GetNetMode() == NM_Standalone) return;
	if (IsLocallyControlled() && !HasAuthority()) { if (bWasDrawing) ServerBowCancel(); }
	else if (HasAuthority()) MulticastBow(0);
}

void AKnightCharacter::ServerBowDraw_Implementation()
{
	if (Bow == EBow::Recover && BowT >= BowRecover * 0.5f) SetBow(EBow::None);   // (the player's machine may be a moment ahead of us, but only that: with the button held it draws again as soon as it can)
	if (Bow == EBow::Reload && CrossReloadLeft < 0.3f) { CrossReloadLeft = 0.f; SetBow(EBow::None); }
	StartDraw();
}

void AKnightCharacter::ServerBowShoot_Implementation(FVector_NetQuantizeNormal Dir, float Charge, int32 ArrowId, FVector_NetQuantize10 From)
{
	if (Bow != EBow::Drawing || !JeebItems::IsShooter(SelectedKind())) return;
	const bool bCross = SelectedKind() == JeebItems::K_Crossbow;
	if (bCross && (BowT < JeebTune::V().CrossRaise - 0.15f || CrossReloadLeft > 0.3f)) { CancelDraw(); return; }   // (never before it can be up, nor before it is loaded)
	const float C = bCross ? 1.f : FMath::Clamp(FMath::Min(Charge, BowT / DrawTimeNow() + 0.15f), 0.f, 1.f);   // (never more draw than we have seen)
	FVector D = FVector(Dir).GetSafeNormal();
	if (FVector::DotProduct(D, GetActorForwardVector()) < 0.2f) D = GetBaseAimRotation().Vector();   // (only ahead of him)
	if (bCross) CrossReloadLeft = JeebTune::V().CrossReload;
	SetBow(bCross ? EBow::Reload : EBow::Recover);
	FireArrow(D, C, ArrowId, CheckShotStart(From));
	MulticastBow(2);
}

void AKnightCharacter::ServerBowCancel_Implementation()
{
	CancelDraw();
}

void AKnightCharacter::MulticastBow_Implementation(uint8 Phase)
{
	if (HasAuthority() || IsLocallyControlled() || Phase > 3) return;   // (they already know)
	if (SelectedKind() == JeebItems::K_Crossbow && Phase == 2) { CrossReloadLeft = JeebTune::V().CrossReload; SetBow(EBow::Reload); return; }
	if (Phase == 3 && CrossReloadLeft <= 0.f) CrossReloadLeft = JeebTune::V().CrossReload * 0.5f;   // (joining part way through someone else's)
	SetBow(static_cast<EBow>(Phase));
}

// TESTING: [Jeebsland.Testing] UnlimitedArrows=True in DefaultGame.ini: arrows are never used up, and everyone is kept at 99
// iron and 99 fire arrows (owner, 2026-11-01)
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
	const int32 Kind = JeebItems::IsShooter(SelectedKind()) ? SelectedKind() : int32(JeebItems::K_Bow);
	const bool bCross = Kind == JeebItems::K_Crossbow;
	const float SpeedMul = Kind == JeebItems::K_Longbow ? JeebTune::V().LongSpeed / FMath::Max(float(BowSpeedMax), 1.f) : 1.f;
	const int32 Dmg = JeebItems::RollDamage(Skills, Kind, BowTier, JeebItems::ArrowTier(Id), bCrit, FMath::Lerp(BowDamageMin, BowDamageMax, FMath::Pow(C, 1.5f)));
	auto ByDraw = [C](float Min, float Mid, float Max) { return C < 0.5f ? FMath::Lerp(Min, Mid, C * 2.f) : FMath::Lerp(Mid, Max, C * 2.f - 1.f); };
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AProjectile* P = GetWorld()->SpawnActor<AProjectile>(AProjectile::StaticClass(), From, Dir.Rotation(), SP))
	{
		const bool bFull = C >= 1.f;
		P->Launch(this, Dir, bCross ? JeebTune::V().CrossSpeed : ByDraw(BowSpeedMin, BowSpeedMid, BowSpeedMax) * SpeedMul, 0.f, Dmg, bCrit, ESkillType::Archery, false, JeebItems::ArrowTier(Id), Id,
			bCross ? JeebTune::V().CrossSag : bFull ? BowSagFull : ByDraw(BowSagMin, BowSagMid, BowSagMax), bFull ? BowStraight : 0.f);
		if (bCross) P->GuardMul = JeebTune::V().CrossGuardMul;
	}
}

void AKnightCharacter::TickBow(float Dt)
{
	const bool bBowIn = JeebItems::IsShooter(SelectedKind());
	const bool bCross = SelectedKind() == JeebItems::K_Crossbow;
	const bool bOtherBow = Bow == EBow::Drawing && SelectedKind() != BowKindDrawn && (HasAuthority() || IsLocallyControlled());   // (a draw never carries over to another bow)
	const bool bTaken = !bBowIn || bOtherBow || bAttacking || bKnockedDown || StunLeft > 0.f || bRolling || bSliding || bClimbing || bSitting || bMounted || Health <= 0.f || bDrinking;
	// let down without shooting: another weapon, a roll, a kick or a swing, hitstun, knocked down, climbing, sitting ...
	if (Bow != EBow::None && (bTaken || (Bow != EBow::Reload && IsLocallyControlled() && IsUIOpen())))
		CancelDraw();
	// the crossbow's loading: whatever stopped it, it carries on from where it was as soon as he is free
	if (bCross && !bTaken && Bow == EBow::None && CrossReloadLeft > 0.f && (HasAuthority() || IsLocallyControlled()))
	{
		SetBow(EBow::Reload);
		if (HasAuthority() && GetNetMode() != NM_Standalone) MulticastBow(3);
	}
	if (Bow == EBow::Reload)
	{
		CrossReloadLeft -= Dt;
		if (CrossReloadLeft <= 0.f) { CrossReloadLeft = 0.f; SetBow(EBow::None); }
	}
	BowT += Dt;
	if (HasAuthority() && Skills && UnlimitedArrows())   // (testing: topped up to a full stack of each)
		for (const int32 Id : { JeebItems::ArrowId(JeebItems::T_Iron), JeebItems::IdFireArrow })
			if (const int32 Have = Skills->CountItem(Id); Have < USkillComponent::StartIronArrows) Skills->AddInvItem(Id, USkillComponent::StartIronArrows - Have);
	if (bShotFace && ShotStrafe != 0.f) AddMovementInput(GetActorRightVector(), ShotStrafe);   // (test pictures)
	if (bShotFace && bShotWalk) AddMovementInput(FRotator(0.f, ShotFaceYaw, 0.f).Vector(), 1.f);
	if (ShotBowAt > 0.f && GetWorld()->GetTimeSeconds() >= ShotBowAt) { ShotBowAt = -1.f; ReleaseDraw(); }   // (test pictures)
	if (Bow == EBow::Drawing)
	{
		BowCharge = FMath::Min(1.f, BowT / FMath::Max(DrawTimeNow(), 0.01f));   // (the crossbow has no draw: it is ready at once)
		if (bCross && bCrossQueued && IsLocallyControlled() && BowT >= JeebTune::V().CrossRaise) ReleaseDraw();
	}
	else if (Bow == EBow::Recover && BowT >= BowRecover) SetBow(EBow::None);
	// Cursor out (Tab) or a screen open: a draw in progress is cancelled without shooting (the arrow is kept), and both buttons are forgotten so nothing sticks or fires when it goes away again
	if (IsLocallyControlled() && (IsUIOpen() || bPanelCursor))
	{
		if (Bow == EBow::Drawing) CancelDraw();
		if (Smash == ESmash::Deciding || Smash == ESmash::Charging) SetSmash(ESmash::None);
		bAttackDown = false; bAimHeld = false;
	}
	// The button still down after a shot, a roll or a weapon swap: straight back to drawing
	const bool bAimButton = bAimHeld || (bAttackDown && !bCross);   // (only the right button aims the crossbow)
	if (IsLocallyControlled() && bAimButton && Bow == EBow::None && bBowIn && !bTaken && !IsUIOpen() && !bPanelCursor && ArrowSlotId() != 0 && !(bCross && CrossReloadLeft > 0.f)) StartDraw();

	// The camera closes in over his right shoulder while aiming (the player's own machine)
	if (IsLocallyControlled() && !ShotSideOn())
	{
		const bool bAimCam = Bow == EBow::Drawing || (bAimButton && bBowIn && !bTaken && !IsUIOpen() && !bPanelCursor);   // (it stays in for as long as the button is down, and pulls back out when it is let go)
		BowCamAlpha = FMath::FInterpTo(BowCamAlpha, bAimCam ? 1.f : 0.f, Dt, bAimCam ? 7.f : 5.f);
		if (USpringArmComponent* Boom = GetCameraBoom())
		{
			const float A = BowCamAlpha * BowCamAlpha * (3.f - 2.f * BowCamAlpha);
			// Aiming: the camera keeps up with him (its lag made arrows seem to curve as he moved). The engine's lag only steps by a frame's share, and around a lag speed of 80 that share
			// swings with the frame time, so at jump speed he shook a few cm on screen from frame to frame: past that speed there is simply no lag sideways.
			Boom->CameraLagSpeed = FMath::Lerp(8.f, 300.f, A * A);
			Boom->TargetArmLength = FMath::Lerp(540.f, BowCamLength, A);
			Boom->SocketOffset = FMath::Lerp(FVector(0.f, 0.f, 60.f), FVector(0.f, 75.f, 42.f), A);
			// A jump leaves the ground at full speed, and a camera nailed to him whips the whole world up and down with it: its height follows softly instead, the same whatever the frame rate.
			const float Z = GetActorLocation().Z;
			if (!bAimCamZSet) { AimCamZ = Z; bAimCamZSet = true; }
			AimCamZ = FMath::Lerp(Z, AimCamZ, FMath::Exp(-AimCamFollow * Dt));
			const float Off = AimCamMax * FMath::Tanh((AimCamZ - Z) / AimCamMax);
			Boom->TargetOffset = FVector(0.f, 0.f, Off * A);
		}
	}
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
FQuat KnightBowRestQuat()
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
	const bool bCross = SelectedKind() == JeebItems::K_Crossbow;
	const bool bShow = BowHold && (bCross || BowAimAlpha > 0.02f) && !bStowed && !(bCross && bDrinking);   // (with the crossbow put away for a drink, its string and bolt go too)
	if (BowString) BowString->SetVisibility(!bShow);
	if (!bShow) { for (UStaticMeshComponent* C : BowBits) if (C) C->SetVisibility(false); return; }
	if (BowBits.Num() == 0)
	{
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.9f, 0.88f, 0.78f)));   // string, upper half
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.9f, 0.88f, 0.78f)));   // lower half
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FLinearColor(0.5f, 0.34f, 0.17f)));   // the nocked arrow
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cone.Cone"), FLinearColor(0.6f, 0.6f, 0.7f)));             // its head
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cube.Cube"), FLinearColor(0.9f, 0.9f, 0.9f)));             // its feathers
		BowBits.Add(MakeBit(this, GetRootComponent(), TEXT("/Engine/BasicShapes/Cube.Cube"), FLinearColor(0.9f, 0.9f, 0.9f)));             // (across the first)
		for (UStaticMeshComponent* C : BowBits) { C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true); }
	}
	const FTransform B = BowHold->GetComponentTransform();
	const int32 ShowId = (IsLocallyControlled() || GetNetMode() == NM_Standalone || HasAuthority()) ? ArrowSlotId() : LookArrows;
	// An arrow laid from Nock to Tip, its feathers at the nock
	auto LayArrow = [&](const FVector& Nock, const FVector& Along, float Long, bool bShown)
	{
		for (int32 i = 2; i < 6; ++i) BowBits[i]->SetVisibility(bShown);
		if (!bShown) return;
		const FVector Tip = Nock + Along * Long;
		Rod(BowBits[2], Nock, Tip, 0.6f);
		BowBits[3]->SetWorldLocationAndRotation(Tip + Along * 2.f, FRotationMatrix::MakeFromZ(Along).Rotator());
		BowBits[3]->SetWorldScale3D(FVector(0.028f, 0.028f, 0.065f));
		if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(BowBits[3]->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), JeebItems::ArrowTipColor(ShowId));
		const FRotator Fl = FRotationMatrix::MakeFromX(Along).Rotator();
		for (int32 i = 4; i < 6; ++i)
		{
			BowBits[i]->SetWorldLocationAndRotation(Nock + Along * 6.f, (Fl.Quaternion() * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(i == 4 ? 0.f : 90.f))).Rotator());
			BowBits[i]->SetWorldScale3D(FVector(0.1f, 0.004f, 0.045f));
			if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(BowBits[i]->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), JeebItems::ArrowFeatherColor(ShowId));
		}
	};
	if (bCross)
	{
		// (the model has no string of its own: from one end of the prod back to the nut, drawn up as it loads, with the arrow lying on the stock once it is loaded)
		if (!bBitsCross)
		{
			bBitsCross = true;
			for (int32 i = 0; i < 2; ++i) if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(BowBits[i]->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.09f, 0.075f, 0.055f));   // (a dark cord: as long as the prod is wide, a pale one draws the eye across his face)
		}
		const FVector PA = B.TransformPosition(FVector(JeebProps::CrossTipX, -JeebProps::CrossTipY, JeebProps::CrossZ)), PB = B.TransformPosition(FVector(JeebProps::CrossTipX, JeebProps::CrossTipY, JeebProps::CrossZ));
		const float Load = CrossReloadLeft > 0.f ? FMath::Clamp(1.f - CrossReloadLeft / JeebTune::V().CrossReload, 0.f, 1.f) : 1.f;
		const FVector Nock = B.TransformPosition(FVector(FMath::Lerp(JeebProps::CrossTipX, JeebProps::CrossNutX, FMath::SmoothStep(0.25f, 0.8f, Load)), 0.f, JeebProps::CrossZ));
		for (int32 i = 0; i < 2; ++i) { BowBits[i]->SetVisibility(true); Rod(BowBits[i], i == 0 ? PA : PB, Nock, 0.35f); }
		LayArrow(Nock, B.TransformVectorNoScale(FVector::ForwardVector), JeebProps::CrossBoltLen, CrossReloadLeft <= 0.f && ShowId != 0);
		return;
	}
	if (bBitsCross)
	{
		bBitsCross = false;
		for (int32 i = 0; i < 2; ++i) if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(BowBits[i]->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.9f, 0.88f, 0.78f));
	}
	const float TipX = SelectedKind() == JeebItems::K_Longbow ? JeebProps::LongTipX : 48.f, Brace = SelectedKind() == JeebItems::K_Longbow ? JeebProps::LongBrace : float(WeaponDesign::BowBrace);
	const FVector TipA = B.TransformPosition(FVector(-TipX, 0.f, Brace)), TipB = B.TransformPosition(FVector(TipX, 0.f, Brace));
	const FVector Rest = B.TransformPosition(FVector(BowTune().Rest, 0.f, Brace));   // (the nocking point: level with the arrow rest, just above the fist)
	// the nock: in the drawing fist while drawn, back on the bow as it is let go
	FVector Nock = Rest;
	if (WristR) Nock = FMath::Lerp(Rest, WristR->GetComponentTransform().TransformPosition(KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR)), FMath::Clamp(BowAimAlpha * 1.4f - 0.2f, 0.f, 1.f));
	if (Bow == EBow::Recover) Nock = FMath::Lerp(Nock, Rest, FMath::Clamp(BowT / 0.06f, 0.f, 1.f));
	for (int32 i = 0; i < 2; ++i) { BowBits[i]->SetVisibility(true); Rod(BowBits[i], i == 0 ? TipA : TipB, Nock, 0.3f); }
	LayArrow(Nock, (B.TransformPosition(FVector(BowTune().Rest, 0.f, 0.f)) - Nock).GetSafeNormal(), 74.f, Bow == EBow::Drawing && ShowId != 0);   // (over the top of the fist)
}

// The quiver: a leather tube hung on his right hip like a sword's sheath (clear of the cape; owner, 2026-10-31), its mouth
// tipped forward with fletched arrows showing, while the bow is his weapon
void AKnightCharacter::UpdateQuiver()
{
	const bool bShow = JeebItems::IsShooter(SelectedKind());
	if (!bShow) { for (UStaticMeshComponent* C : QuiverParts) if (C) C->SetVisibility(false); return; }
	if (QuiverParts.Num() == 0)
	{
		// (in the hips' frame: at the back of the right hip, behind where the arm hangs and swings, as Kingdom Come: Deliverance's
		// archers wear it (2026-11-01: the arm used to pass through it at the side); pitch -: the mouth leans forward; roll -:
		// the bottom swings out from the leg)
		USceneComponent* Q = NewObject<USceneComponent>(this);
		Q->SetupAttachment(GetRootComponent());
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
	// (the feathers are those of the arrows he is about to shoot, owner 2026-11-05: emerald arrows have green ones)
	const int32 Id = (IsLocallyControlled() || GetNetMode() == NM_Standalone || HasAuthority()) ? ArrowSlotId() : LookArrows;
	if (Id != QuiverArrowId && Id != 0)
	{
		QuiverArrowId = Id;
		for (int32 i = 4; i < QuiverParts.Num(); i += 2)
			if (QuiverParts[i])
				if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(QuiverParts[i]->GetMaterial(0))) M->SetVectorParameterValue(TEXT("Color"), JeebItems::ArrowFeatherColor(Id));
	}
}
