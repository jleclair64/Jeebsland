#include "KnightCharacter.h"
#include "KnightDesign.h"
#include "Inventory.h"
#include "Projectile.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "JeebTuning.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

// The wand's and staff's aim (owner 2026-11-18, picture 13: "the red dot is where I'll want the reticle/spell path to go. This way the player can see
// where the spell is aiming/going and it's not hidden behind the knight"). The camera looks over his right shoulder with the knight left of the
// middle; the reticle sits up and right of the middle, by his head, so neither he nor the bolt hides it. Whatever is under it is where the bolt goes:
// the player's machine traces through it from the camera (as the bow does through the middle), turns him that way and sends the point; the server
// keeps it if it lies ahead of where he looks and fires from the wand's tip or the staff's orb toward it (AttackLands).

static TAutoConsoleVariable<FString> CVarReticle(TEXT("jeeb.Magic.Reticle"), TEXT("0.5,0.5"), TEXT("Where the wand's and staff's reticle sits, as shares of the screen across and down (0.5,0.5 is the middle, as the bow's since 2026-11-22; was 0.56,0.40)."), ECVF_Default);

FVector2D AKnightCharacter::CastReticleAt()
{
	TArray<FString> P;
	CVarReticle.GetValueOnGameThread().ParseIntoArray(P, TEXT(","));
	if (P.Num() != 2) return FVector2D(0.56f, 0.40f);
	return FVector2D(FMath::Clamp(FCString::Atof(*P[0]), 0.05f, 0.95f), FMath::Clamp(FCString::Atof(*P[1]), 0.05f, 0.95f));
}

float AKnightCharacter::StaffSlide()
{
	static IConsoleVariable* Hold = IConsoleManager::Get().FindConsoleVariable(TEXT("jeeb.Knight.StaffHold"));   // (KnightAnimatorCast.cpp)
	return Hold ? FMath::Clamp(Hold->GetFloat(), 36.3f, 160.f) - 36.3f : 0.f;   // (36.3: the model's butt behind its grip as built)
}

bool AKnightCharacter::ShowsCastReticle() const
{
	const int32 K = SelectedKind();
	return (K == JeebItems::K_Wand || K == JeebItems::K_Staff) && !bStowed && !bSitting && !bMounted && !bDrinking && !bClimbing && !InClimbMove()
		&& !bKnockedDown && Health > 0.f && !IsUIOpen();
}

FVector AKnightCharacter::CastAimPoint() const
{
	const FVector Chest = GetActorLocation() + FVector(0.f, 0.f, 40.f);
	const FVector Ahead = Chest + GetBaseAimRotation().Vector() * 3000.f;
	if (ShotSideOn()) return Chest + GetActorForwardVector() * 3000.f;   // (test pictures from the side: straight ahead of him)
	if (!IsLocallyControlled()) return Ahead;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->PlayerCameraManager) return Ahead;
	int32 VX = 0, VY = 0;
	PC->GetViewportSize(VX, VY);
	FVector From, Dir;
	const FVector2D R = CastReticleAt();
	if (VX <= 0 || VY <= 0 || !PC->DeprojectScreenPositionToWorld(VX * R.X, VY * R.Y, From, Dir)) return Ahead;
	// (from abreast of him on, as the bow's trace: nothing between the camera and the knight catches the aim)
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Start = From + Dir * FMath::Max(150.f, float(FVector::DotProduct(Chest - From, Dir)) + 20.f);
	FVector Target = From + Dir * 9000.f;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CastAim), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Target, ECC_Visibility, Q)) Target = Hit.ImpactPoint;
	const float Body = AProjectile::FirstBodyAlong(GetWorld(), Start, Target, this);   // (a knight or foe under the reticle: the trace sees through them)
	if (Body <= 1.f) Target = Start + (Target - Start) * Body;
	if (FVector::Dist2D(Target, GetActorLocation()) < 200.f) Target = CamLoc + Dir * 3000.f;   // (something right in front of him: still along the reticle's line)
	return Target;
}

float AKnightCharacter::CastFacingYaw(float Fallback) const
{
	if (!GetWorld() || GetWorld()->GetTimeSeconds() - CastTargetAt > 1.5) return Fallback;
	const FVector D = CastTarget - GetActorLocation();
	if (FVector2D(D.X, D.Y).Size() < 100.f) return Fallback;
	const float Yaw = D.Rotation().Yaw;
	return FMath::Abs(FMath::FindDeltaAngleDegrees(Fallback, Yaw)) <= 75.f ? Yaw : Fallback;   // (only ahead of where he looks)
}

void AKnightCharacter::ServerCastAim_Implementation(FVector_NetQuantize Target, FVector_NetQuantize10 From)
{
	const FVector T(Target);
	if (T.ContainsNaN() || FVector::Dist(T, GetActorLocation()) > 30000.f) return;
	CastTarget = T;
	CastFrom = FVector(From);   // (checked when the bolt leaves: CheckCastStart)
	CastTargetAt = GetWorld()->GetTimeSeconds();
}

// (2026-11-22, owner: "similar to the arrow ... comes out in line with where the camera is looking, not in front of the wizard") The bolt leaves
// from the camera's own line, abreast of the wand's tip or the staff's orb, a little left of and below the middle of the screen as the arrow
// does (ShotLeft, ShotDown), and flies straight to what is under the reticle (now the middle of the screen, as the bow's).
FVector AKnightCharacter::CastLineStart(int32 Kind) const
{
	const FVector Hand = CastStartPoint(Kind);
	if (ShotSideOn() || !IsLocallyControlled()) return Hand;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->PlayerCameraManager) return Hand;
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector CamDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
	const FRotationMatrix CamM(PC->PlayerCameraManager->GetCameraRotation());
	return CheckCastStart(CamLoc + CamDir * FMath::Max(0.f, float(FVector::DotProduct(Hand - CamLoc, CamDir)))
		- CamM.GetUnitAxis(EAxis::Y) * ShotLeft - CamM.GetUnitAxis(EAxis::Z) * ShotDown, Kind);
}

FVector AKnightCharacter::CheckCastStart(const FVector& Asked, int32 Kind) const
{
	const FVector Hand = CastStartPoint(Kind);
	if (Asked.ContainsNaN() || FVector::Dist(Asked, Hand) > 200.f || FVector::Dist(Asked, GetActorLocation()) > 260.f) return Hand;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CastLine), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation() + FVector(0.f, 0.f, 40.f), Asked, ECC_Visibility, Q)) return Hand;   // (a wall beside him)
	return Asked;
}

// ---------------- The charged shot ----------------
float AKnightCharacter::CastChargeFrac() const
{
	if (!bCastCharging && !bCastChargingRep) return 0.f;
	const auto& T = JeebTune::V();
	const float Held = float(GetWorld()->GetTimeSeconds() - CastChargeStart) - T.CastTapTime;
	return FMath::Clamp(Held / FMath::Max(0.05f, T.CastChargeTime - T.CastTapTime), 0.f, 1.f);
}

void AKnightCharacter::BeginCastCharge()
{
	const int32 K = SelectedKind();
	if (bCastCharging || !(K == JeebItems::K_Wand || K == JeebItems::K_Staff)) return;
	bCastCharging = true;
	CastChargeStart = GetWorld()->GetTimeSeconds();
	if (GetNetMode() != NM_Standalone && !HasAuthority()) ServerCastCharge(true);
	else if (HasAuthority()) bCastChargingRep = true;
}

void AKnightCharacter::CancelCastCharge()
{
	if (!bCastCharging) return;
	bCastCharging = false;
	if (IsLocallyControlled() && !HasAuthority()) ServerCastCharge(false);
	if (HasAuthority()) bCastChargingRep = false;
}

void AKnightCharacter::ReleaseCastCharge()
{
	if (!bCastCharging) return;
	const float C = CastChargeFrac();
	CancelCastCharge();
	const int32 K = SelectedKind();
	if (!(K == JeebItems::K_Wand || K == JeebItems::K_Staff) || bAttacking || bDrinking || bKnockedDown || IsUIOpen() || bPanelCursor) return;
	StartAttack(EAtkStyle::Cast, 1, C);
}

void AKnightCharacter::ServerCastCharge_Implementation(bool bOn)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bOn && bCastCharging) CastChargeHeldSrv = float(Now - CastChargeStart);   // (the hold as the server timed it)
	bCastCharging = bOn;
	bCastChargingRep = bOn;
	if (bOn) { CastChargeStart = Now; CastChargeHeldSrv = 0.f; }
}

// (server) the most charge a client's cast may claim: what the server itself timed, plus a little for the network
float AKnightCharacter::ServerCastChargeLimit()
{
	const auto& T = JeebTune::V();
	const float Held = CastChargeHeldSrv - T.CastTapTime;
	CastChargeHeldSrv = 0.f;   // (used once)
	return FMath::Clamp(Held / FMath::Max(0.05f, T.CastChargeTime - T.CastTapTime) + 0.1f, 0.f, 1.f);
}

void AKnightCharacter::OnRep_CastCharging()
{
	if (bCastChargingRep) CastChargeStart = GetWorld()->GetTimeSeconds();   // (someone else's: timed from when we heard)
}

void AKnightCharacter::TickCastCharge(float Dt)
{
	if (HasAuthority())   // (the magic bar fills again after a pause: the server's number)
	{
		const auto& TM = JeebTune::V();
		if (Magic < TM.MagicMax && GetWorld()->GetTimeSeconds() - MagicUsedAt > TM.MagicRegenDelay) Magic = FMath::Min(TM.MagicMax, Magic + TM.MagicRegen * Dt);
		else if (Magic > TM.MagicMax) Magic = TM.MagicMax;
	}
	const int32 K = SelectedKind();
	const bool bCaster = K == JeebItems::K_Wand || K == JeebItems::K_Staff;
	if (bCastCharging && IsLocallyControlled() && (!bCaster || bStowed || bKnockedDown || bDrinking || bRolling || bClimbing || bSitting || bMounted || Health <= 0.f || IsUIOpen()))
		CancelCastCharge();
	if (IsNetMode(NM_DedicatedServer)) return;
	// The glow: a small ball at the wand's tip or the staff's orb, growing from nothing to about 18 cm and brightening as the charge fills
	const bool bShow = bCaster && (IsLocallyControlled() ? bCastCharging : bCastChargingRep);
	const float F = bShow ? CastChargeFrac() : 0.f;
	const bool bAny = bShow && float(GetWorld()->GetTimeSeconds() - CastChargeStart) > JeebTune::V().CastTapTime;
	if (!bAny) { if (CastChargeGlow) CastChargeGlow->SetVisibility(false); return; }
	const FLinearColor Gem = K == JeebItems::K_Staff ? FLinearColor(0.02f, 0.72f, 1.f) : FLinearColor(0.62f, 0.22f, 1.f);   // (the bolts' colours: Projectile.cpp)
	if (!CastChargeGlow)
	{
		CastChargeGlow = NewObject<UStaticMeshComponent>(this);
		CastChargeGlow->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		CastChargeGlow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CastChargeGlow->SetCastShadow(false);
		CastChargeGlow->SetupAttachment(GetRootComponent());
		CastChargeGlow->SetUsingAbsoluteLocation(true);
		CastChargeGlow->SetUsingAbsoluteRotation(true);
		CastChargeGlow->SetUsingAbsoluteScale(true);
		CastChargeGlow->RegisterComponent();
		if (UMaterialInterface* OrbMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/M_GlowOrb.M_GlowOrb")))
			CastChargeGlow->CreateDynamicMaterialInstance(0, OrbMat);
	}
	if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(CastChargeGlow->GetMaterial(0)))
	{
		M->SetVectorParameterValue(TEXT("Color"), Gem);
		M->SetScalarParameterValue(TEXT("Strength"), FMath::Lerp(0.4f, 1.3f, F) * (F >= 1.f ? 1.f + 0.25f * FMath::Sin(float(GetWorld()->GetTimeSeconds()) * 14.f) : 1.f));
	}
	CastChargeGlow->SetWorldLocation(CastStartPoint(K));
	CastChargeGlow->SetWorldScale3D(FVector(FMath::Lerp(0.04f, 0.18f, F)));
	CastChargeGlow->SetVisibility(true);
}

FVector AKnightCharacter::CastStartPoint(int32 Kind) const
{
	const FVector Old = GetActorLocation() + GetActorForwardVector() * 70.f + FVector(0.f, 0.f, 30.f);   // (where bolts left from before)
	if (!WristR) return Old;
	const FTransform W = WristR->GetComponentTransform();
	const FVector Grip = W.TransformPosition(KnightDesign::GripPos(false) - KnightDesign::PivotPos(KnightDesign::P_WristR));
	const FVector Along = W.TransformVectorNoScale(KnightDesign::BladeDir(1)).GetSafeNormal();
	// (the wand's crystal 40.9 cm from its grip; the staff's orb 117.3, less what the walking-staff hold slides it down: Tools/fixlook_assets.py)
	const float Reach = Kind == JeebItems::K_Staff ? FMath::Max(10.f, 117.3f - StaffSlide()) : 43.f;
	const FVector Tip = Grip + Along * Reach;
	if (Tip.ContainsNaN() || FVector::Dist(Tip, GetActorLocation()) > 230.f) return Old;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CastStart), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation() + FVector(0.f, 0.f, 40.f), Tip, ECC_Visibility, Q)) return Old;   // (a wall at his hand)
	return Tip;
}

// ---------------- The wizard's magic bar (2026-11-22, owner) ----------------
float AKnightCharacter::GetMagicFraction() const
{
	return FMath::Clamp(Magic / FMath::Max(1.f, JeebTune::V().MagicMax), 0.f, 1.f);
}
