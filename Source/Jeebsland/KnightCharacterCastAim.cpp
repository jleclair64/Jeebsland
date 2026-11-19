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

// The wand's and staff's aim (owner 2026-11-18, picture 13: "the red dot is where I'll want the reticle/spell path to go. This way the player can see
// where the spell is aiming/going and it's not hidden behind the knight"). The camera looks over his right shoulder with the knight left of the
// middle; the reticle sits up and right of the middle, by his head, so neither he nor the bolt hides it. Whatever is under it is where the bolt goes:
// the player's machine traces through it from the camera (as the bow does through the middle), turns him that way and sends the point; the server
// keeps it if it lies ahead of where he looks and fires from the wand's tip or the staff's orb toward it (AttackLands).

static TAutoConsoleVariable<FString> CVarReticle(TEXT("jeeb.Magic.Reticle"), TEXT("0.56,0.40"), TEXT("Where the wand's and staff's reticle sits, as shares of the screen across and down (0.5,0.5 is the middle)."), ECVF_Default);

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

void AKnightCharacter::ServerCastAim_Implementation(FVector_NetQuantize Target)
{
	const FVector T(Target);
	if (T.ContainsNaN() || FVector::Dist(T, GetActorLocation()) > 30000.f) return;
	CastTarget = T;
	CastTargetAt = GetWorld()->GetTimeSeconds();
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
