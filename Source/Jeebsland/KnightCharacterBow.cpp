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
#include "Components/SkinnedMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "KnightShieldRest.h"
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
	if (bShotFace) UE_LOG(LogTemp, Log, TEXT("JEEBBOW start: steel %d fire %d kind %d bow %d arrows %d atk %d bar %d %d %d %d %d sel %d"), Skills ? Skills->CountItem(JeebItems::ArrowId(JeebItems::T_Iron)) : -1, Skills ? Skills->CountItem(JeebItems::IdFireArrow) : -1, SelectedKind(), int32(Bow), ArrowSlotId(), bAttacking ? 1 : 0, Skills ? Skills->Hotbar[0] : -1, Skills ? Skills->Hotbar[1] : -1, Skills ? Skills->Hotbar[2] : -1, Skills ? Skills->Hotbar[3] : -1, Skills ? Skills->Hotbar[4] : -1, Skills ? Skills->Selected : -1);
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
		for (const int32 Id : { JeebItems::ArrowId(JeebItems::T_Iron), JeebItems::IdFireArrow })
			if (const int32 Have = Skills->CountItem(Id); Have < USkillComponent::StartIronArrows) Skills->AddInvItem(Id, USkillComponent::StartIronArrows - Have);
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
	const bool bShow = SelectedKind() == JeebItems::K_Bow;
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
}
